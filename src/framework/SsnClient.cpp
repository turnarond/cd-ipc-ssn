/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * SsnClient 客户端实现
 */
// 文件: src/framework/SsnClient.cpp
// 功能: SsnClient 客户端实现——connect 创建 ssn::Node 并建立到服务端的
//       连接（Node 后台线程自驱动事件收发）；callJson 单 in-flight 同步
//       调用（call_mutex_ 串行化 + 条件变量按请求序号等待应答，wait_for
//       超时）；subscribe/unsubscribe 以信封协议握手（sub→suback /
//       unsub→unsuback）；disconnect 停后台线程并销毁节点。应答/发布消息
//       在 Node 后台事件线程投递（无内部锁），实现只做数据拷贝与通知，
//       订阅回调在锁外执行（见头文件类注释）。
#include "ssn/framework/SsnClient.hpp"

#include <chrono>
#include <exception>
#include <thread>
#include <utility>

#include "ssn/node/Node.hpp"

#include "NodeBus.hpp"
#include "util/ssn_log.h"

namespace ssn {

namespace {

// 从应答 JSON 中提取框架错误消息（无 error 字段返回空串）
std::string error_message_of(const nlohmann::json& resp) {
    if (resp.is_object() && resp.contains("error") && resp["error"].is_object() &&
        resp["error"].contains("message")) {
        return resp["error"]["message"].get<std::string>();
    }
    return std::string();
}

// std::string → MESSAGE 帧字节视图
ByteView to_byte_view(const std::string& bytes) {
    return ByteView{reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
}

}  // namespace

SsnClient::SsnClient() = default;

SsnClient::~SsnClient() {
    disconnect();
}

bool SsnClient::connect(const std::string& peer_address, uint64_t timeout_ms) {
    // 与 disconnect/并发 connect 互斥（缺陷背景：node_/peer_ 原在锁外写，并发
    // connect 会各自建节点后写覆盖（旧节点泄漏），且连续两次启动事件线程
    // 造成资源竞争）
    std::lock_guard<std::mutex> call_lock(call_mutex_);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (connected_) {
            LOG_WARN("SsnClient: 已连接，重复 connect 被拒绝: %s", peer_address.c_str());
            return false;
        }
    }

    // 创建节点并接线事件分发（先 setEventHandler 再 startBackground，事件不丢）
    auto created = Node::create(NodeConfig{});
    if (!created) {
        LOG_ERROR("SsnClient: 节点创建失败");
        return false;
    }
    auto node = std::make_unique<Node>(std::move(created.value()));
    node->setEventHandler([this](const NodeEvent& event) { handleEvent(event); });
    if (!node->startBackground()) {
        LOG_ERROR("SsnClient: 节点后台线程启动失败");
        node.reset();
        return false;
    }

    // 建立连接（同步等待至多 timeout_ms）
    auto conn = node->connect(ListenAddress{peer_address.c_str()},
                              ConnectOptions{std::chrono::milliseconds(timeout_ms)});
    if (!conn) {
        LOG_ERROR("SsnClient: 连接失败: %s", peer_address.c_str());
        (void)node->stop();
        (void)node->waitStopped();
        node.reset();
        return false;
    }

    // connect() 的 InProgress 路径立即返回 Connecting 态 PeerId，须等待后台
    // 线程完成握手（PeerState::Connected）后方可收发（Node API 契约，集成
    // 测试同款等待模式）；ConnectFailed/Failed/Disconnected 视为失败提前退出
    bool established = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto info = node->peerInfo(conn.value());
        if (info.ok()) {
            const PeerState st = info.value().state;
            if (st == PeerState::Connected) { established = true; break; }
            if (st == PeerState::ConnectFailed || st == PeerState::Failed ||
                st == PeerState::Disconnected) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!established) {
        LOG_ERROR("SsnClient: 连接握手未完成（超时或失败）: %s", peer_address.c_str());
        (void)node->stop();
        (void)node->waitStopped();
        node.reset();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        node_ = std::move(node);
        peer_id_ = conn.value();
        peer_ = peer_address;
        connected_ = true;
    }
    return true;
}

void SsnClient::disconnect() {
    // 与 callJson/subscribe/unsubscribe 互斥（Issue #5-5）：等待在途调用结束
    // 再销毁节点，消除「disconnect 与并发 callJson」的 UAF 窗口。在途调用
    // 最迟在自身超时后返回，disconnect 可能因此阻塞至多一个超时周期——调用方
    // 应避免跨线程同时调用（见头文件并发约束注释）
    std::lock_guard<std::mutex> call_lock(call_mutex_);
    std::unique_ptr<Node> node;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!connected_) {
            return;   // 未连接：幂等空操作
        }
        connected_ = false;
        node = std::move(node_);
    }
    // 先停后台事件线程再析构：事件线程可能正执行 handleEvent（访问成员），
    // stop + waitStopped 保证其退出后 this 才被释放
    if (node) {
        (void)node->stop();
        (void)node->waitStopped();
    }
}

