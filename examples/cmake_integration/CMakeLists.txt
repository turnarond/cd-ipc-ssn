# 最小集成示例：通过 find_package(ssn) 消费安装后的 ssn 库
#
# 前置：仓库已构建并安装（任一前缀，如 /tmp/ssn-install）：
#   cmake --install build --prefix /tmp/ssn-install
#
# 消费（以本目录为例）：
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=/tmp/ssn-install
#   cmake --build build && ./build/hello_ssn

cmake_minimum_required(VERSION 3.12)
project(ssn_consumer C CXX)

# 通过 find_package 引入 ssn::ssn_transport 与 ssn::ssn_framework
find_package(ssn REQUIRED)

add_executable(hello_ssn main.c)
target_link_libraries(hello_ssn PRIVATE ssn::ssn_transport)

# C++ 服务框架消费示例（若只需 C 库可去掉）
add_executable(hello_framework main.cpp)
target_link_libraries(hello_framework PRIVATE ssn::ssn_framework)
