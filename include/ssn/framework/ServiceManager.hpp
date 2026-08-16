// 文件: include/ssn/framework/ServiceManager.hpp
// 功能: 服务管理器——"一行启动服务"的入口。Run<T> 编排完整生命周期：
//       initialize → start → 等待 SIGINT/SIGTERM → stop → destroy → 返回 0。
//       信号处理采用"阻塞 + sigtimedwait 轮询"：本线程阻塞 SIGINT/SIGTERM，
//       信号由处理器置位停止标志；若信号被其他线程的处理器消费（如测试中
//       子线程 raise(SIGINT)），本线程以超时轮询感知标志，避免永久阻塞。
//       Run 入口先安装信号处理（阻塞 + 处理器）再 initialize/start（Task 4-M2
//       硬化），消除「initialize 期间收到信号走默认动作直接终止」的窗口。
#ifndef SSN_FRAMEWORK_SERVICEMANAGER_HPP
#define SSN_FRAMEWORK_SERVICEMANAGER_HPP

#include "ssn/framework/ServiceBase.hpp"

#include <atomic>
#include <pthread.h>
#include <signal.h>
#include <time.h>

namespace ssn {

class ServiceManager {
public:
    // Run 完整生命周期：initialize → start → 等待 SIGINT/SIGTERM → stop → destroy → 返回 0
    template <typename ServiceT>
    static int Run(int argc, char** argv);

    static void requestStop();       // 供信号处理器或外部线程请求停止
    static bool stopRequested();

private:
    static std::atomic<bool> s_stop_requested_;
    // 阻塞 SIGINT/SIGTERM 并安装停止处理器（Run 入口最先调用），返回信号集
    // 供 Run 的 sigtimedwait 等待循环复用
    static sigset_t installSignalHandlers();
};

template <typename ServiceT>
int ServiceManager::Run(int argc, char** argv) {
    // 单次运行开始时复位停止标志，支持重复调用
    s_stop_requested_ = false;

    // 先安装信号处理再进入 initialize/start（Task 4-M2 硬化）：若不先阻塞并
    // 安装处理器，initialize 期间收到 SIGINT/SIGTERM 会走默认动作直接终止
    // 进程；安装后信号由本线程 sigtimedwait 消费或由处理器置位停止标志
    sigset_t set = installSignalHandlers();

    ServiceT svc;
    if (!svc.initialize(argc, argv)) {
        // LOG_ERROR("服务初始化失败");
        return 1;
    }
    if (!svc.start()) {
        // LOG_ERROR("服务启动失败");
        return 1;
    }

    // 等待停止请求：sigwait 仅在本线程有未消费信号时返回；若信号被其他
    // 线程的处理器消费，处理器已置位标志，本线程以 sigtimedwait 超时轮询感知
    siginfo_t info = {};
    struct timespec ts = {0, 100 * 1000 * 1000};   // 100ms 轮询间隔
    while (!s_stop_requested_) {
        if (sigtimedwait(&set, &info, &ts) > 0) {
            // 本线程直接收到信号（处理器不会执行）：同样视为停止请求
            requestStop();
        }
    }

    // 优雅停止：先 stop 后 destroy（销毁前必须 stop 的所有权约定）
    svc.stop();
    svc.destroy();

    // 单次运行结束后复位停止标志，供下次 Run 或外部查询使用
    s_stop_requested_ = false;
    return 0;
}

}  // namespace ssn

#endif  // SSN_FRAMEWORK_SERVICEMANAGER_HPP
