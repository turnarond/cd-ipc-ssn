/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * ServiceManager 非模板成员实现
 */
// 文件: src/framework/ServiceManager.cpp
// 功能: ServiceManager 非模板成员实现——停止标志的静态存储与访问接口、
//       停止信号处理器安装与还原（Run<T> 为模板，实现位于头文件 ServiceManager.hpp）。
#include "ssn/framework/ServiceManager.hpp"

#include "util/ssn_log.h"

namespace ssn {

std::atomic<bool> ServiceManager::s_stop_requested_{false};

void ServiceManager::requestStop() {
    s_stop_requested_ = true;
}

bool ServiceManager::stopRequested() {
    return s_stop_requested_;
}

ServiceManager::SignalState ServiceManager::installSignalHandlers() {
    // 阻塞 SIGINT/SIGTERM 于调用线程（Run 线程），并安装停止信号处理器（POSIX）。
    // 调用时机在 Run 入口（initialize/start 之前），见头文件 Run 注释（Task 4-M2）。
    // 同时保存原掩码与处理器，供 Run 结束 restoreSignalHandlers 还原（Issue #5-2）。
    SignalState st = {};
    sigemptyset(&st.blocked);
    sigaddset(&st.blocked, SIGINT);
    sigaddset(&st.blocked, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &st.blocked, &st.old_mask) != 0) {
        LOG_WARN("ServiceManager: 信号阻塞失败，SIGINT/SIGTERM 硬化不可用");
    }
    struct sigaction sa = {};
    sa.sa_handler = [](int) { ServiceManager::requestStop(); };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, &st.old_int) != 0) {
        LOG_WARN("ServiceManager: SIGINT 处理器安装失败");
    }
    if (sigaction(SIGTERM, &sa, &st.old_term) != 0) {
        LOG_WARN("ServiceManager: SIGTERM 处理器安装失败");
    }
    return st;
}

void ServiceManager::restoreSignalHandlers(const SignalState& st) {
    // 还原顺序：先处理器后掩码——掩码尚未恢复时迟到的信号仍由我们的处理器
    // 消费（置位停止标志），不会走默认动作直接终止进程
    sigaction(SIGINT, &st.old_int, nullptr);
    sigaction(SIGTERM, &st.old_term, nullptr);
    pthread_sigmask(SIG_SETMASK, &st.old_mask, nullptr);
}

}  // namespace ssn
