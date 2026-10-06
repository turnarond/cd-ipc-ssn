/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 框架内 JSON 信封协议（私有实现头，不安装）
 */
// 文件: src/framework/NodeBus.hpp
// 功能: SsnService/SsnClient 迁移到 ssn::Node 后的框架内应用层协议——
//       RPC 与 PubSub 语义以 JSON 信封编码在 ssn::Node 的 MESSAGE 帧上承载。
//       私有实现（src/framework/），不进入公共头安装列表。
//
// 信封格式（k 为种类键，b 为 JSON 文本字符串——保留非法体注入能力，
// 使 1002 解析错误路径可测试，与旧 C 层原始字节流语义对齐）：
//   req:      {"k":"req","u":<url>,"s":<seq>,"b":<body 文本>}
//   rep:      {"k":"rep","s":<seq>,"ok":<bool>,"b":<body 文本>}
//             （ok=false 时 b 为 {"error":{"code":..,"message":..}} 框架错误体）
//   pub:      {"k":"pub","u":<topic>,"b":<data 文本>}
//   sub:      {"k":"sub","u":<topic>}          → 服务端回 suback
//   unsub:    {"k":"unsub","u":<topic>}        → 服务端回 unsuback
//   suback:   {"k":"suback","u":<topic>}
//   unsuback: {"k":"unsuback","u":<topic>}
//
// seq 为 uint64 客户端自增序号，应答按 seq 匹配（根治旧 Issue #5-7 迟到
// 应答覆盖新调用的竞态：超时后的迟到 rep 因序号不符被丢弃）。
#ifndef SSN_FRAMEWORK_NODEBUS_HPP
#define SSN_FRAMEWORK_NODEBUS_HPP

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace ssn {
namespace bus {

// 信封种类
enum class Kind {
    Req,       // 客户端 → 服务端：RPC 请求
    Rep,       // 服务端 → 客户端：RPC 应答
    Pub,       // 服务端 → 客户端：PubSub 发布
    Sub,       // 客户端 → 服务端：订阅
    Unsub,     // 客户端 → 服务端：退订
    SubAck,    // 服务端 → 客户端：订阅确认
    UnsubAck,  // 服务端 → 客户端：退订确认
    Invalid    // 解码失败/未知种类
};

// 解码后的信封（body/url 为拷贝，跨出回调安全）
struct Envelope {
    Kind kind{Kind::Invalid};
    std::string url;         // req 的方法 URL / pub/sub/unsub 的主题
    std::uint64_t seq{0};    // req/rep 的请求序号
    bool ok{false};          // rep 的处理成功标志
    std::string body;        // req/rep/pub 的 JSON 文本体
};

// —— 编码 ——
std::string encode_req(const std::string& url, std::uint64_t seq, const std::string& body);
std::string encode_rep(std::uint64_t seq, bool ok, const std::string& body);
std::string encode_pub(const std::string& topic, const std::string& body);
std::string encode_sub(const std::string& topic);
std::string encode_unsub(const std::string& topic);
std::string encode_suback(const std::string& topic);
std::string encode_unsuback(const std::string& topic);

// —— 解码 ——
// 文本 → 信封；非 JSON/非对象/种类未知/必填字段缺失返回 false（out 不变）
bool decode(const std::string& text, Envelope& out);

}  // namespace bus
}  // namespace ssn

#endif  // SSN_FRAMEWORK_NODEBUS_HPP
