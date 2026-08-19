cmake_minimum_required(VERSION 3.12)

project(ssn_transport LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)

set(TRANSPORT_SOURCES
    src/util/ssn_log.c
    src/util/ssn_mutex.c
    src/util/ssn_hash_table.c
    src/version/ssn_version.c
    src/transports/ssn_transport.c
    src/transports/ssn_transport_unix.c
    src/transports/ssn_transport_tcp.c
    src/transports/ssn_transport_udp.c
    src/transports/ssn_transport_factory.c
    src/protocol/ssn_protocol.c
    src/protocol/rpc/ssn_rpc.c
    src/protocol/pubsub/ssn_pubsub.c
    src/protocol/msg/ssn_msg.c
)

set(IPC_SOURCES
    src/ssn_client.c
    src/ssn_server.c
    src/ssn_frame.c
    src/ssn_cliauto.c
    src/ssn_global.c
    src/ssn_error.c
    src/node/ssn_node.c
    src/node/ssn_node_comm.c
    src/vsi/ipc_platform.c
    src/vsi/ipc_socket.c
    src/vsi/ipc_event.c
    src/vsi/ipc_thread.c
    src/vsi/ipc_mutex.c
)

# 版本信息
set(VERSION_MAJOR 2)
set(VERSION_MINOR 5)
set(VERSION_PATCH 0)
set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

# 创建动态库
add_library(${PROJECT_NAME} SHARED
    ${TRANSPORT_SOURCES}
    ${IPC_SOURCES}
)

# 设置动态库版本信息
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${VERSION_STRING}
    SOVERSION ${VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)

# 定义构建库时的宏
target_compile_definitions(${PROJECT_NAME} PRIVATE SSN_BUILDING_LIBRARY=1)

# include 目录区分构建期与安装期：源码树内用 src/，安装后消费者以
# <prefix>/include 为根（相对引用链 node/、transports/、util/、vsi/ 均随安装分发）
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} pthread)

# 导出目标（find_package(ssn) 使用）：ssn::ssn_transport / ssn::ssn_framework
install(TARGETS ${PROJECT_NAME}
    EXPORT ssnTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(EXPORT ssnTargets
    FILE ssnTargets.cmake
    NAMESPACE ssn::
    DESTINATION lib/cmake/ssn
)

# 包配置文件：find_package(ssn) 入口
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ssnConfigVersion.cmake"
    VERSION ${VERSION_STRING}
    COMPATIBILITY SameMajorVersion
)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ssnConfig.cmake"
"include(CMakeFindDependencyMacro)
include(\"\${CMAKE_CURRENT_LIST_DIR}/ssnTargets.cmake\")
")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ssnConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ssnConfigVersion.cmake"
    DESTINATION lib/cmake/ssn
)

install(FILES src/ssn_export.h
    DESTINATION include
)

# 公共头以源码相对路径互相引用（如 ssn_frame.h → "transports/ssn_transport.h"、
# ssn_transport.h → "util/ssn_log.h"），安装时同时保留顶层扁平路径（既有
# 文档约定，向后兼容）与镜像源码树的子目录路径（相对引用成立的前提）
install(FILES src/version/ssn_version.h
    DESTINATION include
)
install(FILES src/version/ssn_version.h
    DESTINATION include/version
)

install(FILES src/transports/ssn_transport.h
    DESTINATION include
)
install(FILES src/transports/ssn_transport.h
    DESTINATION include/transports
)

install(FILES src/util/ssn_log.h
    DESTINATION include
)
install(FILES src/util/ssn_log.h
    DESTINATION include/util
)

install(FILES src/util/ssn_mutex.h
    DESTINATION include
)
install(FILES src/util/ssn_mutex.h
    DESTINATION include/util
)

install(FILES src/util/ssn_hash_table.h
    DESTINATION include
)
install(FILES src/util/ssn_hash_table.h
    DESTINATION include/util
)

install(FILES src/ssn_client.h
    DESTINATION include
)

install(FILES src/ssn_server.h
    DESTINATION include
)

install(FILES src/ssn_frame.h
    DESTINATION include
)

install(FILES src/ssn_cliauto.h
    DESTINATION include
)

# ssn_node.h 内部以 "../ssn_client.h" 等相对引用包含顶层头文件，
# 安装到 include/node/ 后相对引用成立（include/ssn_client.h 等已在顶层）
install(FILES src/node/ssn_node.h
    DESTINATION include/node
)

# 以下头文件构成已安装头文件的完整引用链（缺一则安装后无法编译）：
# ssn_node.h → ssn_error.h；ssn_client.h → ssn_global.h → vsi/ipc_platform.h
install(FILES src/ssn_error.h
    DESTINATION include
)
install(FILES src/ssn_global.h
    DESTINATION include
)
install(FILES src/vsi/ipc_platform.h
    DESTINATION include/vsi
)

