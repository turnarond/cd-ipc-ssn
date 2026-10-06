// 测试：SsnService 服务端基类——真实 IPC 回环（TCP 18901 端口）
// 覆盖：方法注册/重复注册拒绝/保留前缀拒绝、生命周期 start/stop、
//       /add 往返、/boom 异常 1003、JSON 解析失败 1002、未知 URL 1001、
//       内置端点 /urls /health /version、publish 发布（订阅客户端收消息）
// 客户端对端：ssn::Node 直连 + 框架 JSON 信封协议（req/rep/sub/suback/pub），
// 独立组包验证协议兼容性，不依赖 SsnClient（被测面收敛到 SsnService）
#include "ssn/framework/SsnService.hpp"

#include "ssn/node/Node.hpp"
#include "version/ssn_version.h"   // M7：版本断言引用宏而非硬编码

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

constexpr const char* SERVER_ADDR = "tcp://127.0.0.1:18901";
constexpr uint16_t SERVER_PORT = 18901;

// 测试服务：/add 正常求和，/boom 抛异常（验证框架错误码 1003）
class TestServer : public ssn::SsnService {
public:
    TestServer() {
        listenTcp("127.0.0.1", SERVER_PORT);
        registerJson("/add", [](const nlohmann::json& req) -> nlohmann::json {
            return {{"sum", req.at("a").get<int>() + req.at("b").get<int>()}};
        });
        registerJson("/boom", [](const nlohmann::json&) -> nlohmann::json {
            throw std::runtime_error("测试异常");
        });
    }
};

// —— ssn::Node 直连客户端（信封协议，验证框架服务端行为，不依赖 SsnClient）——
// 信封格式（框架 NodeBus 协议，测试独立组包）：
//   req: {"k":"req","u":url,"s":seq,"b":body}   rep: {"k":"rep","s":seq,"ok":bool,"b":body}
//   sub: {"k":"sub","u":topic}  suback: {"k":"suback","u":topic}
//   pub: {"k":"pub","u":topic,"b":data}

// rep 应答槽（事件线程写、主线程轮询 done 后读，atomic 提供 happens-before）
struct RawReply {
    std::atomic<bool> done{false};   // 收到 rep 信封
    bool ok = false;                 // rep.ok（服务端处理成功与否）
    std::string body = "{}";         // rep.b（应答 JSON 文本）；默认空对象——
                                     // 失败路径（超时/发送失败）后续 parse 不崩，
                                     // 红灯以 FAIL 行呈现而非 terminate
};
RawReply g_reply;

// suback/unsuback 确认槽
struct RawAck {
    std::atomic<bool> done{false};
    std::string url;
};
RawAck g_ack;

// pub 消息槽
struct PubMsg {
    std::atomic<bool> done{false};
    std::string url;
    std::string body = "{}";   // 默认空对象：失败路径 parse 不崩（同 RawReply）
};
PubMsg g_msg;

std::atomic<uint64_t> g_seq{1};   // 请求序号（客户端自增）

// 事件处理器：按信封种类分流到各槽（无捕获，可转函数指针语义）
void on_client_event(const ssn::NodeEvent& event) {
    if (event.type != ssn::NodeEventType::MessageReceived) { return; }
    if (event.message.empty()) { return; }   // 空载荷（旧协议回包等）直接忽略
    std::string text(reinterpret_cast<const char*>(event.message.data()),
                     event.message.size());
    nlohmann::json env;
    try {
        env = nlohmann::json::parse(text);
    } catch (...) {
        return;
    }
    if (!env.is_object()) { return; }
    const std::string kind = env.value("k", std::string());
    if (kind == "rep") {
        g_reply.ok = env.value("ok", false);
        g_reply.body = env.value("b", std::string());
        g_reply.done.store(true);
    } else if (kind == "suback" || kind == "unsuback") {
        g_ack.url = env.value("u", std::string());
        g_ack.done.store(true);
    } else if (kind == "pub") {
        g_msg.url = env.value("u", std::string());
        g_msg.body = env.value("b", std::string());
        g_msg.done.store(true);
    }
}

// 创建客户端节点：后台驱动 + 事件槽接线；失败返回空指针
std::unique_ptr<ssn::Node> make_client_node() {
    auto created = ssn::Node::create(ssn::NodeConfig{});
    if (!created) { return nullptr; }
    auto node = std::make_unique<ssn::Node>(std::move(created.value()));
    node->setEventHandler(on_client_event);
    if (!node->startBackground()) { return nullptr; }
    return node;
}

