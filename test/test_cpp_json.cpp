// 类型安全层测试：DTO 结构体 + RegisterMethod/Call 全链路
#include "ssn/framework/SsnService.hpp"
#include "ssn/framework/SsnClient.hpp"
#include <cstdio>
#include <string>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

// 用户 DTO（字段 snake_case，服务框架不干涉用户类型）
struct AddRequest {
    int a = 0;
    int b = 0;
    std::string note;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddRequest, a, b, note)
};

struct AddResponse {
    int sum = 0;
    std::string note;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddResponse, sum, note)
};

class CalcServer : public ssn::SsnService {
public:
    CalcServer() {
        listenTcp("127.0.0.1", 18903);
        RegisterMethod<AddRequest, AddResponse>("/calc/add", [](const AddRequest& req) {
            return AddResponse{req.a + req.b, req.note};
        });
    }
};

void test_dto_roundtrip() {
    CalcServer srv;
    CHECK(srv.initialize(0, nullptr));
    CHECK(srv.start());
    ssn::SsnClient cli;
    CHECK(cli.connect("tcp://127.0.0.1:18903"));

    AddResponse resp;
    AddRequest req{10, 20, "测试"};
    CHECK(cli.Call("/calc/add", req, resp));
    CHECK(resp.sum == 30);
    CHECK(resp.note == "测试");          // DTO 字段完整往返

    cli.disconnect();
    srv.stop();
    srv.destroy();
}

void test_json_direct_also_works() {
    // json 层与类型安全层共存
    CalcServer srv;
    CHECK(srv.initialize(0, nullptr));
    CHECK(srv.start());
    ssn::SsnClient cli;
    CHECK(cli.connect("tcp://127.0.0.1:18903"));
    nlohmann::json resp;
    CHECK(cli.callJson("/calc/add", {{"a", 1}, {"b", 2}, {"note", "x"}}, resp));
    CHECK(resp.at("sum") == 3);
    cli.disconnect();
    srv.stop();
    srv.destroy();
}

}  // namespace

int main() {
    test_dto_roundtrip();
    test_json_direct_also_works();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
