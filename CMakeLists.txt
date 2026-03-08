cmake_minimum_required(VERSION 3.12)

project(lwipcssn)

include(../../CMakeCommonLib)
set(CMAKE_CXX_STANDARD 11)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(IPC_LOG_LEVEL=0)   # DEBUG
else()
    add_definitions(-DNDEBUG)
endif()

include_directories(
    ${LW_INCLUDE_DIR}
    ${LW_INCLUDE_DIR}/libevent
    ./
    ./src
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
    src/ipc_error.c
    src/ipc_cliauto.c
)

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
)

## Mark cpp header files for installation
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/src/ipc_client.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/src/ipc_server.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/src/ipc_protocol.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/src/ipc_error.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/src/ipc_global.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/src/ipc_cliauto.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/src/vsi/ipc_platform.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}/vsi
)

# Build test files
add_executable(test_ipc_server test/test_ipc_server.c)
target_link_libraries(test_ipc_server ${PROJECT_NAME})

add_executable(test_ipc_client test/test_ipc_client.c)
target_link_libraries(test_ipc_client ${PROJECT_NAME})

add_executable(test_ipc_comprehensive test/test_ipc_comprehensive.c)
target_link_libraries(test_ipc_comprehensive ${PROJECT_NAME} pthread)