// 连接服务端：connect() 的 InProgress 路径立即返回 Connecting 态 PeerId，
// 轮询等待握手完成（PeerState::Connected）后 out_peer 才可用于 send
bool raw_connect(ssn::Node& node, ssn::PeerId& out_peer,
                 uint64_t timeout_ms = 5000) {
    auto r = node.connect(ssn::ListenAddress{SERVER_ADDR},
                          ssn::ConnectOptions{std::chrono::milliseconds(timeout_ms)});
    if (!r) { return false; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto info = node.peerInfo(r.value());
        if (info.ok() && info.value().state == ssn::PeerState::Connected) {
            out_peer = r.value();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

// 发送原始字节（协议注入用：非法体/异常信封测试）
bool raw_send(ssn::Node& node, ssn::PeerId peer, const std::string& bytes) {
    return static_cast<bool>(node.send(
        peer, ssn::ByteView{reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()}));
}

// 轮询等待 rep（deadline 5s），结果快照到 out
bool wait_reply(RawReply& out) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (!g_reply.done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    out.done.store(g_reply.done.load());
    out.ok = g_reply.ok;
    out.body = g_reply.body;
    return out.done.load();
}

// 同步 RPC：发起 req 信封并等待 rep
bool raw_call(ssn::Node& node, ssn::PeerId peer, const char* url,
              const nlohmann::json& req, RawReply& out) {
    std::string bytes = (nlohmann::json{{"k", "req"},
                                        {"u", url},
                                        {"s", g_seq.fetch_add(1)},
                                        {"b", req.dump()}}).dump();
    g_reply.done.store(false);
    g_reply.ok = false;
    g_reply.body = "{}";
    if (!raw_send(node, peer, bytes)) { return false; }
    return wait_reply(out);
}

// 订阅：发 sub 信封并等待 suback
bool sub_topic(ssn::Node& node, ssn::PeerId peer, const char* topic) {
    g_ack.done.store(false);
    if (!raw_send(node, peer, (nlohmann::json{{"k", "sub"}, {"u", topic}}).dump())) {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (!g_ack.done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return g_ack.done.load() && g_ack.url == topic;
}

// 从应答体中取框架错误码（无 error 对象返回 -1）
int error_code_of(const nlohmann::json& resp) {
    if (resp.is_object() && resp.contains("error") &&
        resp["error"].is_object() && resp["error"].contains("code")) {
        return resp["error"]["code"].get<int>();
    }
    return -1;
}

// 安全取值：CHECK 宏不短路，失败路径（应答为空对象）继续取值不抛异常，
// 红灯以 FAIL 行呈现而非 terminate
template <typename T>
T jget(const nlohmann::json& j, const char* key, T fallback) {
    try { return j.at(key).get<T>(); } catch (...) { return fallback; }
}

// 方法注册约束与监听配置
void test_registration() {
    TestServer server;
    CHECK(server.listenHost() == "127.0.0.1");
    CHECK(server.listenPort() == SERVER_PORT);

    // 重复注册同一 URL 返回 false
    CHECK(!server.registerJson("/add", [](const nlohmann::json&) -> nlohmann::json { return nullptr; }));

    // 内置端点保留前缀，拒绝用户注册
    CHECK(!server.registerJson("/urls", [](const nlohmann::json&) -> nlohmann::json { return nullptr; }));
    CHECK(!server.registerJson("/health", [](const nlohmann::json&) -> nlohmann::json { return nullptr; }));
    CHECK(!server.registerJson("/version", [](const nlohmann::json&) -> nlohmann::json { return nullptr; }));

    // unregister：未注册返回 false，已注册返回 true，之后可重新注册
    CHECK(!server.unregister("/no_such"));
    CHECK(server.unregister("/add"));
    CHECK(server.registerJson("/add", [](const nlohmann::json& req) -> nlohmann::json {
        return {{"sum", req.at("a").get<int>() + req.at("b").get<int>()}};
    }));

    // Issue #5-3 回归：尾斜杠 URL（长度 > 1）与兜底命令 "/"（旧 C 层保留语义）
    // 均为保留端点，拒绝注册/退订
    CHECK(!server.registerJson("/", [](const nlohmann::json&) -> nlohmann::json { return nullptr; }));
    CHECK(!server.registerJson("/foo/", [](const nlohmann::json&) -> nlohmann::json { return nullptr; }));
    CHECK(!server.registerJson("//", [](const nlohmann::json&) -> nlohmann::json { return nullptr; }));
    CHECK(!server.unregister("/foo/"));
}

// 生命周期：initialize/start/stop/destroy 状态迁移
void test_lifecycle() {
    TestServer server;
    CHECK(server.initialize(0, nullptr));
    CHECK(server.state() == ssn::ServiceState::Initialized);
    CHECK(server.start());
    CHECK(server.state() == ssn::ServiceState::Started);
    server.stop();
    CHECK(server.state() == ssn::ServiceState::Stopped);
    server.destroy();
    CHECK(server.state() == ssn::ServiceState::Created);
}

// 真实 IPC 回环：方法往返 + 错误码 + 内置端点
void test_rpc_roundtrip() {
    TestServer server;
    CHECK(server.initialize(0, nullptr));
    CHECK(server.start());

    std::unique_ptr<ssn::Node> client = make_client_node();
    CHECK(client != nullptr);
    ssn::PeerId cpid;
    CHECK(raw_connect(*client, cpid));

    RawReply out;

    // /add 往返正确
    CHECK(raw_call(*client, cpid, "/add", {{"a", 3}, {"b", 4}}, out));
    CHECK(out.done.load());
    CHECK(out.ok);
    nlohmann::json resp = nlohmann::json::parse(out.body);
    CHECK(jget(resp, "sum", -1) == 7);

    // /boom 抛异常 → 框架错误码 1003
    CHECK(raw_call(*client, cpid, "/boom", nlohmann::json::object(), out));
    CHECK(out.done.load());
    CHECK(!out.ok);
    resp = nlohmann::json::parse(out.body);
    CHECK(error_code_of(resp) == 1003);

    // 请求体非法 JSON → 框架错误码 1002（信封 b 为 JSON 文本字符串，可注入非法体）
    {
        std::string bytes = (nlohmann::json{{"k", "req"},
                                            {"u", "/add"},
                                            {"s", g_seq.fetch_add(1)},
                                            {"b", "{invalid json"}}).dump();
        g_reply.done.store(false);
        g_reply.ok = false;
        g_reply.body = "{}";
        CHECK(raw_send(*client, cpid, bytes));
        CHECK(wait_reply(out));
        CHECK(!out.ok);
        resp = nlohmann::json::parse(out.body);
        CHECK(error_code_of(resp) == 1002);
    }

    // 未知 URL → 框架错误码 1001
    CHECK(raw_call(*client, cpid, "/no_such_method", nlohmann::json::object(), out));
    CHECK(out.done.load());
    CHECK(!out.ok);
    resp = nlohmann::json::parse(out.body);
    CHECK(error_code_of(resp) == 1001);

    // 内置端点 /urls：包含全部内置端点与用户方法
    CHECK(raw_call(*client, cpid, "/urls", nlohmann::json::object(), out));
    CHECK(out.done.load() && out.ok);
    resp = nlohmann::json::parse(out.body);
    CHECK(resp.contains("urls") && resp["urls"].is_array());
    const char* expected_urls[] = {"/urls", "/health", "/version", "/add", "/boom"};
    for (const char* expect : expected_urls) {
        bool found = false;
        for (const auto& u : resp["urls"]) {
            if (u.get<std::string>() == expect) { found = true; break; }
        }
        CHECK(found);
    }

    // 内置端点 /health：status ok，连接数 >= 1（本客户端已连入），消息数 >= 1（已分发多次）
    CHECK(raw_call(*client, cpid, "/health", nlohmann::json::object(), out));
    CHECK(out.done.load() && out.ok);
    resp = nlohmann::json::parse(out.body);
    CHECK(jget(resp, "status", std::string()) == "ok");
    CHECK(jget(resp, "connections", 0) >= 1);
    CHECK(jget(resp, "messages", uint64_t{0}) >= 1);

    // 内置端点 /version：与 SSN_VERSION_STRING 一致（M7：引用宏，版本升级不再红）
    CHECK(raw_call(*client, cpid, "/version", nlohmann::json::object(), out));
    CHECK(out.done.load() && out.ok);
    resp = nlohmann::json::parse(out.body);
    CHECK(jget(resp, "version", std::string()) == SSN_VERSION_STRING);

    // 框架内置端点直接访问（非 IPC 路径）
    CHECK(server.builtinVersion().at("version").get<std::string>() == SSN_VERSION_STRING);
    CHECK(server.builtinHealth().at("status").get<std::string>() == "ok");

    (void)client->stop();
    (void)client->waitStopped();
    server.stop();
    server.destroy();
}

// 重复 initialize→destroy→initialize：销毁后重新初始化不泄漏、功能正常
// （Task 6-M4 用例，Task 5 Minor-1 修复的回归——destroy 从 Initialized 态直接
// 归位 Created（不调 OnShutdown），旧节点仍存活；若 OnInit 不回收旧节点，
// 新节点监听同端口会 EADDRINUSE，initialize 失败，此用例即变红）
void test_reinit() {
    TestServer server;
    CHECK(server.initialize(0, nullptr));
    CHECK(server.state() == ssn::ServiceState::Initialized);
    server.destroy();                                    // 未 start 直接销毁：归位 Created
    CHECK(server.state() == ssn::ServiceState::Created);
    CHECK(server.initialize(0, nullptr));                // 重新初始化：旧节点必须回收
    CHECK(server.start());
    CHECK(server.state() == ssn::ServiceState::Started);

    // 功能验证：重新初始化后的实例仍可正常响应 RPC
    std::unique_ptr<ssn::Node> client = make_client_node();
    CHECK(client != nullptr);
    ssn::PeerId cpid;
    CHECK(raw_connect(*client, cpid));

    RawReply out;
    CHECK(raw_call(*client, cpid, "/add", {{"a", 5}, {"b", 6}}, out));
    CHECK(out.done.load());
    CHECK(out.ok);
    nlohmann::json resp = nlohmann::json::parse(out.body);
    CHECK(jget(resp, "sum", -1) == 11);

    (void)client->stop();
    (void)client->waitStopped();
    server.stop();
    server.destroy();
    CHECK(server.state() == ssn::ServiceState::Created);
}

// publish：订阅客户端收到发布消息
void test_publish() {
    TestServer server;
    CHECK(server.initialize(0, nullptr));
    CHECK(server.start());

    std::unique_ptr<ssn::Node> client = make_client_node();
    CHECK(client != nullptr);
    ssn::PeerId cpid;
    CHECK(raw_connect(*client, cpid));

    CHECK(sub_topic(*client, cpid, "/news"));
    CHECK(server.publish("/news", {{"title", "测试消息"}, {"seq", 1}}));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (!g_msg.done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(g_msg.done.load());
    CHECK(g_msg.url == "/news");
    nlohmann::json published = nlohmann::json::parse(g_msg.body);
    CHECK(jget(published, "title", std::string()) == "测试消息");
    CHECK(jget(published, "seq", -1) == 1);

    (void)client->stop();
    (void)client->waitStopped();
    server.stop();
    server.destroy();
}

// Issue #5-6 回归：OnInit 失败（监听端口冲突 → 节点监听失败）后不得悬挂——
// initialize 返回 false 且状态归位 Created，换端口二次 initialize 可成功
void test_init_failure_rollback() {
    // A 先占用 18903 端口
    TestServer server_a;
    server_a.listenTcp("127.0.0.1", 18903);
    CHECK(server_a.initialize(0, nullptr));
    CHECK(server_a.start());

    // B 监听同端口：节点 listen 失败（EADDRINUSE）→ initialize 返回 false，不悬挂
    TestServer server_b;
    server_b.listenTcp("127.0.0.1", 18903);
    CHECK(!server_b.initialize(0, nullptr));
    CHECK(server_b.state() == ssn::ServiceState::Created);

    // 换端口二次 initialize 成功（无泄漏：内部节点已随失败路径回收）
    server_b.listenTcp("127.0.0.1", 18904);
    CHECK(server_b.initialize(0, nullptr));
    CHECK(server_b.start());
    CHECK(server_b.state() == ssn::ServiceState::Started);

    server_a.stop();
    server_a.destroy();
    server_b.stop();
    server_b.destroy();
}

}  // namespace

int main() {
    test_registration();
    test_lifecycle();
    test_rpc_roundtrip();
    test_reinit();
    test_publish();
    test_init_failure_rollback();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
