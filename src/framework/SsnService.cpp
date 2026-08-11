// 文件: src/framework/SsnService.cpp
// 功能: SsnService 通信服务基类实现——OnInit 创建服务节点并注册方法
//       （内置端点 /urls /health /version + 用户方法 + "/" 兜底命令），
//       svc() 运行 poll 事件循环；handleRpc 分发 JSON 请求并应答
//       （框架错误码 1001 方法不存在 / 1002 JSON 解析失败 / 1003 handler 异常）；
//       publish 走节点发布通道。
#include "ssn/framework/SsnService.hpp"

#include <cstring>
#include <exception>
#include <vector>

#include "util/ssn_log.h"
#include "version/ssn_version.h"

namespace ssn {

namespace {

// 框架错误码（Task 6/7 复用）：1001 方法不存在 / 1002 JSON 解析失败 / 1003 handler 异常
constexpr int kErrMethodNotFound = 1001;
constexpr int kErrJsonParse = 1002;
constexpr int kErrHandlerException = 1003;
// 应答头部 status：0 成功，非 0 失败（C 层语义）
constexpr uint32_t kStatusOk = 0;
constexpr uint32_t kStatusFail = 1;

// 内置端点保留前缀（registerJson 拒绝用户注册）
constexpr const char* kBuiltinUrls = "/urls";
constexpr const char* kBuiltinHealth = "/health";
constexpr const char* kBuiltinVersion = "/version";
// "/" 为 C 层默认命令（兜底：未注册 URL 也进入 handleRpc，返回 1001），同样保留
constexpr const char* kCatchAllUrl = "/";

// 构造错误应答体 {"error": {"code": ..., "message": "..."}}
nlohmann::json make_error(int code, const char* message) {
    return {{"error", {{"code", code}, {"message", message}}}};
}

// 从方法表收集全部 URL（调用方持有 methods_mutex_ 语义：先拷贝后释放锁）
std::vector<std::string> collect_urls(const std::map<std::string, SsnService::JsonHandler>& methods) {
    std::vector<std::string> urls;
    urls.reserve(methods.size() + 1);
    for (const auto& kv : methods) {
        urls.push_back(kv.first);
    }
    return urls;
}

}  // namespace

SsnService::SsnService() = default;

SsnService::~SsnService() {
    // 兜底清理：未显式 stop/destroy 时回收节点资源。
    // 先 destroy() 走正常停机（Started 状态经 OnShutdown 停线程），
    // 再处理 Initialized 未 start 残留的节点（此时无线程运行，直接回收）。
    destroy();
    if (node_) {
        ssn_node_stop(node_);
        ssn_node_destroy(node_);
        node_ = nullptr;
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
    // 内置端点与 "/" 兜底命令为保留路径，拒绝用户注册
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
    std::lock_guard<std::mutex> lock(methods_mutex_);
    return methods_.erase(url) > 0;
}

bool SsnService::publish(const std::string& topic, const nlohmann::json& data) {
    if (!node_) {
        LOG_ERROR("SsnService: 节点未初始化，无法发布");
        return false;
    }
    std::string body = data.dump();
    ssn_url_ref_t url = {const_cast<char*>(topic.data()), topic.size()};
    ssn_data_ref_t d = {const_cast<char*>(body.data()), body.size()};
    if (!ssn_node_publish(node_, &url, &d)) {
        LOG_ERROR("SsnService: 发布主题失败: %s", topic.c_str());
        return false;
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
    // 读数取自框架侧原子计数（见头文件说明：RPC 分发在 node->lock 内执行，
    // 此处不得再调用 ssn_node_get_stats，否则自锁死锁）
    return {{"status", "ok"},
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
        ssn_node_destroy(node_);
        node_ = nullptr;
    }

    // 构造服务节点：服务端 + RPC + PubSub，监听 TCP host:port
    ssn_node_config_t cfg = {};
    std::strncpy(cfg.node_type, "server", sizeof(cfg.node_type) - 1);
    if (!name_.empty()) {
        std::strncpy(cfg.node_name, name_.c_str(), sizeof(cfg.node_name) - 1);
    } else {
        std::strncpy(cfg.node_name, "SsnService", sizeof(cfg.node_name) - 1);
    }
    std::strncpy(cfg.listen_address, listen_host_.c_str(), sizeof(cfg.listen_address) - 1);
    cfg.listen_port = listen_port_;
    cfg.capabilities = SSN_NODE_CAP_SERVER | SSN_NODE_CAP_RPC | SSN_NODE_CAP_PUBSUB;
    cfg.idle_timeout_sec = 0;   // 禁用 idle 断连，避免长测/长连接被服务端断开

    node_ = ssn_node_create(&cfg);
    if (!node_) {
        LOG_ERROR("SsnService: 节点创建失败");
        return false;
    }
    if (!ssn_node_start(node_)) {
        LOG_ERROR("SsnService: 节点启动失败");
        ssn_node_destroy(node_);
        node_ = nullptr;
        return false;
    }

    // 注册连接事件回调，维护健康统计（原子计数，不触碰 C 层锁）
    ssn_node_set_connect_handler(node_, onConnectCb, this);

    // 内置端点以普通 handler 注入方法表（同名 URL 已被 registerJson 拒绝，不会冲突）
    {
        std::lock_guard<std::mutex> lock(methods_mutex_);
        methods_[kBuiltinUrls] = [this](const nlohmann::json&) -> nlohmann::json { return builtinUrls(); };
        methods_[kBuiltinHealth] = [this](const nlohmann::json&) -> nlohmann::json { return builtinHealth(); };
        methods_[kBuiltinVersion] = [this](const nlohmann::json&) -> nlohmann::json { return builtinVersion(); };
    }

    // 逐个注册到 C 层（含内置端点与 "/" 兜底）；url_len 必须为 strlen（仓库已知语义）
    std::vector<std::string> urls;
    {
        std::lock_guard<std::mutex> lock(methods_mutex_);
        urls = collect_urls(methods_);
    }
    urls.push_back(kCatchAllUrl);
    bool ok = true;
    for (const auto& u : urls) {
        ssn_url_ref_t ref = {const_cast<char*>(u.data()), u.size()};
        if (!ssn_node_add_rpc_method(node_, &ref, onRpcCb, this)) {
            LOG_WARN("SsnService: 方法注册失败: %s", u.c_str());
            ok = false;
        }
    }
    return ok;
}

void SsnService::OnShutdown() {
    if (!node_) {
        return;
    }
    // 先停 svc 线程再回收节点：svc 持有 node_ 并轮询，直接销毁会悬垂。
    // （stopImpl 的 requestShutdown/wait 随后调用时均为幂等空操作）
    requestShutdown();
    wait();

    // 卸载方法（含 "/" 兜底命令），停止并销毁节点
    std::vector<std::string> urls;
    {
        std::lock_guard<std::mutex> lock(methods_mutex_);
        urls = collect_urls(methods_);
    }
    urls.push_back(kCatchAllUrl);
    for (const auto& u : urls) {
        ssn_url_ref_t ref = {const_cast<char*>(u.data()), u.size()};
        ssn_node_remove_rpc_method(node_, &ref);
    }
    ssn_node_stop(node_);
    ssn_node_destroy(node_);
    node_ = nullptr;
}

int SsnService::svc() {
    // 事件循环：poll 驱动服务节点（含服务器 accept 与 RPC 分发）
    while (isRunning()) {
        if (!node_) {
            break;   // 节点已清理，无事件源，直接退出
        }
        ssn_node_poll(node_, 100);
    }
    return 0;
}

void SsnService::onRpcCb(ssn_server_t* server, ssn_peer_id_t id, ssn_header_t* hdr,
                         ssn_url_ref_t* url, ssn_data_ref_t* data, void* arg) {
    auto* self = static_cast<SsnService*>(arg);
    self->handleRpc(server, id, hdr, url, data);
}

void SsnService::onConnectCb(ssn_server_t* /*server*/, ssn_peer_id_t /*id*/, bool connect, void* arg) {
    auto* self = static_cast<SsnService*>(arg);
    if (connect) {
        ++self->connections_;
    } else if (self->connections_.load() > 0) {
        --self->connections_;
    }
}

void SsnService::handleRpc(ssn_server_t* server, ssn_peer_id_t id, ssn_header_t* hdr,
                           ssn_url_ref_t* url, ssn_data_ref_t* data) {
    if (!server || !hdr || !url) {
        LOG_ERROR("SsnService: handleRpc 参数非法");
        return;
    }
    ++messages_;   // 健康统计：累计分发请求数
    const uint16_t seqno = ssn_get_seqno(hdr);

    // 查方法表（含内置端点）
    std::string key;
    if (url->url && url->url_len) {
        key.assign(url->url, url->url_len);
    }
    JsonHandler handler;
    {
        std::lock_guard<std::mutex> lock(methods_mutex_);
        auto it = methods_.find(key);
        if (it != methods_.end()) {
            handler = it->second;
        }
    }
    if (!handler) {
        // 方法不存在（含未注册 URL 经 "/" 兜底进入此处）
        LOG_WARN("SsnService: 方法不存在: %s", key.c_str());
        nlohmann::json err = make_error(kErrMethodNotFound, "方法不存在");
        std::string body = err.dump();
        ssn_data_ref_t resp = {const_cast<char*>(body.data()), body.size()};
        ssn_server_response(server, id, kStatusFail, seqno, &resp);
        return;
    }

    // 请求体 JSON 解析（无请求体视为空对象 {}）
    nlohmann::json req = nlohmann::json::object();
    if (data && data->data && data->length) {
        try {
            auto* begin = static_cast<char*>(data->data);
            req = nlohmann::json::parse(begin, begin + data->length);
        } catch (const std::exception& e) {
            LOG_WARN("SsnService: 请求 JSON 解析失败: %s", e.what());
            nlohmann::json err = make_error(kErrJsonParse, "请求 JSON 解析失败");
            std::string body = err.dump();
            ssn_data_ref_t resp = {const_cast<char*>(body.data()), body.size()};
            ssn_server_response(server, id, kStatusFail, seqno, &resp);
            return;
        }
    }

    // 调用 handler（异常归为框架错误码 1003）
    nlohmann::json result;
    try {
        result = handler(req);
    } catch (const std::exception& e) {
        LOG_ERROR("SsnService: 方法 %s 处理异常: %s", key.c_str(), e.what());
        nlohmann::json err = make_error(kErrHandlerException, "handler 抛出异常");
        std::string body = err.dump();
        ssn_data_ref_t resp = {const_cast<char*>(body.data()), body.size()};
        ssn_server_response(server, id, kStatusFail, seqno, &resp);
        return;
    } catch (...) {
        LOG_ERROR("SsnService: 方法 %s 抛出未知异常", key.c_str());
        nlohmann::json err = make_error(kErrHandlerException, "handler 抛出未知异常");
        std::string body = err.dump();
        ssn_data_ref_t resp = {const_cast<char*>(body.data()), body.size()};
        ssn_server_response(server, id, kStatusFail, seqno, &resp);
        return;
    }

    // 成功应答
    std::string body = result.dump();
    ssn_data_ref_t resp = {const_cast<char*>(body.data()), body.size()};
    ssn_server_response(server, id, kStatusOk, seqno, &resp);
}

}  // namespace ssn
