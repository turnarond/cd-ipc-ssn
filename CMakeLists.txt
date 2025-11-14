cmake_minimum_required(VERSION 3.12)

project(lwipcssn)

set(CMAKE_CXX_STANDARD 11)

include(../CMakeCommonLib)

include_directories(
    ${LW_INCLUDE_DIR}
    ${LW_INCLUDE_DIR}/libevent
    ./
)

link_directories(
    ${LW_LIB_DIR}
)

add_library(${PROJECT_NAME} SHARED
    ipc_client.c
    ipc_server.c
    ipc_parser.c
    ipc_cliauto.c
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
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/ipc_parser.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/ipc_cliauto.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
