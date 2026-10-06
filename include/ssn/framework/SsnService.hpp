/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 通信服务基类（服务端：方法注册/分发/内置端点/发布）
 */
// 文件: include/ssn/framework/SsnService.hpp
// 功能: 通信服务基类（服务端）——组合 ssn::Node：OnInit 创建节点、监听
//       listenTcp 配置的地址并启动后台事件线程，svc() 作为生命周期守护
//       线程等待停止信号；RPC 与 PubSub 语义以 JSON 信封协议承载于 Node
//       的 MESSAGE 帧（协议见 src/framework/NodeBus.hpp）。请求按信封
//       url 分发到已注册的 JsonHandler 并以 rep 信封应答（框架错误码
//       1001/1002/1003）；publish 向订阅该主题的 peer 定向投递 pub 信封。
//       Task 7 在此基础上做类型安全包装。
#ifndef SSN_FRAMEWORK_SSNSERVICE_HPP
#define SSN_FRAMEWORK_SSNSERVICE_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ssn/framework/ServiceTask.hpp"
#include "ssn/node/Types.hpp"   // PeerId（订阅表元素类型；Node 于 cpp 内完整使用）

namespace ssn {

class Node;
namespace bus { struct Envelope; }   // 框架内 JSON 信封（src/framework/NodeBus.hpp，私有实现）

// 通信服务基类（服务端）：继承 ServiceTask，svc() 为生命周期守护线程
// （事件收发由 Node 后台线程自驱动）。start 后监听 listenTcp 配置的地址，
// RPC 请求按信封 url 分发到已注册的 JsonHandler；应答体为 JSON 对象，
// 失败时返回 {"error": {"code": <int>, "message": "<中文描述>"}}：
//   1001 方法不存在 / 1002 请求 JSON 解析失败 / 1003 handler 抛出异常
//   （1004 客户端超时归 Task 6 SsnClient 使用）
// 线程与锁约束：请求分发、订阅握手与发布投递在 Node 后台事件线程执行
// （期间不持有任何 Node 内部锁）——handler 内不得阻塞等待自身应答
//（如经 SsnClient 调回本服务，事件线程被阻塞后应答无法投递，必然超时），
// 需快速返回；publish 可在任意线程调用（用户线程或事件线程）。
// 生命周期约束（Issue #5-4）：stop()/destroy() 后不得调用 publish/unregister
//（node_ 已销毁；publish 内部对 node_ 空指针有守卫但非原子——与延迟调用
// 存在 TOCTOU 窗口，属文档约束而非代码保证）。
class SSN_FRAMEWORK_API SsnService : public ServiceTask {
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
    // 抛出，SsnService 分发捕获后按框架错误码 1003（handler 异常）应答。
    template <typename Req, typename Resp, typename Fn>
    bool RegisterMethod(const std::string& url, Fn&& fn) {
        // Resp 模板参数参与编译期类型约束：强制 handler 返回值可转换为 Resp，
        // 避免「返回类型与 DTO 不匹配仍静默编译」破坏类型安全层契约
        return registerJson(url, [fn = std::forward<Fn>(fn)](const nlohmann::json& jreq) -> nlohmann::json {
            Req req = jreq.get<Req>();   // 反序列化失败 → 抛异常 → 框架捕获 → 1003
            static_assert(std::is_convertible_v<decltype(fn(req)), Resp>,
                          "RegisterMethod: handler 返回值必须可转换为 Resp");
            Resp resp = fn(req);         // 显式转换：类型不匹配在编译期报错
            return resp;
        });
    }

    // 发布（PubSub 主题，向订阅者定向投递；无订阅者视为成功）
    bool publish(const std::string& topic, const nlohmann::json& data);

    // 内置端点数据
    nlohmann::json builtinUrls() const;      // {"urls": [...]}
    // {"status":"ok"|"degraded","connections":N,"messages":M}——svc 线程异常退出
    // （failed()==true）时状态为 "degraded"（稳定性加固 I4），节点未初始化时为 "error"
    nlohmann::json builtinHealth() const;
    nlohmann::json builtinVersion() const;   // {"version":"X.Y.Z"}（当前 SSN_VERSION_STRING）

    const std::string& listenHost() const;
    uint16_t listenPort() const;

protected:
    bool OnInit(int argc, char** argv) override;   // 创建 node、监听、注册内置端点、启动后台线程
    void OnShutdown() override;                    // node stop/destroy
    // 生命周期守护循环：while (isRunning()) 内 sleep 等待停止信号
    //（事件收发由 Node 后台线程自驱动，详见实现注释）
    int svc() override;

private:
    void handleEvent(const NodeEvent& event);          // Node 事件分发（后台事件线程）
    void dispatchEnvelope(PeerId peer, const bus::Envelope& env);
    void handleReq(PeerId peer, const bus::Envelope& env);
    void sendEnvelope(PeerId peer, const std::string& bytes);   // rep/suback/pub 投递
    void replyBusError(PeerId peer, std::uint64_t seq, int code, const char* message);

    std::unique_ptr<Node> node_;                   // 底层多 Peer 节点（MESSAGE 帧承载信封）
    std::string listen_host_{"127.0.0.1"};
    uint16_t listen_port_{18888};
    mutable std::mutex methods_mutex_;             // mutable：builtinUrls() 等 const 访问需加锁
    std::map<std::string, JsonHandler> methods_;   // URL → handler（含内置端点）
    // 订阅表：topic → 订阅 peer 列表（事件线程写、publish 任意线程读，互斥保护）
    std::mutex subs_mutex_;
    std::map<std::string, std::vector<PeerId>> subs_;
    // 健康统计（框架侧计数）：连接数由 PeerConnected/Disconnected 事件维护，
    // messages 仅累计 RPC 请求分发（保持旧口径：messages 为请求数而非信封数）
    std::atomic<int> connections_{0};
    std::atomic<uint64_t> messages_{0};
};

}  // namespace ssn

#endif  // SSN_FRAMEWORK_SSNSERVICE_HPP
