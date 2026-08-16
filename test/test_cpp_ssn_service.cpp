// 测试：SsnService 服务端基类——真实 IPC 回环（TCP 18901 端口）
// 覆盖：方法注册/重复注册拒绝/保留前缀拒绝、生命周期 start/stop、
//       /add 往返、/boom 异常 1003、JSON 解析失败 1002、未知 URL 1001、
//       内置端点 /urls /health /version、publish 发布（订阅客户端收消息）
#include "ssn/framework/SsnService.hpp"

// C 层直连客户端 API（头已带 extern "C" 保护，可直接包含）
#include "node/ssn_node.h"
#include "ssn_frame.h"

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
constexpr uint64_t CALL_TIMEOUT_MS = 3000;

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

// —— C 层直连客户端（阻塞轮询模式，验证框架服务端行为，不依赖 Task 6 SsnClient）——
struct RpcReply {
    bool done = false;      // 应答已返回（或超时无应答）
    bool replied = false;   // 服务端有回应（ipc_hdr 非空）
    uint32_t status = 0;    // 应答头部状态码
    std::string body;       // 应答 JSON 文本
};

// RPC 应答回调：回调内只拷贝数据（hdr/data 在回调返回后失效）
void on_rpc_reply(ssn_client_t* /*client*/, ssn_header_t* hdr, ssn_data_ref_t* data, void* arg) {
    auto* reply = static_cast<RpcReply*>(arg);
    reply->done = true;
    if (!hdr) { return; }   // hdr 为空表示服务端无回应（超时）
    reply->replied = true;
    reply->status = ssn_get_status(hdr);
    if (data && data->data && data->length) {
        reply->body.assign(static_cast<char*>(data->data), data->length);
    }
}

