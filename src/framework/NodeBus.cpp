/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 框架内 JSON 信封协议实现
 */
// 文件: src/framework/NodeBus.cpp
// 功能: JSON 信封编解码（协议格式见 NodeBus.hpp 头注释）。
//       解码对必填字段做类型校验：k 必为已知种类字符串，req 必含字符串 u
//       与数值 s，rep 必含数值 s 与布尔 ok——缺一即整体判非法（调用方丢弃），
//       避免半信封进入分发逻辑。
#include "NodeBus.hpp"

namespace ssn {
namespace bus {

namespace {

const char* kind_name(Kind kind) {
    switch (kind) {
    case Kind::Req:      return "req";
    case Kind::Rep:      return "rep";
    case Kind::Pub:      return "pub";
    case Kind::Sub:      return "sub";
    case Kind::Unsub:    return "unsub";
    case Kind::SubAck:   return "suback";
    case Kind::UnsubAck: return "unsuback";
    case Kind::Invalid:  break;
    }
    return "";
}

// 信封装配（k 必填；其余字段按种类附加）
std::string encode(Kind kind, const std::string& url, std::uint64_t seq,
                   bool ok, const std::string& body) {
    nlohmann::json env = {{"k", kind_name(kind)}};
    if (!url.empty()) { env["u"] = url; }
    if (kind == Kind::Req || kind == Kind::Rep) { env["s"] = seq; }
    if (kind == Kind::Rep) { env["ok"] = ok; }
    if (!body.empty() || kind == Kind::Req || kind == Kind::Rep || kind == Kind::Pub) {
        env["b"] = body;
    }
    return env.dump();
}

}  // namespace

std::string encode_req(const std::string& url, std::uint64_t seq, const std::string& body) {
    return encode(Kind::Req, url, seq, false, body);
}

std::string encode_rep(std::uint64_t seq, bool ok, const std::string& body) {
    return encode(Kind::Rep, std::string(), seq, ok, body);
}

std::string encode_pub(const std::string& topic, const std::string& body) {
    return encode(Kind::Pub, topic, 0, false, body);
}

std::string encode_sub(const std::string& topic) {
    return encode(Kind::Sub, topic, 0, false, std::string());
}

std::string encode_unsub(const std::string& topic) {
    return encode(Kind::Unsub, topic, 0, false, std::string());
}

std::string encode_suback(const std::string& topic) {
    return encode(Kind::SubAck, topic, 0, false, std::string());
}

std::string encode_unsuback(const std::string& topic) {
    return encode(Kind::UnsubAck, topic, 0, false, std::string());
}

bool decode(const std::string& text, Envelope& out) {
    nlohmann::json env;
    try {
        env = nlohmann::json::parse(text);
    } catch (...) {
        return false;
    }
    if (!env.is_object()) { return false; }
    if (!env.contains("k") || !env["k"].is_string()) { return false; }
    const std::string k = env["k"].get<std::string>();

    Kind kind = Kind::Invalid;
    if (k == "req")      { kind = Kind::Req; }
    else if (k == "rep") { kind = Kind::Rep; }
    else if (k == "pub") { kind = Kind::Pub; }
    else if (k == "sub") { kind = Kind::Sub; }
    else if (k == "unsub")     { kind = Kind::Unsub; }
    else if (k == "suback")    { kind = Kind::SubAck; }
    else if (k == "unsuback")  { kind = Kind::UnsubAck; }
    else { return false; }

    // 必填字段校验（u：req/pub/sub/unsub/suback/unsuback；s：req/rep；ok：rep）
    if (kind != Kind::Rep) {
        if (!env.contains("u") || !env["u"].is_string()) { return false; }
    }
    if (kind == Kind::Req || kind == Kind::Rep) {
        if (!env.contains("s") || !env["s"].is_number_unsigned()) { return false; }
    }
    if (kind == Kind::Rep) {
        if (!env.contains("ok") || !env["ok"].is_boolean()) { return false; }
    }

    Envelope result;
    result.kind = kind;
    if (env.contains("u")) { result.url = env["u"].get<std::string>(); }
    if (env.contains("s")) { result.seq = env["s"].get<std::uint64_t>(); }
    if (env.contains("ok")) { result.ok = env["ok"].get<bool>(); }
    if (env.contains("b") && env["b"].is_string()) { result.body = env["b"].get<std::string>(); }
    out = std::move(result);
    return true;
}

}  // namespace bus
}  // namespace ssn
