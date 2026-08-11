// 文件: src/framework/ServiceManager.cpp
// 功能: ServiceManager 非模板成员实现——停止标志的静态存储与访问接口
//       （Run<T> 为模板，实现位于头文件 ServiceManager.hpp）。
#include "ssn/framework/ServiceManager.hpp"

namespace ssn {

std::atomic<bool> ServiceManager::s_stop_requested_{false};

void ServiceManager::requestStop() {
    s_stop_requested_ = true;
}

bool ServiceManager::stopRequested() {
    return s_stop_requested_;
}

}  // namespace ssn