# ---- C++ 服务框架（v2.4.0，独立库，链接 ssn_transport）----
add_library(ssn_framework SHARED
    src/framework/ServiceBase.cpp
    src/framework/ServiceTask.cpp
    src/framework/ServiceManager.cpp
    src/framework/SsnService.cpp
    src/framework/SsnClient.cpp
)

target_compile_features(ssn_framework PUBLIC cxx_std_17)
target_include_directories(ssn_framework PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(ssn_framework ssn_transport pthread)

# 框架库构建期定义导出宏（CXX_VISIBILITY_PRESET hidden 下公开类靠此导出）
target_compile_definitions(ssn_framework PRIVATE SSN_BUILDING_FRAMEWORK=1)

# 版本化 SONAME 与符号可见性（缺陷背景：原无 VERSION/SOVERSION，升级时新库
# 直接覆盖旧文件无 ABI 保护；缺 CXX_VISIBILITY_PRESET 使 C++ 符号全量导出——
# 与 ssn_transport 的治理策略对齐）
set_target_properties(ssn_framework PROPERTIES
    VERSION ${VERSION_STRING}
    SOVERSION ${VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)

install(TARGETS ssn_framework
    EXPORT ssnTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ssn/framework/
    DESTINATION include/ssn/framework
)

# nlohmann/json 为单头文件 vendor（框架公共头依赖 <nlohmann/json.hpp>），随安装分发
install(FILES third_party/nlohmann/json.hpp
    DESTINATION include/nlohmann
)

add_executable(test_transport test/test_transport.c)
target_link_libraries(test_transport ${PROJECT_NAME} pthread)

add_executable(test_node test/test_node.c)
target_link_libraries(test_node ${PROJECT_NAME} pthread)

add_executable(test_node_basic test/test_node_basic.c)
target_link_libraries(test_node_basic ${PROJECT_NAME} pthread)

add_executable(test_protocol test/test_protocol.c)
target_link_libraries(test_protocol ${PROJECT_NAME} pthread)

add_executable(test_protocol_integration test/test_protocol_integration.c)
target_link_libraries(test_protocol_integration ${PROJECT_NAME} pthread)

# Example applications (interactive, run with manual server)
add_executable(example_server test/test_ssn_server.c)
target_link_libraries(example_server ${PROJECT_NAME} pthread)

add_executable(example_client test/test_ssn_client.c)
target_link_libraries(example_client ${PROJECT_NAME} pthread)

# 自动重连客户端（cliauto）单元测试
add_executable(test_cliauto test/test_cliauto.c)
target_link_libraries(test_cliauto ${PROJECT_NAME} pthread)

# 哈希表单元测试（含字符串键 API 回归）
add_executable(test_hash_table test/test_hash_table.c)
target_link_libraries(test_hash_table ${PROJECT_NAME} pthread)

# C++ 服务框架测试（批次 2 任务 2：ServiceBase 生命周期）
add_executable(test_cpp_service_base test/test_cpp_service_base.cpp)
target_link_libraries(test_cpp_service_base ssn_framework pthread)

# C++ 服务框架测试（批次 2 任务 3：ServiceTask 线程池基类）
add_executable(test_cpp_service_task test/test_cpp_service_task.cpp)
target_link_libraries(test_cpp_service_task ssn_framework pthread)

# C++ 服务框架测试（批次 2 任务 4：ServiceManager 编排与信号停止）
add_executable(test_cpp_service_manager test/test_cpp_service_manager.cpp)
target_link_libraries(test_cpp_service_manager ssn_framework pthread)

# C++ 服务框架测试（批次 2 任务 5：SsnService 通信服务基类 + 内置端点）
add_executable(test_cpp_ssn_service test/test_cpp_ssn_service.cpp)
target_link_libraries(test_cpp_ssn_service ssn_framework pthread)

# C++ 服务框架测试（批次 2 任务 6：SsnClient 客户端——同步 callJson + 订阅）
add_executable(test_cpp_ssn_client test/test_cpp_ssn_client.cpp)
target_link_libraries(test_cpp_ssn_client ssn_framework pthread)

# C++ 服务框架测试（批次 2 任务 7：类型安全层——RegisterMethod/Call + DTO 全链路）
add_executable(test_cpp_json test/test_cpp_json.cpp)
target_link_libraries(test_cpp_json ssn_framework pthread)

# C++ 服务框架测试（稳定性加固批次：T1-T13 回调异常/并发/超时风暴/生命周期泄漏/信号风暴）
add_executable(test_cpp_stability test/test_cpp_stability.cpp)
target_link_libraries(test_cpp_stability ssn_framework pthread)

# Advanced tests (require running server, use for manual/CI testing)
add_executable(test_comprehensive test/test_ssn_comprehensive.c)
target_link_libraries(test_comprehensive ${PROJECT_NAME} pthread)

add_executable(test_thread_safety test/test_ssn_thread_safety.c)
target_link_libraries(test_thread_safety ${PROJECT_NAME} pthread)

add_executable(test_stress test/test_ssn_stress.c)
target_link_libraries(test_stress ${PROJECT_NAME} pthread)
