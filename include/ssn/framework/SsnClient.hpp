/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 通信客户端（同步调用/订阅/连接管理）
 */
// 文件: include/ssn/framework/SsnClient.hpp
// 功能: 通信客户端——连接管理 + 同步 callJson + PubSub 订阅。组合 ssn::Node
//       （connect 建立连接并启动后台事件线程，收发由 Node 自驱动）；
//       callJson 为单 in-flight 同步调用（内部互斥串行化 + 条件变量按请求
//       序号等待应答，超时或服务端错误返回 false）；订阅回调在 Node 后台
//       事件线程执行。框架 RPC/PubSub 语义以 JSON 信封协议承载于 Node 的
//       MESSAGE 帧（协议见 src/framework/NodeBus.hpp）。Task 7 在此基础上
//       做类型安全包装。
#ifndef SSN_FRAMEWORK_SSNCLIENT_HPP
#define SSN_FRAMEWORK_SSNCLIENT_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "ssn/node/Types.hpp"   // ssn::Node/PeerId 前向所需的类型（Node 于 cpp 内完整使用）

namespace ssn {

class Node;

// 通信客户端：同步 JSON-RPC 调用 + PubSub 订阅 + 连接管理。
// 线程模型：connect 后由 Node 后台线程驱动事件收发；应答与订阅消息回调在
// 该事件线程内执行（期间不持有任何 Node 内部锁）——回调内不得调用本客户端
// 的 callJson/subscribe（阻塞事件线程，应答/确认将永远无法被接收，必然
// 超时），也不得调用 disconnect。回调只允许拷贝数据 / 设置标志 / 通知，
// 并需快速返回。
// callJson 为单 in-flight 同步调用：同一 client 的并发调用被 call_mutex_
// 串行化，后到者排队等待；超时返回 false。每个请求携带自增序号，应答按
// 序号匹配——超时后到达的迟到应答因序号不符被丢弃（旧 C 层「迟到应答
// 覆盖新调用」竞态，即 Issue #5-7，已根治）。
class SSN_FRAMEWORK_API SsnClient {
public:
    SsnClient();
    ~SsnClient();
    SsnClient(const SsnClient&) = delete;
    SsnClient& operator=(const SsnClient&) = delete;

    // 连接：创建节点、监听并连接到 peer_address（传输层地址格式，如
    // tcp://127.0.0.1:18902），随后启动后台事件线程。连接同步等待至多
    // timeout_ms。已连接时重复调用返回 false。
    bool connect(const std::string& peer_address, uint64_t timeout_ms = 5000);
    // 停止后台事件线程并销毁节点；未连接时为幂等空操作。
    // 并发约束（Issue #5-5）：内部已与 callJson 互斥（等待在途调用结束），
    // 但调用方应避免跨线程同时调用 disconnect 与 callJson/subscribe——
    // disconnect 会阻塞至在途调用超时返回；回调中禁止调用本方法（见类注释）
    void disconnect();
    bool connected() const;
    const std::string& peer() const;

    // 同步调用（json 层；Task 7 类型安全包装）。
    // 注意：单 in-flight——同一 client 并发 Call 串行化（内部互斥锁保护）。
    // 应答由 Node 后台事件线程接收（见类注释的回调约束）；超时或服务端
    // 返回框架错误（应答含 error 字段，如 1001 方法不存在）时返回 false。
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

    // PubSub 订阅（回调在 Node 后台事件线程执行，需快速返回）
    // 锁约束：回调执行期间不持有任何内部锁，但不得在回调中阻塞等待本客户端
    // 的 callJson（单 in-flight 串行化，且事件线程被阻塞后应答无法投递）。
    // 稳定性加固：回调抛出的异常由框架捕获并丢弃该消息（不影响进程与后续消息）；
    // subscribe/unsubscribe 内部与 disconnect 同锁（call_mutex_），并发调用被
    // 串行化（disconnect 会等待在途订阅/退订完成，见 disconnect 并发约束注释）。
    using MsgHandler = std::function<void(const std::string& topic, const nlohmann::json& data)>;
    bool subscribe(const std::string& topic, MsgHandler handler, uint64_t timeout_ms = 5000);
    bool unsubscribe(const std::string& topic);

private:
    void handleEvent(const NodeEvent& event);   // Node 事件分发（后台事件线程）

    std::unique_ptr<Node> node_;               // 底层多 Peer 节点（MESSAGE 帧承载信封）
    PeerId peer_id_;                           // 服务端 peer（connect 成功后有效）
    std::string peer_;
    bool connected_{false};
    std::mutex call_mutex_;                    // 单 in-flight 串行化
    mutable std::mutex state_mutex_;           // 保护 connected_/node_/peer_id_
    std::atomic<std::uint64_t> next_seq_{1};   // 请求序号（应答按序号匹配）
    // 应答等待（单 in-flight，一次至多一个未决请求）
    bool reply_pending_{false};
    std::uint64_t reply_seq_{0};               // 已收应答的请求序号
    bool reply_ok_{false};                     // rep 信封 ok 标志
    nlohmann::json reply_data_;
    std::condition_variable reply_cv_;
    // 保护 reply_* 成员。独立互斥锁而非复用 call_mutex_：应答在 Node 后台
    // 事件线程投递，若事件线程加锁 call_mutex_ 而调用线程持 call_mutex_
    // 等待应答，将形成死锁
    std::mutex reply_mutex_;
    // 订阅表与 sub/unsub 确认等待
    std::mutex subs_mutex_;
    std::map<std::string, MsgHandler> subs_;   // topic → handler
    bool ack_pending_{false};
    std::string ack_topic_;
    std::mutex ack_mutex_;
    std::condition_variable ack_cv_;
};

}  // namespace ssn

#endif  // SSN_FRAMEWORK_SSNCLIENT_HPP
