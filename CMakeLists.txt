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
)

set(IPC_SOURCES
    src/cd_ipc_client.c
    src/cd_ipc_server.c
    src/cd_ipc_protocol.c
    src/cd_ipc_cliauto.c
    src/cd_ipc_global.c
    src/cd_ipc_error.c
    src/node/ipc_node.c
    src/node/ipc_node_comm.c
    src/vsi/ipc_platform.c
    src/vsi/ipc_socket.c
    src/vsi/ipc_event.c
    src/vsi/ipc_thread.c
    src/vsi/ipc_mutex.c
)

add_library(${PROJECT_NAME} STATIC
    ${TRANSPORT_SOURCES}
    ${IPC_SOURCES}
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} pthread)

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
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

install(FILES src/cd_ipc_client.h
    DESTINATION include
)

install(FILES src/cd_ipc_server.h
    DESTINATION include
)

install(FILES src/cd_ipc_protocol.h
    DESTINATION include
)

install(FILES src/cd_ipc_cliauto.h
    DESTINATION include
)

add_executable(test_transport test/test_transport.c)
target_link_libraries(test_transport ${PROJECT_NAME} pthread)

add_executable(test_transport_interface test_transport_interface.c)
target_link_libraries(test_transport_interface ${PROJECT_NAME} pthread)

add_executable(test_node test/test_node.c)
target_link_libraries(test_node ${PROJECT_NAME} pthread)

add_executable(test_node_basic test/test_node_basic.c)
target_link_libraries(test_node_basic ${PROJECT_NAME} pthread)