bool SsnClient::connected() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return connected_;
}

const std::string& SsnClient::peer() const {
    return peer_;   // connect 后不变，无需加锁（与 SsnService::listenHost 同约定）
}

bool SsnClient::callJson(const std::string& url, const nlohmann::json& req,
                         nlohmann::json& resp, uint64_t timeout_ms) {
    // 单 in-flight：同一 client 的并发调用在此串行化
    std::lock_guard<std::mutex> call_lock(call_mutex_);

    // call_mutex_ 保证 disconnect 不会在本调用期间销毁节点（存活保证），
    // 故锁外使用裸指针安全
    Node* node;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!connected_ || !node_) {
            LOG_ERROR("SsnClient: 客户端未连接，无法调用: %s", url.c_str());
            return false;
        }
        node = node_.get();
    }

    // 重置应答状态后发起调用（应答在事件线程按序号匹配写回并通知）
    const std::uint64_t seq = next_seq_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(reply_mutex_);
        reply_pending_ = false;
        reply_seq_ = 0;
        reply_ok_ = false;
        reply_data_ = nullptr;
    }
    if (!node->send(peer_id_, to_byte_view(bus::encode_req(url, seq, req.dump())))) {
        LOG_ERROR("SsnClient: RPC 调用发送失败: %s", url.c_str());
        return false;
    }

    // 等待匹配序号的应答；超时后到达的迟到 rep 因序号不符被事件线程丢弃
    //（根治旧 Issue #5-7 迟到应答覆盖新调用的竞态）
    std::unique_lock<std::mutex> reply_lock(reply_mutex_);
    if (!reply_cv_.wait_for(reply_lock, std::chrono::milliseconds(timeout_ms),
                            [this, seq] { return reply_pending_ && reply_seq_ == seq; })) {
        LOG_ERROR("SsnClient: RPC 调用超时: %s (%llu ms)", url.c_str(),
                  static_cast<unsigned long long>(timeout_ms));
        return false;
    }
    resp = reply_data_;

    // 服务端返回框架错误（如 1001 方法不存在，rep.ok=false 且 b 为错误体）→ 失败
    if (!reply_ok_) {
        LOG_WARN("SsnClient: 方法 %s 返回错误: %s", url.c_str(),
                 error_message_of(resp).c_str());
        return false;
    }
    return true;
}

bool SsnClient::subscribe(const std::string& topic, MsgHandler handler, uint64_t timeout_ms) {
    // 与 disconnect/unsubscribe 互斥：持锁覆盖「登记处理器 → 信封握手」整个
    // 窗口。与 callJson 同锁同顺序（call_mutex_ → state_mutex_），无死锁风险
    std::lock_guard<std::mutex> call_lock(call_mutex_);
    if (topic.empty() || topic[0] != '/' || !handler) {
        LOG_ERROR("SsnClient: subscribe 参数非法: %s", topic.c_str());
        return false;
    }
    Node* node;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!connected_ || !node_) {
            LOG_ERROR("SsnClient: 客户端未连接，无法订阅: %s", topic.c_str());
            return false;
        }
        node = node_.get();
    }

    // 先登记本地处理器（同名主题覆盖），再向服务端握手；失败则回滚
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        subs_[topic] = std::move(handler);
    }
    {
        std::lock_guard<std::mutex> lock(ack_mutex_);
        ack_pending_ = false;
        ack_topic_.clear();
    }
    if (!node->send(peer_id_, to_byte_view(bus::encode_sub(topic)))) {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        subs_.erase(topic);
        LOG_ERROR("SsnClient: 订阅发送失败: %s", topic.c_str());
        return false;
    }
    std::unique_lock<std::mutex> ack_lock(ack_mutex_);
    if (!ack_cv_.wait_for(ack_lock, std::chrono::milliseconds(timeout_ms),
                          [this, &topic] { return ack_pending_ && ack_topic_ == topic; })) {
        std::lock_guard<std::mutex> slock(subs_mutex_);
        subs_.erase(topic);
        LOG_ERROR("SsnClient: 订阅超时: %s", topic.c_str());
        return false;
    }
    return true;
}

