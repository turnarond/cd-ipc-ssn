// 文件: src/framework/ServiceTask.cpp
// 功能: ServiceTask 线程池基类实现——activate 按线程数并发执行 svc()，
//       requestShutdown/wait 协作退出；svc() 外层 try/catch 防止
//       线程内异常导致 std::terminate。
#include "ssn/framework/ServiceTask.hpp"

#include <exception>

#include "util/ssn_log.h"   // C 头已带 extern "C" 保护，可直接包含

namespace ssn {

bool ServiceTask::activate(int num_threads) {
    if (running_.exchange(true)) { return false; }  // 已运行则拒绝重复激活
    threads_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        threads_.emplace_back([this]() {
            try {
                svc();  // 派生类线程入口
            } catch (const std::exception& e) {
                LOG_ERROR("svc() 线程异常退出: %s", e.what());
            } catch (...) {
                LOG_ERROR("svc() 线程抛出未知异常");
            }
        });
    }
    return true;
}

void ServiceTask::requestShutdown() {
    running_ = false;  // svc 循环据此退出
}

void ServiceTask::wait() {
    for (auto& th : threads_) {
        if (th.joinable()) { th.join(); }
    }
    threads_.clear();  // 线程已回收，供复用/threadCount 归零
}

bool ServiceTask::startImpl() {
    return activate(1);  // 生命周期 start → 单线程运行
}

void ServiceTask::stopImpl() {
    requestShutdown();
    wait();
}

bool ServiceTask::isRunning() const {
    return running_.load();
}

int ServiceTask::threadCount() const {
    return static_cast<int>(threads_.size());
}

}  // namespace ssn
