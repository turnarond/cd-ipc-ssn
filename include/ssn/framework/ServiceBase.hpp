/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 服务生命周期基类（状态机与钩子编排）
 */
// 文件: include/ssn/framework/ServiceBase.hpp
// 功能: 服务生命周期基类——统一编排服务的创建/初始化/启动/停止/销毁
//       状态机，提供 OnInit/OnShutdown 用户钩子与 startImpl/stopImpl
//       内部扩展点，供 ServiceTask / SsnService 等派生类复用。
#ifndef SSN_FRAMEWORK_SERVICEBASE_HPP
#define SSN_FRAMEWORK_SERVICEBASE_HPP

#include <string>

#include "ssn_export.h"

namespace ssn {

// 服务生命周期状态：Created → Initialized → Started → Stopped，
// destroy 可将任意状态归位回 Created 以便重新初始化
enum class ServiceState { Created, Initialized, Started, Stopped };

class SSN_FRAMEWORK_API ServiceBase {
public:
    ServiceBase();
    virtual ~ServiceBase();
    ServiceBase(const ServiceBase&) = delete;
    ServiceBase& operator=(const ServiceBase&) = delete;

    bool initialize(int argc, char** argv);   // final：Created→Initialized，OnInit 失败回 Created 返回 false
    bool start();                             // final：Initialized→Started，startImpl 失败回 Initialized 返回 false
    void stop();                              // final：Started→Stopped，先 OnShutdown 后 stopImpl
    void destroy();                           // final：任意状态安全销毁；若 Started 先 stop；状态回 Created

    ServiceState state() const;
    const std::string& name() const;
    void setName(const std::string& name);

protected:
    virtual bool OnInit(int argc, char** argv);   // 用户钩子，默认 true
    virtual void OnShutdown();                    // 用户钩子，默认空
    virtual bool startImpl();                     // 内部扩展点（ServiceTask 覆写），默认 true
    virtual void stopImpl();                      // 内部扩展点（ServiceTask 覆写），默认空

    std::string name_;
    ServiceState state_{ServiceState::Created};
};

}  // namespace ssn

#endif  // SSN_FRAMEWORK_SERVICEBASE_HPP
