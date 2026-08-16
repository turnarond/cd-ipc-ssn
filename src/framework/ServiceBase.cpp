/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 服务生命周期基类实现
 */
// 文件: src/framework/ServiceBase.cpp
// 功能: ServiceBase 生命周期基类实现——状态机切换与钩子调用顺序
//       在此统一编排，非法状态转移一律拒绝（返回 false 或直接返回）。
#include "ssn/framework/ServiceBase.hpp"

namespace ssn {

ServiceBase::ServiceBase() = default;
ServiceBase::~ServiceBase() = default;

bool ServiceBase::initialize(int argc, char** argv) {
    if (state_ != ServiceState::Created) { return false; }
    if (!OnInit(argc, argv)) { return false; }   // 失败保持 Created
    state_ = ServiceState::Initialized;
    return true;
}

bool ServiceBase::start() {
    if (state_ != ServiceState::Initialized) { return false; }
    if (!startImpl()) { return false; }          // 失败回 Initialized
    state_ = ServiceState::Started;
    return true;
}

void ServiceBase::stop() {
    if (state_ != ServiceState::Started) { return; }
    OnShutdown();
    stopImpl();
    state_ = ServiceState::Stopped;
}

void ServiceBase::destroy() {
    if (state_ == ServiceState::Started) { stop(); }  // 先走正常停机，保证 OnShutdown/stopImpl 被调用
    state_ = ServiceState::Created;                   // 任意状态最终归位 Created，允许重新初始化
}

ServiceState ServiceBase::state() const {
    return state_;
}

const std::string& ServiceBase::name() const {
    return name_;
}

void ServiceBase::setName(const std::string& name) {
    name_ = name;
}

bool ServiceBase::OnInit(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return true;   // 默认成功，派生类按需覆写
}

void ServiceBase::OnShutdown() {
}

bool ServiceBase::startImpl() {
    return true;   // 默认成功，ServiceTask 覆写为线程池启动
}

void ServiceBase::stopImpl() {
}

}  // namespace ssn
