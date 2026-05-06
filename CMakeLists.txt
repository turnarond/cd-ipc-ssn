cmake_minimum_required(VERSION 3.12)

project(ssn_transport)

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
set(VERSION_MINOR 0)
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

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} pthread)

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES src/ssn_export.h
    DESTINATION include
)

install(FILES src/version/ssn_version.h
    DESTINATION include
)

install(FILES src/transports/ssn_transport.h
    DESTINATION include
)

install(FILES src/util/ssn_log.h
    DESTINATION include
)

install(FILES src/util/ssn_mutex.h
    DESTINATION include
)

install(FILES src/util/ssn_hash_table.h
    DESTINATION include
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

# Advanced tests (require running server, use for manual/CI testing)
add_executable(test_comprehensive test/test_ssn_comprehensive.c)
target_link_libraries(test_comprehensive ${PROJECT_NAME} pthread)

add_executable(test_thread_safety test/test_ssn_thread_safety.c)
target_link_libraries(test_thread_safety ${PROJECT_NAME} pthread)

add_executable(test_stress test/test_ssn_stress.c)
target_link_libraries(test_stress ${PROJECT_NAME} pthread)
