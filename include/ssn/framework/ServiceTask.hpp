/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 服务线程池基类（线程生命周期管理）
 */
// 文件: include/ssn/framework/ServiceTask.hpp
// 功能: 服务线程池基类——继承 ServiceBase，覆写 startImpl/stopImpl
//       实现线程生命周期管理；派生类实现 svc() 作为线程入口，
//       SsnService 等服务基类基于本类构建。
#ifndef SSN_FRAMEWORK_SERVICETASK_HPP
#define SSN_FRAMEWORK_SERVICETASK_HPP

#include <atomic>
#include <thread>
#include <vector>

#include "ssn/framework/ServiceBase.hpp"

namespace ssn {

// 线程池服务基类：activate 启动 num_threads 个线程并发执行 svc()，
// requestShutdown 置 running_=false 通知 svc 退出，wait 回收全部线程
class ServiceTask : public ServiceBase {
public:
    ~ServiceTask() override;              // 兜底回收线程（requestShutdown + wait）
    bool activate(int num_threads = 1);  // 重复调用或已运行返回 false
    void wait();                          // join 全部线程
    void requestShutdown();               // 置 running_=false，svc 应据此退出
    bool isRunning() const;
    int threadCount() const;

protected:
    virtual int svc() = 0;                // 线程入口，子类实现
    bool startImpl() override;            // 调 activate(1)
    void stopImpl() override;             // requestShutdown + wait

private:
    std::atomic<bool> running_{false};
    std::vector<std::thread> threads_;
};

}  // namespace ssn

#endif  // SSN_FRAMEWORK_SERVICETASK_HPP
