/*
 * main.c - find_package(ssn) 集成最小示例（C 库）
 *
 * 展示：安装后通过 ssn::ssn_transport 目标消费 C API，
 * 打印库版本并跑一次节点生命周期。
 */

#include <stdio.h>
#include <string.h>

#include "node/ssn_node.h"
#include "version/ssn_version.h"

int main(void)
{
    printf("ssn version: %s\n", ssn_version_get_string());

    /* 节点生命周期（C API 冒烟） */
    ssn_node_config_t cfg = {
        .node_type = "consumer",
        .node_name = "cmake-consumer",
        .capabilities = SSN_NODE_CAP_CLIENT
    };
    ssn_node_t *node = ssn_node_create(&cfg);
    if (!node) {
        fprintf(stderr, "FAIL: ssn_node_create\n");
        return 1;
    }
    if (!ssn_node_start(node)) {
        fprintf(stderr, "FAIL: ssn_node_start\n");
        ssn_node_destroy(node);
        return 1;
    }
    printf("node started: %s\n", node->node_id);
    ssn_node_stop(node);
    ssn_node_destroy(node);
    printf("OK: find_package(ssn) C 集成可用\n");
    return 0;
}
