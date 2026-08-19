/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 服务管理器（Run<T> 一行启动与信号优雅停止）
 */
// 文件: include/ssn/framework/ServiceManager.hpp
// 功能: 服务管理器——"一行启动服务"的入口。Run<T> 编排完整生命周期：
//       initialize → start → 等待 SIGINT/SIGTERM → stop → destroy → 返回 0。
//       信号处理采用"阻塞 + sigtimedwait 轮询"：本线程阻塞 SIGINT/SIGTERM，
//       信号由处理器置位停止标志；若信号被其他线程的处理器消费（如测试中
//       子线程 raise(SIGINT)），本线程以超时轮询感知标志，避免永久阻塞。
//       Run 入口先安装信号处理（阻塞 + 处理器）再 initialize/start（Task 4-M2
//       硬化），消除「initialize 期间收到信号走默认动作直接终止」的窗口；
//       Run 返回前还原调用前的掩码与处理器（Issue #5-2，不向调用方泄漏信号状态）。
//       异常安全（稳定性加固 I1）：initialize/start/等待/stop/destroy 任一步抛
//       出异常（如用户 OnInit 抛 std::runtime_error）均被捕获——还原信号状态、
//       复位停止标志后返回 1（与失败语义一致），保证所有退出路径都不向调用方
//       泄漏信号状态，且可再次 Run。
#ifndef SSN_FRAMEWORK_SERVICEMANAGER_HPP
#define SSN_FRAMEWORK_SERVICEMANAGER_HPP

#include "ssn/framework/ServiceBase.hpp"

#include "util/ssn_log.h"   // C 头已带 extern "C" 保护，可直接包含

#include <atomic>
#include <exception>
#include <pthread.h>
#include <signal.h>
#include <time.h>

namespace ssn {

class SSN_FRAMEWORK_API ServiceManager {
public:
    // Run 完整生命周期：initialize → start → 等待 SIGINT/SIGTERM → stop → destroy → 返回 0
    template <typename ServiceT>
    static int Run(int argc, char** argv);

    static void requestStop();       // 供信号处理器或外部线程请求停止
    static bool stopRequested();

private:
    static std::atomic<bool> s_stop_requested_;
    // 安装前的信号状态快照（原掩码与处理器），供 Run 结束恢复，避免信号
    // 状态泄漏（Issue #5-2：Run 返回后不再阻塞信号、处理器还原默认）
    struct SignalState {
        sigset_t blocked;             // 本线程阻塞信号集（sigtimedwait 等待循环复用）
        sigset_t old_mask;            // 安装前原线程信号掩码
        struct sigaction old_int;     // 安装前原 SIGINT 处理器
        struct sigaction old_term;    // 安装前原 SIGTERM 处理器
        bool mask_ok = false;         // 掩码安装成功标志（失败则不还原，避免写回零值）
        bool int_ok = false;          // SIGINT 处理器安装成功标志
        bool term_ok = false;         // SIGTERM 处理器安装成功标志
    };
    // 阻塞 SIGINT/SIGTERM 并安装停止处理器（Run 入口最先调用），返回快照
    static SignalState installSignalHandlers();
    static void restoreSignalHandlers(const SignalState& st);   // Run 结束还原
};

template <typename ServiceT>
int ServiceManager::Run(int argc, char** argv) {
    // 单次运行开始时复位停止标志，支持重复调用
    s_stop_requested_ = false;

    // 先安装信号处理再进入 initialize/start（Task 4-M2 硬化）：若不先阻塞并
    // 安装处理器，initialize 期间收到 SIGINT/SIGTERM 会走默认动作直接终止
    // 进程；安装后信号由本线程 sigtimedwait 消费或由处理器置位停止标志。
    // 安装前保存原掩码与处理器，全部退出路径结束前还原（Issue #5-2）。
    SignalState sig = installSignalHandlers();

    // 全部生命周期步骤包在 try 内（稳定性加固 I1）：任一步抛异常（含用户钩子
    // 抛出的 std::exception）都会在 catch 中还原信号状态并复位停止标志后返回 1，
    // 与 initialize/start 失败语义一致——进程内的异常不得携带信号状态泄漏到
    // 调用方（ServiceT 析构在栈展开时执行，先于 catch 中的还原）
    try {
        ServiceT svc;
        if (!svc.initialize(argc, argv)) {
            restoreSignalHandlers(sig);
            return 1;
        }
        if (!svc.start()) {
            restoreSignalHandlers(sig);
            return 1;
        }

        // 等待停止请求：sigwait 仅在本线程有未消费信号时返回；若信号被其他
        // 线程的处理器消费，处理器已置位标志，本线程以 sigtimedwait 超时轮询感知
        siginfo_t info = {};
        struct timespec ts = {0, 100 * 1000 * 1000};   // 100ms 轮询间隔
        while (!s_stop_requested_) {
            if (sigtimedwait(&sig.blocked, &info, &ts) > 0) {
                // 本线程直接收到信号（处理器不会执行）：同样视为停止请求
                requestStop();
            }
        }

        // 优雅停止：先 stop 后 destroy（销毁前必须 stop 的所有权约定）
        svc.stop();
        svc.destroy();
    } catch (const std::exception& e) {
        LOG_ERROR("ServiceManager: Run 异常退出，信号状态已还原: %s", e.what());
        restoreSignalHandlers(sig);
        s_stop_requested_ = false;
        return 1;
    } catch (...) {
        LOG_ERROR("ServiceManager: Run 抛出未知异常，信号状态已还原");
        restoreSignalHandlers(sig);
        s_stop_requested_ = false;
        return 1;
    }

    // 还原信号状态（恢复调用前掩码与处理器），再复位停止标志
    restoreSignalHandlers(sig);
    s_stop_requested_ = false;
    return 0;
}

}  // namespace ssn

#endif  // SSN_FRAMEWORK_SERVICEMANAGER_HPP
