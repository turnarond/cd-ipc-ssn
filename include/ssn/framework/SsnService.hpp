// 文件: include/ssn/framework/SsnService.hpp
// 功能: 通信服务基类（服务端）——绑定 SSN C API：OnInit 创建服务节点并注册
//       内置端点（/urls /health /version）与用户方法，svc() 运行 poll 事件
//       循环；handleRpc 按 URL 分发 JSON 请求并应答（框架错误码 1001/1002/
//       1003）；publish 发布 PubSub 消息。Task 7 在此基础上做类型安全包装。
#ifndef SSN_FRAMEWORK_SSNSERVICE_HPP
#define SSN_FRAMEWORK_SSNSERVICE_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "ssn/framework/ServiceTask.hpp"
#include "node/ssn_node.h"   // C 头已带 extern "C" 保护，可直接包含

namespace ssn {

// 通信服务基类（服务端）：继承 ServiceTask，svc() 运行节点 poll 事件循环。
// start 后监听 listenTcp 配置的地址，RPC 请求按 URL 分发到已注册的
// JsonHandler；应答体为 JSON 对象，失败时返回
// {"error": {"code": <int>, "message": "<中文描述>"}}：
//   1001 方法不存在 / 1002 请求 JSON 解析失败 / 1003 handler 抛出异常
//   （1004 客户端超时归 Task 6 SsnClient 使用）
// 线程与锁约束：RPC 分发、连接与消息回调在节点 poll 线程（svc）内执行，
// 期间持有 C 层 node->lock——回调内不得调用任何会加 node 锁的 API
//（ssn_node_rpc_call / ssn_node_publish / ssn_node_subscribe /
// ssn_node_send_to_peer / ssn_node_get_stats / ssn_node_stop 等），否则自锁
// 死锁；同一约束适用于客户端订阅回调（SsnClient::MsgHandler）。
class SsnService : public ServiceTask {
public:
    SsnService();
    ~SsnService() override;

    // 监听配置（必须 OnInit 前调用；默认 127.0.0.1:18888）
    void listenTcp(const std::string& host, uint16_t port);

    // 方法注册（json 层；Task 7 提供类型安全 RegisterMethod 包装）
    using JsonHandler = std::function<nlohmann::json(const nlohmann::json&)>;
    bool registerJson(const std::string& url, JsonHandler handler);   // 重复注册同 URL 返回 false
    bool unregister(const std::string& url);

    // 类型安全方法注册（Task 7）：用户传入 DTO 结构体 Req/Resp（配合
    // NLOHMANN_DEFINE_TYPE_INTRUSIVE），handler 收到反序列化后的 Req，
    // 返回值自动序列化为 JSON 应答。
    // 异常路径：Req 反序列化失败（如请求体字段缺失/类型不符）由包装 lambda
    // 抛出，SsnService::handleRpc 捕获后按框架错误码 1003（handler 异常）应答。
    template <typename Req, typename Resp, typename Fn>
    bool RegisterMethod(const std::string& url, Fn&& fn) {
        return registerJson(url, [fn = std::forward<Fn>(fn)](const nlohmann::json& jreq) -> nlohmann::json {
            Req req = jreq.get<Req>();   // 反序列化失败 → 抛异常 → 框架捕获 → 1003
            return fn(req);
        });
    }

    // 发布（PubSub 主题，任意客户端可订阅）
    bool publish(const std::string& topic, const nlohmann::json& data);

    // 内置端点数据
    nlohmann::json builtinUrls() const;      // {"urls": [...]}
    nlohmann::json builtinHealth() const;    // {"status":"ok","connections":N,"messages":M}
    nlohmann::json builtinVersion() const;   // {"version":"2.3.2"}

    const std::string& listenHost() const;
    uint16_t listenPort() const;

protected:
    bool OnInit(int argc, char** argv) override;   // 创建 node、注册内置端点与用户方法、node start
    void OnShutdown() override;                    // 卸载方法、node stop/destroy
    // 事件循环：while (isRunning()) 内 ssn_node_poll(node_, 100) 之后 sleep 1ms
    // 让出锁窗口（外部线程 publish 锁饥饿修复，详见实现注释）
    int svc() override;

private:
    static void onRpcCb(ssn_server_t*, ssn_peer_id_t, ssn_header_t*, ssn_url_ref_t*, ssn_data_ref_t*, void*);
    void handleRpc(ssn_server_t* server, ssn_peer_id_t id, ssn_header_t* hdr,
                   ssn_url_ref_t* url, ssn_data_ref_t* data);
    static void onConnectCb(ssn_server_t*, ssn_peer_id_t, bool connect, void* arg);

    ssn_node_t* node_{nullptr};
    std::string listen_host_{"127.0.0.1"};
    uint16_t listen_port_{18888};
    mutable std::mutex methods_mutex_;             // mutable：builtinUrls() 等 const 访问需加锁
    std::map<std::string, JsonHandler> methods_;   // URL → handler（含内置端点）
    // 健康统计（框架侧计数）：RPC 分发在节点 poll 线程内执行，期间持有 node->lock，
    // 调用 ssn_node_get_stats 会自锁死锁，故在 connect 回调与分发路径自维护计数
    std::atomic<int> connections_{0};
    std::atomic<uint64_t> messages_{0};
};

}  // namespace ssn

#endif  // SSN_FRAMEWORK_SSNSERVICE_HPP
