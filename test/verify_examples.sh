#!/bin/bash
# 验证全部 15 个示例构建（迁移后验证脚本）
cd /mnt/d/personal/cd-ipc-ssn || exit 1
ok=0
fail=0
for d in examples/basic/01_hello_world examples/basic/02_rpc_call examples/basic/03_pubsub \
         examples/basic/04_node_basic examples/advanced/01_multithread examples/advanced/02_error_handling \
         examples/advanced/03_timeout examples/advanced/04_transport_selection \
         examples/protocols/01_unix_socket examples/protocols/02_tcp examples/protocols/03_udp \
         examples/node/01_node_lifecycle examples/node/02_node_comm examples/node/03_node_rpc \
         examples/node/04_node_pubsub; do
    if (cd "$d" && make clean >/dev/null 2>&1 && make >/dev/null 2>&1); then
        ok=$((ok + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $d"
    fi
done
echo "RESULT: $ok ok, $fail failed"
[ "$fail" -eq 0 ]
