/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * SsnService 通信服务基类实现
 */
// 文件: src/framework/SsnService.cpp
// 功能: SsnService 通信服务基类实现——OnInit 创建 ssn::Node、监听
//       tcp://host:port 并启动后台事件线程（svc 仅作生命周期守护等待停止
//       信号）；事件分发：req 信封按 URL 路由到方法表并回 rep 信封
//       （框架错误码 1001 方法不存在 / 1002 JSON 解析失败 / 1003 handler
//       异常），sub/unsub 信封维护订阅表并回 suback/unsuback，发布向订阅
//       者定向投递 pub 信封。
#include "ssn/framework/SsnService.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

#include "ssn/node/Node.hpp"

#include "NodeBus.hpp"
#include "util/ssn_log.h"
#include "version/ssn_version.h"

namespace ssn {

namespace {

// 框架错误码（Task 6/7 复用）：1001 方法不存在 / 1002 JSON 解析失败 / 1003 handler 异常
constexpr int kErrMethodNotFound = 1001;
constexpr int kErrJsonParse = 1002;
constexpr int kErrHandlerException = 1003;

// 内置端点保留前缀（registerJson 拒绝用户注册）
constexpr const char* kBuiltinUrls = "/urls";
constexpr const char* kBuiltinHealth = "/health";
constexpr const char* kBuiltinVersion = "/version";
// "/" 保留端点（旧 C 层兜底命令路径；信封协议下未注册 URL 由框架直接返回
// 1001，无需注册兜底方法，但保留拒绝语义以防误用）
constexpr const char* kCatchAllUrl = "/";

// 构造错误应答体 {"error": {"code": ..., "message": "..."}}
nlohmann::json make_error(int code, const char* message) {
    return {{"error", {{"code", code}, {"message", message}}}};
}

// std::string → MESSAGE 帧字节视图
ByteView to_byte_view(const std::string& bytes) {
    return ByteView{reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
}

}  // namespace

SsnService::SsnService() = default;

SsnService::~SsnService() {
    // 兜底清理：未显式 stop/destroy 时回收节点资源。
    // 先 destroy() 走正常停机（Started 状态经 OnShutdown 停守护线程），
    // 再处理 Initialized 未 start 残留的节点（此时无事件运行，直接回收）。
    destroy();
    if (node_) {
        (void)node_->stop();
        (void)node_->waitStopped();
        node_.reset();
    }
}

void SsnService::listenTcp(const std::string& host, uint16_t port) {
    listen_host_ = host;
    listen_port_ = port;
}

bool SsnService::registerJson(const std::string& url, JsonHandler handler) {
    if (url.empty() || url[0] != '/' || !handler) {
        LOG_ERROR("SsnService: registerJson 参数非法: %s", url.c_str());
        return false;
    }
    // 尾斜杠 URL（长度 > 1）拒绝（Issue #5-3 语义保留）：框架分发为精确匹配，
    // 尾斜杠 URL 语义含糊，注册后易生误解；"/" 保留端点（长度 1）不受此限制
    if (url.size() > 1 && url.back() == '/') {
        LOG_WARN("SsnService: %s 为尾斜杠 URL（框架精确匹配语义含糊），拒绝注册", url.c_str());
        return false;
    }
    // 内置端点与 "/" 保留路径，拒绝用户注册
    if (url == kCatchAllUrl || url == kBuiltinUrls || url == kBuiltinHealth || url == kBuiltinVersion) {
        LOG_WARN("SsnService: %s 为保留端点，拒绝注册", url.c_str());
        return false;
    }
    std::lock_guard<std::mutex> lock(methods_mutex_);
    if (methods_.count(url)) {
        LOG_WARN("SsnService: 方法重复注册: %s", url.c_str());
        return false;
    }
    methods_.emplace(url, std::move(handler));
    return true;
}

bool SsnService::unregister(const std::string& url) {
    // 与 registerJson 同规则（Issue #5-3）：尾斜杠 URL（长度 > 1）从未能注册，
    // 无需（也不应）退订
    if (url.size() > 1 && url.back() == '/') {
        return false;
    }
    std::lock_guard<std::mutex> lock(methods_mutex_);
    return methods_.erase(url) > 0;
}

bool SsnService::publish(const std::string& topic, const nlohmann::json& data) {
    if (!node_) {
        LOG_ERROR("SsnService: 节点未初始化，无法发布");
        return false;
    }
    // 快照订阅者后投递（尽力而为语义：无订阅者视为成功；个别投递失败仅告警，
    // 不改变 publish 返回值——与旧 C 层广播语义的返回值口径一致）
    std::vector<PeerId> targets;
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        auto it = subs_.find(topic);
        if (it == subs_.end()) {
            return true;
        }
        targets = it->second;
    }
    const std::string bytes = bus::encode_pub(topic, data.dump());
    for (const PeerId pid : targets) {
        if (!node_->send(pid, to_byte_view(bytes))) {
            LOG_WARN("SsnService: 主题 %s 投递至 peer 失败（尽力而为，继续其余订阅者）",
                     topic.c_str());
        }
    }
    return true;
}

nlohmann::json SsnService::builtinUrls() const {
    std::lock_guard<std::mutex> lock(methods_mutex_);
    nlohmann::json urls = nlohmann::json::array();
    for (const auto& kv : methods_) {
        urls.push_back(kv.first);
    }
    return {{"urls", std::move(urls)}};
}

nlohmann::json SsnService::builtinHealth() const {
    if (!node_) {
        LOG_WARN("SsnService: 节点未初始化，健康状态不可用");
        return {{"status", "error"}, {"connections", 0}, {"messages", 0}};
    }
    // 读数取自框架侧原子计数（见头文件说明：分发在 Node 后台事件线程执行，
    // 连接/消息计数由事件维护，健康查询不再触达节点内部状态）。
    // svc 线程异常退出（事件循环崩溃）后健康状态降级为 degraded（I4）
    return {{"status", failed() ? "degraded" : "ok"},
            {"connections", connections_.load()},
            {"messages", messages_.load()}};
}

nlohmann::json SsnService::builtinVersion() const {
    return {{"version", ssn_version_get_string()}};
}

const std::string& SsnService::listenHost() const {
    return listen_host_;
}

uint16_t SsnService::listenPort() const {
    return listen_port_;
}

bool SsnService::OnInit(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // 清理残留节点：destroy() 从 Initialized 态直接归位 Created（不调用
    // OnShutdown，见 ServiceBase::destroy），重复 initialize 时旧节点仍存活，
    // 直接重建会泄漏（此态下 svc 线程从未运行，可安全直接回收）
    if (node_) {
        (void)node_->stop();
        (void)node_->waitStopped();
        node_.reset();
    }

    // 创建节点：监听必须处于 Created 态（先 listen 再启动后台线程）
    auto created = Node::create(NodeConfig{});
    if (!created) {
        LOG_ERROR("SsnService: 节点创建失败");
        return false;
    }
    auto node = std::make_unique<Node>(std::move(created.value()));
    const std::string address = "tcp://" + listen_host_ + ":" + std::to_string(listen_port_);
    if (!node->listen(ListenAddress{address.c_str()})) {
        // 监听失败（如 EADDRINUSE）：同步返回失败，initialize 回滚（Issue #5-6）
        LOG_ERROR("SsnService: 监听失败: %s", address.c_str());
        node.reset();
        return false;
    }
    node->setEventHandler([this](const NodeEvent& event) { handleEvent(event); });
    if (!node->startBackground()) {
        LOG_ERROR("SsnService: 节点后台线程启动失败");
        node.reset();
        return false;
    }
    node_ = std::move(node);

    // 内置端点以普通 handler 注入方法表（同名 URL 已被 registerJson 拒绝，不会冲突）
    {
        std::lock_guard<std::mutex> lock(methods_mutex_);
        methods_[kBuiltinUrls] = [this](const nlohmann::json&) -> nlohmann::json { return builtinUrls(); };
        methods_[kBuiltinHealth] = [this](const nlohmann::json&) -> nlohmann::json { return builtinHealth(); };
        methods_[kBuiltinVersion] = [this](const nlohmann::json&) -> nlohmann::json { return builtinVersion(); };
    }
    return true;
}

void SsnService::OnShutdown() {
    if (!node_) {
        return;
    }
    // 先停守护线程再回收节点：svc 在 isRunning() 翻转后退出
    //（stopImpl 的 requestShutdown/wait 随后调用时均为幂等空操作）
    requestShutdown();
    wait();

    // 停后台事件线程并销毁节点
    (void)node_->stop();
    (void)node_->waitStopped();
    node_.reset();
}

int SsnService::svc() {
    // 事件收发由 Node 后台线程自驱动（OnInit 的 startBackground）；svc 仅作
    // 生命周期守护线程等待停止信号（isRunning() 由 stopImpl 翻转），使
    // ServiceTask 的线程模型（failed()/degraded 健康降级等）保持不变
    while (isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}

void SsnService::handleEvent(const NodeEvent& event) {
    switch (event.type) {
    case NodeEventType::PeerConnected:
        ++connections_;   // 健康统计：连接数由连接事件维护
        break;
    case NodeEventType::PeerDisconnected: {
        if (connections_.load() > 0) {
            --connections_;
        }
        // 清除该 peer 的全部订阅（断连后不再向其投递发布消息）
        {
            std::lock_guard<std::mutex> lock(subs_mutex_);
            for (auto& kv : subs_) {
                auto& list = kv.second;
                list.erase(std::remove(list.begin(), list.end(), event.peer), list.end());
            }
        }
        break;
    }
    case NodeEventType::MessageReceived: {
        if (event.message.empty()) {
            break;
        }
        std::string text(reinterpret_cast<const char*>(event.message.data()),
                         event.message.size());
        bus::Envelope env;
        if (!bus::decode(text, env)) {
            LOG_WARN("SsnService: 收到非法信封，丢弃");
            break;
        }
        dispatchEnvelope(event.peer, env);
        break;
    }
    default:
        break;   // Error/Backpressure 事件由 Node 内部处理，框架不消费
    }
}

void SsnService::dispatchEnvelope(PeerId peer, const bus::Envelope& env) {
    switch (env.kind) {
    case bus::Kind::Req:
        handleReq(peer, env);
        break;
    case bus::Kind::Sub: {
        {
            std::lock_guard<std::mutex> lock(subs_mutex_);
            auto& list = subs_[env.url];
            if (std::find(list.begin(), list.end(), peer) == list.end()) {
                list.push_back(peer);   // 重复订阅同主题幂等
            }
        }
        sendEnvelope(peer, bus::encode_suback(env.url));
        break;
    }
    case bus::Kind::Unsub: {
        {
            std::lock_guard<std::mutex> lock(subs_mutex_);
            auto it = subs_.find(env.url);
            if (it != subs_.end()) {
                auto& list = it->second;
                list.erase(std::remove(list.begin(), list.end(), peer), list.end());
            }
        }
        sendEnvelope(peer, bus::encode_unsuback(env.url));
        break;
    }
    default:
        LOG_WARN("SsnService: 忽略不适用的信封种类（peer 仅允许 req/sub/unsub）");
        break;
    }
}

void SsnService::handleReq(PeerId peer, const bus::Envelope& env) {
    ++messages_;   // 健康统计：累计分发请求数（保持旧口径：仅计 RPC 请求）
    const std::string& key = env.url;

    // 查方法表（含内置端点）
    JsonHandler handler;
    {
        std::lock_guard<std::mutex> lock(methods_mutex_);
        auto it = methods_.find(key);
        if (it != methods_.end()) {
            handler = it->second;
        }
    }
    if (!handler) {
        LOG_WARN("SsnService: 方法不存在: %s", key.c_str());
        replyBusError(peer, env.seq, kErrMethodNotFound, "方法不存在");
        return;
    }

    // 请求体 JSON 解析（空体视为空对象 {}）
    nlohmann::json req = nlohmann::json::object();
    if (!env.body.empty()) {
        try {
            req = nlohmann::json::parse(env.body);
        } catch (const std::exception& e) {
            LOG_WARN("SsnService: 请求 JSON 解析失败: %s", e.what());
            replyBusError(peer, env.seq, kErrJsonParse, "请求 JSON 解析失败");
            return;
        }
    }

    // 调用 handler（异常归为框架错误码 1003）
    nlohmann::json result;
    try {
        result = handler(req);
    } catch (const std::exception& e) {
        LOG_ERROR("SsnService: 方法 %s 处理异常: %s", key.c_str(), e.what());
        replyBusError(peer, env.seq, kErrHandlerException, "handler 抛出异常");
        return;
    } catch (...) {
        LOG_ERROR("SsnService: 方法 %s 抛出未知异常", key.c_str());
        replyBusError(peer, env.seq, kErrHandlerException, "handler 抛出未知异常");
        return;
    }

    // 成功应答
    sendEnvelope(peer, bus::encode_rep(env.seq, true, result.dump()));
}

void SsnService::sendEnvelope(PeerId peer, const std::string& bytes) {
    // 分发在 Node 后台事件线程执行，node_ 在 stop+waitStopped 前保持存活
    //（OnShutdown 先停守护线程再停事件线程，最后才 reset），直接使用安全
    if (!node_) {
        LOG_ERROR("SsnService: 节点未初始化，无法应答");
        return;
    }
    if (!node_->send(peer, to_byte_view(bytes))) {
        LOG_WARN("SsnService: 信封投递失败（peer 可能已断开）");
    }
}

void SsnService::replyBusError(PeerId peer, std::uint64_t seq, int code, const char* message) {
    sendEnvelope(peer, bus::encode_rep(seq, false, make_error(code, message).dump()));
}

}  // namespace ssn
