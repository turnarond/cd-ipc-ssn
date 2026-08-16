/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * ServiceTask 线程池基类实现
 */
// 文件: src/framework/ServiceTask.cpp
// 功能: ServiceTask 线程池基类实现——activate 按线程数并发执行 svc()，
//       requestShutdown/wait 协作退出；svc() 外层 try/catch 防止
//       线程内异常导致 std::terminate。
#include "ssn/framework/ServiceTask.hpp"

#include <exception>

#include "util/ssn_log.h"   // C 头已带 extern "C" 保护，可直接包含

namespace ssn {

ServiceTask::~ServiceTask() {
    // 兜底回收线程：start 失败/异常路径下 threads_ 可能残留 joinable 线程，
    // 不复位直接析构会 std::terminate（Issue #5-1）。约定 svc() 在 isRunning()
    // 循环内正常退出——阻塞中析构会等待其返回；正常路径（先 stop）下为幂等空操作。
    requestShutdown();
    wait();
}

bool ServiceTask::activate(int num_threads) {
    if (num_threads <= 0) {
        LOG_ERROR("ServiceTask: 线程数非法: %d", num_threads);
        return false;   // 不置 running_，保持未运行状态（Issue #5-1 边界态）
    }
    if (running_.exchange(true)) { return false; }  // 已运行则拒绝重复激活
    try {
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
    } catch (const std::exception& e) {
        // 线程创建中途失败（如资源耗尽）：回滚已创建的线程并复位运行标志，
        // 避免「running_ 已置 true 但线程不完整」的状态不一致（Issue #5-1）
        LOG_ERROR("ServiceTask: 线程创建失败: %s", e.what());
        running_ = false;
        wait();   // join 已创建线程（svc 见 isRunning()=false 自行退出）并清空
        return false;
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
