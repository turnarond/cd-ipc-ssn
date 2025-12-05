cmake_minimum_required(VERSION 3.12)

project(lwipcssn)

set(CMAKE_CXX_STANDARD 11)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(IPC_LOG_LEVEL=0)   # DEBUG
else()
    add_definitions(NDEBUG)
endif()

include_directories(
    ${LW_INCLUDE_DIR}
    ${LW_INCLUDE_DIR}/libevent
    ./
)

link_directories(
    ${LW_LIB_DIR}
)

add_library(${PROJECT_NAME} SHARED
    src/vsi/ipc_event.c
    src/vsi/ipc_mutex.c
    src/vsi/ipc_platform.c
    src/vsi/ipc_socket.c
    src/vsi/ipc_thread.c
    src/util/ipc_log.c
    src/ipc_global.c
    src/ipc_client.c
    src/ipc_server.c
    src/ipc_protocol.c
)

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)

## Mark cpp header files for installation
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/ipc_client.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/ipc_server.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/ipc_protocol.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/ipc_cliauto.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
