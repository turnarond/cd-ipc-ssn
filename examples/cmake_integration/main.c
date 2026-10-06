/*
 * main.c - find_package(ssn) 集成最小示例（C 库）
 *
 * 展示：安装后通过 ssn::ssn_transport 目标消费 C API，
 * 打印库版本并做一次帧协议冒烟。
 * （旧 Node C API 已于 v3.0.0 移除，Node 能力迁移至 C++ ssn::Node，
 *  见 main.cpp / examples/cpp/node/）
 */

#include <stdio.h>
#include <string.h>

#include "ssn_frame.h"
#include "version/ssn_version.h"

int main(void)
{
    printf("ssn version: %s\n", ssn_version_get_string());

    /* 帧协议冒烟（纯传输层 C API） */
    ssn_stream_ctx_t stream;
    memset(&stream, 0, sizeof(stream));
    ssn_stream_init(&stream);
    printf("OK: find_package(ssn) C 集成可用\n");
    return 0;
}
