// ============================================================================
// 稳健客户端示例 —— 三态结果分类 + 断线重连（指数退避）
//
// 教学要点：
//   1. callJson 的「三态分类」：返回值只区分成功/失败，失败还要看应答体——
//       应答含 error.code 是服务端框架错误（如 1001 方法不存在、1003 handler
//       异常），应答为空/无 error 才是超时；本示例用 classify_call 统一封装；
//   2. 断线重连模式（./robust_client reconnect）：服务端停机后调用失败，
//       按 disconnect → 指数退避（1s/2s/4s/8s 封顶）→ connect 循环重试，
//       服务端恢复后自动重连成功；
//   3. RAII 清理：SsnClient 析构函数自动 disconnect（兜底），示例仍显式
//       disconnect 以展示意图；客户端自身在重连循环内不做任何回调操作，
//       遵守框架死锁约束。
// ============================================================================
#include "ssn/framework/SsnClient.hpp"

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace {

constexpr const char* SERVER_ADDR = "tcp://127.0.0.1:18882";

// 调用结果三态分类：成功 / 超时 / 服务端框架错误
enum class CallResult { Success, Timeout, ServerError };

const char* state_name(CallResult r) {
    switch (r) {
    case CallResult::Success:     return "成功";
    case CallResult::Timeout:     return "超时";
    case CallResult::ServerError: return "服务端错误";
    }
    return "未知";
}

// 发起一次调用并按应答体分类：
// 成功 → 应答可用；失败且应答含 error.code → 服务端框架错误；否则 → 超时
CallResult classify_call(ssn::SsnClient& cli, const std::string& url,
                         const nlohmann::json& req, nlohmann::json& resp,
                         uint64_t timeout_ms) {
    if (cli.callJson(url, req, resp, timeout_ms)) {
        return CallResult::Success;
    }
    if (resp.is_object() && resp.contains("error") &&
        resp["error"].is_object() && resp["error"].contains("code")) {
        return CallResult::ServerError;
    }
    return CallResult::Timeout;
}

// 指数退避：1s/2s/4s/8s 封顶（attempt 从 1 开始）
std::chrono::milliseconds backoff(int attempt) {
    long ms = 1000L << (attempt - 1);
    return std::chrono::milliseconds(ms > 8000 ? 8000 : ms);
}

// 阶段 1：三态分类演示（服务端运行中）
void phase_classify() {
    ssn::SsnClient cli;
    if (!cli.connect(SERVER_ADDR)) {
        std::cerr << "连接失败：请确认 robust_server 已启动" << std::endl;
        exit(1);
    }

    // 成功：/echo 正常回显
    nlohmann::json resp;
    auto r = classify_call(cli, "/echo", {{"msg", "你好"}}, resp, 2000);
    std::cout << "[1] /echo       → " << state_name(r)
              << "，应答=" << (r == CallResult::Success ? resp.dump() : "-")
              << std::endl;

    // 超时：/slow 处理 500ms，超时配 300ms → 约 300ms 快速失败（不阻塞等待）
    auto t0 = std::chrono::steady_clock::now();
    r = classify_call(cli, "/slow", nlohmann::json::object(), resp, 300);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    std::cout << "[2] /slow       → " << state_name(r)
              << "（" << ms << "ms 返回，未等待完整 500ms）" << std::endl;

    // 教学点（Issue #5-7）：超时后迟到应答可能「覆盖」下一次调用（C API 无请求
    // 序号回调，框架不可修）——超时路径应避免紧接重试；此处让出 600ms 等迟到
    // 应答落地后再发起下一次调用，保证后续分类演示确定
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // 服务端框架错误：未注册 URL → 1001
    r = classify_call(cli, "/no_such", nlohmann::json::object(), resp, 2000);
    std::cout << "[3] /no_such    → " << state_name(r)
              << "（error.code="
              << (r == CallResult::ServerError
                      ? std::to_string(resp["error"]["code"].get<int>())
                      : "-")
              << "）" << std::endl;

    // 服务端框架错误：handler 抛异常 → 1003（服务端被框架保护，不崩溃）
    r = classify_call(cli, "/boom", nlohmann::json::object(), resp, 2000);
    std::cout << "[4] /boom       → " << state_name(r)
              << "（error.code="
              << (r == CallResult::ServerError
                      ? std::to_string(resp["error"]["code"].get<int>())
                      : "-")
              << "）" << std::endl;

    // RAII：作用域结束析构自动 disconnect；显式调用表达意图
    cli.disconnect();
}

// 阶段 2：断线重连（指数退避，最多 8 次尝试）
// 用法：./robust_client reconnect —— make run 会编排「停服务端 → 重启服务端」
// 演示客户端在服务端停机期间自动重试、恢复后重连成功
int phase_reconnect() {
    ssn::SsnClient cli;
    const int kMaxAttempts = 8;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        std::cout << "尝试 " << attempt << " ..." << std::endl;

        // 未连接（或上次失败后已断开）则重建连接
        if (!cli.connected() && !cli.connect(SERVER_ADDR)) {
            std::this_thread::sleep_for(backoff(attempt));
            continue;
        }
        nlohmann::json resp;
        if (classify_call(cli, "/echo", {{"ping", attempt}}, resp, 2000) ==
            CallResult::Success) {
            std::cout << "重连成功（第 " << attempt << " 次尝试）："
                      << resp.dump() << std::endl;
            cli.disconnect();
            return 0;
        }
        // 调用失败（服务端停机/调用被拒）：断开失活会话，退避后重试
        cli.disconnect();
        std::this_thread::sleep_for(backoff(attempt));
    }
    std::cerr << "重试 " << kMaxAttempts << " 次仍未恢复，退出" << std::endl;
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "reconnect") {
        return phase_reconnect();
    }
    phase_classify();
    return 0;
}