// 同步 RPC：发起调用后轮询驱动客户端节点，直到应答或超时
bool rpc_json(ssn_node_t* node, const char* url, const nlohmann::json& req, RpcReply& out) {
    std::string body = req.dump();
    ssn_url_ref_t u = {const_cast<char*>(url), static_cast<uint32_t>(std::strlen(url))};
    ssn_data_ref_t d = {const_cast<char*>(body.data()), body.size()};
    out = RpcReply();
    if (ssn_node_rpc_call(node, SERVER_ADDR, &u, &d, on_rpc_reply, &out, CALL_TIMEOUT_MS) < 0) {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (!out.done && std::chrono::steady_clock::now() < deadline) {
        ssn_node_poll(node, 20);
    }
    return out.done;
}

// 从应答体中取框架错误码（无 error 对象返回 -1）
int error_code_of(const nlohmann::json& resp) {
    if (resp.is_object() && resp.contains("error") &&
        resp["error"].is_object() && resp["error"].contains("code")) {
        return resp["error"]["code"].get<int>();
    }
    return -1;
}

ssn_node_t* make_client_node() {
    ssn_node_config_t cfg = {};
    std::strncpy(cfg.node_type, "client", sizeof(cfg.node_type) - 1);
    std::strncpy(cfg.node_name, "cpp-test-client", sizeof(cfg.node_name) - 1);
    cfg.capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_RPC | SSN_NODE_CAP_PUBSUB;
    return ssn_node_create(&cfg);
}

// 发布消息回调（订阅后每主题一个）
struct PubMsg {
    std::atomic<bool> done{false};
    std::string url;
    std::string body;
};

void on_pub_msg(ssn_client_t* /*client*/, ssn_url_ref_t* url, ssn_data_ref_t* data, void* arg) {
    auto* msg = static_cast<PubMsg*>(arg);
    msg->done = true;
    if (url && url->url && url->url_len) {
        msg->url.assign(url->url, url->url_len);
    }
    if (data && data->data && data->length) {
        msg->body.assign(static_cast<char*>(data->data), data->length);
    }
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

    // Issue #5-3 回归：尾斜杠 URL（长度 > 1）拒绝注册/退订——C 层把 "/foo/"
    // 注册为前缀规则而框架分发为精确匹配，注册后永不命中（语义缝隙）；
    // "/"（长度 1）兜底命令不受影响（仍为保留端点，拒绝注册）
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

    ssn_node_t* client = make_client_node();
    CHECK(client != nullptr);
    CHECK(ssn_node_start(client));

    RpcReply out;

    // /add 往返正确
    CHECK(rpc_json(client, "/add", {{"a", 3}, {"b", 4}}, out));
    CHECK(out.replied);
    CHECK(out.status == 0);
    nlohmann::json resp = nlohmann::json::parse(out.body);
    CHECK(resp.at("sum").get<int>() == 7);

    // /boom 抛异常 → 框架错误码 1003
    CHECK(rpc_json(client, "/boom", nlohmann::json::object(), out));
    CHECK(out.replied);
    CHECK(out.status != 0);
    resp = nlohmann::json::parse(out.body);
    CHECK(error_code_of(resp) == 1003);

    // 请求体非法 JSON → 框架错误码 1002
    {
        const char* bad = "{invalid json";
        ssn_url_ref_t u = {const_cast<char*>("/add"), 4};
        ssn_data_ref_t d = {const_cast<char*>(bad), std::strlen(bad)};
        out = RpcReply();
        CHECK(ssn_node_rpc_call(client, SERVER_ADDR, &u, &d, on_rpc_reply, &out, CALL_TIMEOUT_MS) == 0);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
        while (!out.done && std::chrono::steady_clock::now() < deadline) {
            ssn_node_poll(client, 20);
        }
        CHECK(out.done && out.replied);
        CHECK(out.status != 0);
        resp = nlohmann::json::parse(out.body);
        CHECK(error_code_of(resp) == 1002);
    }

    // 未知 URL → 框架错误码 1001
    CHECK(rpc_json(client, "/no_such_method", nlohmann::json::object(), out));
    CHECK(out.replied);
    CHECK(out.status != 0);
    resp = nlohmann::json::parse(out.body);
    CHECK(error_code_of(resp) == 1001);

    // 内置端点 /urls：包含全部内置端点与用户方法
    CHECK(rpc_json(client, "/urls", nlohmann::json::object(), out));
    CHECK(out.replied && out.status == 0);
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
    CHECK(rpc_json(client, "/health", nlohmann::json::object(), out));
    CHECK(out.replied && out.status == 0);
    resp = nlohmann::json::parse(out.body);
    CHECK(resp.at("status").get<std::string>() == "ok");
    CHECK(resp.at("connections").get<int>() >= 1);
    CHECK(resp.at("messages").get<uint64_t>() >= 1);

    // 内置端点 /version：与 SSN_VERSION_STRING 一致
    CHECK(rpc_json(client, "/version", nlohmann::json::object(), out));
    CHECK(out.replied && out.status == 0);
    resp = nlohmann::json::parse(out.body);
    CHECK(resp.at("version").get<std::string>() == "2.4.0");

    // 框架内置端点直接访问（非 IPC 路径）
    CHECK(server.builtinVersion().at("version").get<std::string>() == "2.4.0");
    CHECK(server.builtinHealth().at("status").get<std::string>() == "ok");

    ssn_node_stop(client);
    ssn_node_destroy(client);
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
    ssn_node_t* client = make_client_node();
    CHECK(client != nullptr);
    CHECK(ssn_node_start(client));

    RpcReply out;
    CHECK(rpc_json(client, "/add", {{"a", 5}, {"b", 6}}, out));
    CHECK(out.replied);
    CHECK(out.status == 0);
    nlohmann::json resp = nlohmann::json::parse(out.body);
    CHECK(resp.at("sum").get<int>() == 11);

    ssn_node_stop(client);
    ssn_node_destroy(client);
    server.stop();
    server.destroy();
    CHECK(server.state() == ssn::ServiceState::Created);
}

// publish：订阅客户端收到发布消息
void test_publish() {
    TestServer server;
    CHECK(server.initialize(0, nullptr));
    CHECK(server.start());

    ssn_node_t* client = make_client_node();
    CHECK(client != nullptr);
    CHECK(ssn_node_start(client));

    PubMsg msg;
    ssn_url_ref_t topic = {const_cast<char*>("/news"), 5};
    CHECK(ssn_node_subscribe(client, SERVER_ADDR, &topic, on_pub_msg, &msg, CALL_TIMEOUT_MS));

    // 轮询驱动订阅握手（服务端在 svc 线程 poll 中处理 SUBSCRIBE），再发布
    for (int i = 0; i < 25; ++i) {   // 约 500ms
        ssn_node_poll(client, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(server.publish("/news", {{"title", "测试消息"}, {"seq", 1}}));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (!msg.done && std::chrono::steady_clock::now() < deadline) {
        ssn_node_poll(client, 20);
    }
    CHECK(msg.done);
    CHECK(msg.url == "/news");
    nlohmann::json published = nlohmann::json::parse(msg.body);
    CHECK(published.at("title").get<std::string>() == "测试消息");
    CHECK(published.at("seq").get<int>() == 1);

    ssn_node_stop(client);
    ssn_node_destroy(client);
    server.stop();
    server.destroy();
}

// Issue #5-6 回归：OnInit 失败（监听端口冲突 → 节点 start 失败）后不得悬挂——
// initialize 返回 false 且状态归位 Created，换端口二次 initialize 可成功
void test_init_failure_rollback() {
    // A 先占用 18903 端口
    TestServer server_a;
    server_a.listenTcp("127.0.0.1", 18903);
    CHECK(server_a.initialize(0, nullptr));
    CHECK(server_a.start());

    // B 监听同端口：节点 start 失败（EADDRINUSE）→ initialize 返回 false，不悬挂
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