bool SsnClient::unsubscribe(const std::string& topic) {
    // 与 disconnect/subscribe 互斥（稳定性加固 I2）：防止退订握手与 disconnect
    // 销毁节点交错（UAF 窗口）
    std::lock_guard<std::mutex> call_lock(call_mutex_);
    Node* node;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        node = node_.get();
    }
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        subs_.erase(topic);
    }
    if (!node) {
        LOG_WARN("SsnClient: 客户端未连接，无法退订: %s", topic.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(ack_mutex_);
        ack_pending_ = false;
        ack_topic_.clear();
    }
    if (!node->send(peer_id_, to_byte_view(bus::encode_unsub(topic)))) {
        LOG_WARN("SsnClient: 退订发送失败: %s", topic.c_str());
        return false;
    }
    // 与旧实现同语义：服务端确认（unsuback）到达才算退订完成，超时返回 false
    std::unique_lock<std::mutex> ack_lock(ack_mutex_);
    if (!ack_cv_.wait_for(ack_lock, std::chrono::milliseconds(5000),
                          [this, &topic] { return ack_pending_ && ack_topic_ == topic; })) {
        LOG_WARN("SsnClient: 退订超时: %s", topic.c_str());
        return false;
    }
    return true;
}

void SsnClient::handleEvent(const NodeEvent& event) {
    if (event.type != NodeEventType::MessageReceived) {
        return;   // 连接建立/断开事件：连接状态由本地 connect/disconnect 管理
    }
    if (event.message.empty()) {
        return;
    }
    std::string text(reinterpret_cast<const char*>(event.message.data()),
                     event.message.size());
    bus::Envelope env;
    if (!bus::decode(text, env)) {
        LOG_WARN("SsnClient: 收到非法信封，丢弃");
        return;
    }

    switch (env.kind) {
    case bus::Kind::Rep: {
        nlohmann::json data = nlohmann::json::object();
        try {
            if (!env.body.empty()) { data = nlohmann::json::parse(env.body); }
        } catch (const std::exception& e) {
            // 应答体损坏按框架错误处理（保留错误体解析由调用方 error_message_of 承担）
            LOG_WARN("SsnClient: 应答 JSON 解析失败: %s", e.what());
            data = nlohmann::json::object();
        }
        {
            std::lock_guard<std::mutex> lock(reply_mutex_);
            reply_seq_ = env.seq;
            reply_ok_ = env.ok;
            reply_data_ = std::move(data);
            reply_pending_ = true;
        }
        reply_cv_.notify_all();
        break;
    }
    case bus::Kind::Pub: {
        MsgHandler handler;
        {
            std::lock_guard<std::mutex> lock(subs_mutex_);
            auto it = subs_.find(env.url);
            if (it == subs_.end()) {
                return;   // 未订阅主题（含已退订后迟到的发布）：丢弃
            }
            handler = it->second;
        }
        nlohmann::json data = nlohmann::json::object();
        try {
            if (!env.body.empty()) { data = nlohmann::json::parse(env.body); }
        } catch (const std::exception& e) {
            LOG_WARN("SsnClient: 发布消息 JSON 解析失败: %s", e.what());
            return;
        }
        // handler 在锁外执行（拷贝 function 后释放锁）；异常捕获丢弃该消息
        try {
            handler(env.url, data);
        } catch (const std::exception& e) {
            LOG_WARN("SsnClient: 订阅回调异常（消息已丢弃）: %s", e.what());
        } catch (...) {
            LOG_WARN("SsnClient: 订阅回调未知异常（消息已丢弃）");
        }
        break;
    }
    case bus::Kind::SubAck:
    case bus::Kind::UnsubAck: {
        {
            std::lock_guard<std::mutex> lock(ack_mutex_);
            ack_topic_ = env.url;
            ack_pending_ = true;
        }
        ack_cv_.notify_all();
        break;
    }
    default:
        // 服务端不会收到 req 类信封；忽略未知/不适用种类
        break;
    }
}

}  // namespace ssn
