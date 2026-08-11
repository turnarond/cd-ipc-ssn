// 文件: src/framework/SsnClient.cpp
// 功能: SsnClient 客户端实现——connect 创建客户端节点（client + RPC + PubSub）
//       并启动内部驱动线程（ssn_node_poll 事件循环；C 层 node start 不自动
//       收发，须显式 poll 才能收到应答与发布消息）；callJson 单 in-flight
//       同步调用（call_mutex_ 串行化 + 条件变量等待应答，wait_for 超时）；
//       subscribe/unsubscribe 走节点订阅通道；disconnect 停驱动线程并销毁
//       节点。应答/消息回调在驱动线程内执行（持 node->lock），实现只拷贝
//       数据并通知，绝不调用任何会加 node 锁的 API（见头文件类注释）。
#include "ssn/framework/SsnClient.hpp"

#include <chrono>
#include <cstring>
#include <exception>
#include <utility>

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

}  // namespace

SsnClient::SsnClient() = default;

SsnClient::~SsnClient() {
    disconnect();
}

bool SsnClient::connect(const std::string& peer_address, uint64_t timeout_ms) {
    // C 层节点连接（connect_to_peer）同步超时固定为 3 秒，timeout_ms 暂留作扩展
    (void)timeout_ms;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (connected_) {
            LOG_WARN("SsnClient: 已连接，重复 connect 被拒绝: %s", peer_address.c_str());
            return false;
        }
    }

    // 构造客户端节点：client + RPC + PubSub 能力
    ssn_node_config_t cfg = {};
    std::strncpy(cfg.node_type, "client", sizeof(cfg.node_type) - 1);
    std::strncpy(cfg.node_name, "SsnClient", sizeof(cfg.node_name) - 1);
    cfg.capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_RPC | SSN_NODE_CAP_PUBSUB;
    cfg.idle_timeout_sec = 0;   // 禁用 idle 断连，避免长连接被服务端断开

    ssn_node_t* node = ssn_node_create(&cfg);
    if (!node) {
        LOG_ERROR("SsnClient: 节点创建失败");
        return false;
    }
    if (!ssn_node_start(node)) {
        LOG_ERROR("SsnClient: 节点启动失败");
        ssn_node_destroy(node);
        return false;
    }

    node_ = node;
    peer_ = peer_address;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        connected_ = true;
    }
    // 启动内部驱动线程（节点事件需 poll 驱动；首个 rpc_call/subscribe 之前
    // 客户端尚未创建，poll 会立即返回——该短暂空转窗口由首次调用自然结束）
    try {
        poll_thread_ = std::thread(&SsnClient::pollLoop, this);
    } catch (const std::exception& e) {
        LOG_ERROR("SsnClient: 驱动线程创建失败: %s", e.what());
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            connected_ = false;
        }
        ssn_node_stop(node_);
        ssn_node_destroy(node_);
        node_ = nullptr;
        peer_.clear();
        return false;
    }
    return true;
}

void SsnClient::disconnect() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!connected_) {
            return;   // 未连接：幂等空操作
        }
        connected_ = false;
    }
    // 先等驱动线程退出（最迟一个 poll 周期 100ms），再回收节点：
    // 驱动线程可能正持有 node->lock 执行 poll，直接 stop 会与其竞争或悬垂
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
    if (node_) {
        ssn_node_stop(node_);
        ssn_node_destroy(node_);
        node_ = nullptr;
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

    ssn_node_t* node;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!connected_ || !node_) {
            LOG_ERROR("SsnClient: 客户端未连接，无法调用: %s", url.c_str());
            return false;
        }
        node = node_;
    }

    std::string body = req.dump();
    ssn_url_ref_t u = {const_cast<char*>(url.data()), url.size()};
    ssn_data_ref_t d = {const_cast<char*>(body.data()), body.size()};

    // 清空应答状态后发起调用（应答回调在驱动线程写 reply_data_ 并通知）
    {
        std::lock_guard<std::mutex> lock(reply_mutex_);
        reply_pending_ = false;
    }
    if (ssn_node_rpc_call(node, peer_.c_str(), &u, &d, onReplyCb, this, timeout_ms) != 0) {
        LOG_ERROR("SsnClient: RPC 调用发起失败: %s", url.c_str());
        return false;
    }

    // 等待应答；C 层超时回调（hdr 为空）会把 pending 置回 false 并唤醒，
    // 谓词不满足则继续等待直至自身超时，不会把超时误判为应答
    std::unique_lock<std::mutex> reply_lock(reply_mutex_);
    if (!reply_cv_.wait_for(reply_lock, std::chrono::milliseconds(timeout_ms),
                            [this] { return reply_pending_; })) {
        LOG_ERROR("SsnClient: RPC 调用超时: %s (%llu ms)", url.c_str(),
                  static_cast<unsigned long long>(timeout_ms));
        return false;
    }
    resp = reply_data_;

    // 服务端返回框架错误（如 1001 方法不存在）→ 调用失败
    std::string err = error_message_of(resp);
    if (!err.empty()) {
        LOG_WARN("SsnClient: 方法 %s 返回错误: %s", url.c_str(), err.c_str());
        return false;
    }
    return true;
}

