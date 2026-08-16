// 文件: src/framework/ServiceManager.cpp
// 功能: ServiceManager 非模板成员实现——停止标志的静态存储与访问接口、
//       停止信号处理器安装（Run<T> 为模板，实现位于头文件 ServiceManager.hpp）。
#include "ssn/framework/ServiceManager.hpp"

namespace ssn {

std::atomic<bool> ServiceManager::s_stop_requested_{false};

void ServiceManager::requestStop() {
    s_stop_requested_ = true;
}

bool ServiceManager::stopRequested() {
    return s_stop_requested_;
}

sigset_t ServiceManager::installSignalHandlers() {
    // 阻塞 SIGINT/SIGTERM 于调用线程（Run 线程），并安装停止信号处理器（POSIX）。
    // 调用时机在 Run 入口（initialize/start 之前），见头文件 Run 注释（Task 4-M2）。
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
    struct sigaction sa = {};
    sa.sa_handler = [](int) { ServiceManager::requestStop(); };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    return set;   // 返回信号集供 Run 的 sigtimedwait 等待循环复用
}

}  // namespace ssn
