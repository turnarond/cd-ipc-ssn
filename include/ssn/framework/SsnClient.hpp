/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 通信客户端（同步调用/订阅/连接管理）
 */
// 文件: include/ssn/framework/SsnClient.hpp
// 功能: 通信客户端——连接管理 + 同步 callJson + PubSub 订阅。封装 SSN C 层
//       node（client + RPC + PubSub 能力）：connect 创建并启动节点与内部驱动
//       线程（C 层 node start 不自动收发，事件需 ssn_node_poll 驱动）；
//       callJson 为单 in-flight 同步调用（内部互斥串行化 + 条件变量等待应答，
//       超时或服务端错误返回 false）；订阅回调在内部驱动线程执行。
//       Task 7 在此基础上做类型安全包装。
#ifndef SSN_FRAMEWORK_SSNCLIENT_HPP
#define SSN_FRAMEWORK_SSNCLIENT_HPP

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "node/ssn_node.h"   // C 头已带 extern "C" 保护，可直接包含

namespace ssn {

// 通信客户端：同步 JSON-RPC 调用 + PubSub 订阅 + 连接管理。
// 线程模型：connect 后内部驱动线程轮询节点（ssn_node_poll，100ms 周期），
// 应答与订阅消息回调在该驱动线程内执行（期间持有 C 层 node->lock）——
// 回调内不得调用任何会加 node 锁的 API（ssn_node_rpc_call / ssn_node_publish /
// ssn_node_subscribe / ssn_node_unsubscribe / ssn_node_send_to_peer /
// ssn_node_get_stats / ssn_node_stop 等），否则自锁死锁；也不得在回调中
// 调用本客户端的 callJson（驱动线程自身被阻塞，应答永远不会被接收，必然超时），
// 且不得调用 disconnect。回调只允许拷贝数据 / 设置标志 / 通知，并需快速返回。
// callJson 为单 in-flight 同步调用：同一 client 的并发调用被 call_mutex_
// 串行化，后到者排队等待；超时返回 false。
class SSN_FRAMEWORK_API SsnClient {
public:
    SsnClient();
    ~SsnClient();
    SsnClient(const SsnClient&) = delete;
    SsnClient& operator=(const SsnClient&) = delete;

    // 连接：创建并启动客户端节点与内部驱动线程。已连接时重复调用返回 false。
    // peer_address 为传输层地址格式（如 tcp://127.0.0.1:18902）。
    // 注意：C 层节点连接的同步超时固定为 3 秒，timeout_ms 参数暂留作扩展。
    bool connect(const std::string& peer_address, uint64_t timeout_ms = 5000);
    // 停止驱动线程并销毁节点；未连接时为幂等空操作。
    // 并发约束（Issue #5-5）：内部已与 callJson 互斥（等待在途调用结束），
    // 但调用方应避免跨线程同时调用 disconnect 与 callJson/subscribe——
    // disconnect 会阻塞至在途调用超时返回；回调中禁止调用本方法（见类注释）
    void disconnect();
    bool connected() const;
    const std::string& peer() const;

    // 同步调用（json 层；Task 7 类型安全包装）。
    // 注意：单 in-flight——同一 client 并发 Call 串行化（内部互斥锁保护）。
    // 应答由内部驱动线程接收（见类注释的锁约束）；超时或服务端返回
    // 框架错误（应答含 error 字段，如 1001 方法不存在）时返回 false。
    // 竞态提示（Issue #5-7）：超时失败后立即发起下一次调用，理论上有极窄
    // 窗口被迟到应答覆盖（C API 无请求序号回调，不可修）；超时路径应避免紧接重试。
    bool callJson(const std::string& url, const nlohmann::json& req,
                  nlohmann::json& resp, uint64_t timeout_ms = 3000);

    // 类型安全调用（Task 7）：Req/Resp 为 DTO 结构体（配合
    // NLOHMANN_DEFINE_TYPE_INTRUSIVE 或 to_json/from_json 特化）。
    // 语义与 callJson 一致（单 in-flight 串行化、超时/框架错误返回 false）；
    // 区别在于 Resp 反序列化失败会向调用方抛异常（DTO 与应答不匹配属编程错误）。
    template <typename Req, typename Resp>
    bool Call(const std::string& url, const Req& req, Resp& resp, uint64_t timeout_ms = 3000) {
        nlohmann::json jreq = req;   // 依赖 NLOHMANN_DEFINE_TYPE_INTRUSIVE / json 转换
        nlohmann::json jresp;
        if (!callJson(url, jreq, jresp, timeout_ms)) {
            return false;
        }
        resp = jresp.get<Resp>();
        return true;
    }

    // PubSub 订阅（回调在 SSN 内部线程执行，需快速返回）
    // 锁约束：回调执行期间持有 node->lock，不得调用任何会加 node 锁的 API
    //（rpc_call / publish / subscribe / unsubscribe / send_to_peer / get_stats 等），
    // 且不得在回调中阻塞等待本客户端的 callJson（单 in-flight 串行化）。
    // 稳定性加固：回调抛出的异常由框架捕获并丢弃该消息（不影响进程与后续消息）；
    // subscribe/unsubscribe 内部与 disconnect 同锁（call_mutex_），并发调用被
    // 串行化（disconnect 会等待在途订阅/退订完成，见 disconnect 并发约束注释）。
    using MsgHandler = std::function<void(const std::string& topic, const nlohmann::json& data)>;
    bool subscribe(const std::string& topic, MsgHandler handler, uint64_t timeout_ms = 5000);
    bool unsubscribe(const std::string& topic);

    ssn_node_t* node();   // 高级用户访问底层节点

private:
    static void onReplyCb(ssn_client_t*, ssn_header_t*, ssn_data_ref_t*, void* arg);
    static void onMsgCb(ssn_client_t*, ssn_url_ref_t*, ssn_data_ref_t*, void* arg);
    void handleReply(ssn_header_t* hdr, ssn_data_ref_t* data);
    void handleMsg(ssn_url_ref_t* url, ssn_data_ref_t* data);
    void pollLoop();      // 驱动线程：while (connected_) ssn_node_poll(node_, 100)

    ssn_node_t* node_{nullptr};
    std::string peer_;
    bool connected_{false};
    std::mutex call_mutex_;                    // 单 in-flight 串行化
    mutable std::mutex state_mutex_;           // 保护 connected_/node_（驱动线程共享）
    bool reply_pending_{false};
    nlohmann::json reply_data_;
    std::condition_variable reply_cv_;         // 应答到达通知
    // 保护 reply_pending_/reply_data_/reply_cv_。独立互斥锁而非复用 call_mutex_：
    // 应答回调在驱动线程（持 node->lock）内执行，若其加锁 call_mutex_ 而调用线程
    // 持 call_mutex_ 等待 node->lock（rpc_call 发送路径），将形成 ABBA 死锁
    std::mutex reply_mutex_;
    std::mutex subs_mutex_;
    std::map<std::string, MsgHandler> subs_;   // topic → handler
    std::thread poll_thread_;                  // 内部驱动线程（节点事件 poll）
};

}  // namespace ssn

#endif  // SSN_FRAMEWORK_SSNCLIENT_HPP