bool SsnClient::subscribe(const std::string& topic, MsgHandler handler, uint64_t timeout_ms) {
    if (topic.empty() || topic[0] != '/' || !handler) {
        LOG_ERROR("SsnClient: subscribe 参数非法: %s", topic.c_str());
        return false;
    }
    ssn_node_t* node;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!connected_ || !node_) {
            LOG_ERROR("SsnClient: 客户端未连接，无法订阅: %s", topic.c_str());
            return false;
        }
        node = node_;
    }

    // 先登记本地处理器（同名主题覆盖），再向服务端订阅；失败则回滚
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        subs_[topic] = std::move(handler);
    }
    ssn_url_ref_t u = {const_cast<char*>(topic.data()), topic.size()};
    if (!ssn_node_subscribe(node, peer_.c_str(), &u, onMsgCb, this, timeout_ms)) {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        subs_.erase(topic);
        LOG_ERROR("SsnClient: 订阅失败: %s", topic.c_str());
        return false;
    }
    return true;
}

bool SsnClient::unsubscribe(const std::string& topic) {
    ssn_node_t* node;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        node = node_;
    }
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        subs_.erase(topic);
    }
    if (!node) {
        LOG_WARN("SsnClient: 客户端未连接，无法退订: %s", topic.c_str());
        return false;
    }
    ssn_url_ref_t u = {const_cast<char*>(topic.data()), topic.size()};
    if (!ssn_node_unsubscribe(node, &u, 5000)) {
        LOG_WARN("SsnClient: 退订失败: %s", topic.c_str());
        return false;
    }
    return true;
}

ssn_node_t* SsnClient::node() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return node_;
}

void SsnClient::pollLoop() {
    // 节点事件驱动：C 层 node start 不自动收发，须显式 poll 才能收到应答
    // 与发布消息。connected_ 置 false 后本循环最迟一个 poll 周期（100ms）退出。
    while (true) {
        ssn_node_t* node;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!connected_) {
                break;
            }
            node = node_;
        }
        ssn_node_poll(node, 100);
    }
}

void SsnClient::onReplyCb(ssn_client_t* /*client*/, ssn_header_t* hdr,
                          ssn_data_ref_t* data, void* arg) {
    auto* self = static_cast<SsnClient*>(arg);
    self->handleReply(hdr, data);
}

void SsnClient::onMsgCb(ssn_client_t* /*client*/, ssn_url_ref_t* url,
                        ssn_data_ref_t* data, void* arg) {
    auto* self = static_cast<SsnClient*>(arg);
    self->handleMsg(url, data);
}

void SsnClient::handleReply(ssn_header_t* hdr, ssn_data_ref_t* data) {
    // 回调在驱动线程（ssn_node_poll 内）执行，期间持有 node->lock：
    // 只拷贝数据 + 通知，不得调用任何会加 node 锁的 API（见头文件类注释）。
    if (!hdr) {
        // C 层超时/清理回调（服务端无应答）：不会有应答到达。置回未决并
        // 唤醒等待方——等待方检查 pending 为 false 后继续等待直至自身超时。
        std::lock_guard<std::mutex> lock(reply_mutex_);
        reply_pending_ = false;
        reply_cv_.notify_all();
        return;
    }
    // 应答数据在回调返回后失效（C 层语义），须立即解析拷贝
    nlohmann::json parsed = nlohmann::json::object();
    if (data && data->data && data->length) {
        try {
            auto* begin = static_cast<char*>(data->data);
            parsed = nlohmann::json::parse(begin, begin + data->length);
        } catch (const std::exception& e) {
            LOG_WARN("SsnClient: 应答 JSON 解析失败: %s", e.what());
            parsed = nlohmann::json::object();
        }
    }
    std::lock_guard<std::mutex> lock(reply_mutex_);
    reply_data_ = std::move(parsed);
    reply_pending_ = true;
    reply_cv_.notify_all();
}

void SsnClient::handleMsg(ssn_url_ref_t* url, ssn_data_ref_t* data) {
    if (!url || !url->url || !url->url_len) {
        return;
    }
    const std::string topic(url->url, url->url_len);

    // 取出处理器（拷贝 std::function 后解锁再调用：用户回调中可能再次
    // subscribe/unsubscribe，若持 subs_mutex_ 调用会自锁死锁）
    MsgHandler handler;
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        auto it = subs_.find(topic);
        if (it == subs_.end()) {
            return;
        }
        handler = it->second;
    }

    nlohmann::json payload = nlohmann::json::object();
    if (data && data->data && data->length) {
        try {
            auto* begin = static_cast<char*>(data->data);
            payload = nlohmann::json::parse(begin, begin + data->length);
        } catch (const std::exception& e) {
            LOG_WARN("SsnClient: 消息 JSON 解析失败: %s", e.what());
        }
    }
    handler(topic, payload);   // 用户回调：需快速返回（见头文件锁约束）
}

}  // namespace ssn
