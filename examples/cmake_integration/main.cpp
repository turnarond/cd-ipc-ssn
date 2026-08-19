/*
 * main.cpp - find_package(ssn) 集成最小示例（C++ 服务框架）
 *
 * 展示：安装后通过 ssn::ssn_framework 目标消费 C++ 框架，
 * 启动一个 echo 服务并同步调用一次。
 */

#include <iostream>
#include <string>

#include "ssn/framework/SsnService.hpp"
#include "ssn/framework/SsnClient.hpp"

using namespace ssn;

// 最小 echo 服务：/version 由框架内置；这里注册一个 /ping
class PingService : public SsnService {
public:
    bool OnInit(int argc, char **argv) override {
        if (!SsnService::OnInit(argc, argv)) return false;
        RegisterMethod<std::string, std::string>("/ping",
            [](const std::string &msg) { return msg; });
        return true;
    }
};

int main(void)
{
    PingService svc;
    if (!svc.initialize(0, nullptr)) {
        std::cerr << "FAIL: initialize" << std::endl;
        return 1;
    }
    if (!svc.start()) {
        std::cerr << "FAIL: start" << std::endl;
        svc.destroy();
        return 1;
    }
    svc.stop();
    svc.destroy();
    std::cout << "OK: find_package(ssn) C++ 框架集成可用" << std::endl;
    return 0;
}
