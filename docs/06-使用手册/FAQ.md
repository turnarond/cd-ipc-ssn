# SSN 常见问题（FAQ）

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 状态 | 有效 |
| 更新日期 | 2026-08-09 |
| 适用范围 | 使用 SSN 的开发者与部署人员 |
| 相关文档 | [使用指南](使用指南.md)、[API 使用指南](API使用指南.md)、[部署手册](../05-部署手册/部署手册.md)、[部署场景指南](../05-部署手册/部署场景指南.md) |

本文档汇总 SSN 使用中的高频问题，按类别分组，每个问题按「问题 → 原因 → 解决方案」组织，并指向相关文档或代码位置。内容与当前版本（v2.4.1）代码行为保持一致。

## 一、构建与安装

### Q1. 在 Windows 上无法构建，怎么办？

**问题**：在 Windows 上执行 `cmake`/`make` 构建失败或产物无法使用。

**原因**：SSN 仅支持 Linux/POSIX 系统，产物为动态库 `libssn_transport.so`，不支持 Windows 原生构建。

**解决方案**：使用 WSL 作为 Linux 构建环境。仓库位于 Windows（如 `D:\personal\cd-ipc-ssn`），在 WSL 中的挂载路径为 `/mnt/d/personal/cd-ipc-ssn`，一键验证命令：

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh"
```

注意 `/mnt/d` 为 Windows 挂载点，I/O 较慢，建议 `make -j` 控制并行度；若性能敏感，可将仓库拷贝到 WSL 本地文件系统后构建。详见 [部署手册](../05-部署手册/部署手册.md)「4.2 WSL 构建路径」。

### Q2. 运行时提示找不到 libssn_transport.so

**问题**：编译链接成功，但运行时报错 `error while loading shared libraries: libssn_transport.so: cannot open shared object file`。

**原因**：动态库搜索路径未包含库所在目录。

**解决方案**：仓库内示例与测试程序已内置 rpath（`-Wl,-rpath,$(TOP_DIR)/build`）可直接运行，无需额外配置；若将库安装/拷贝到自定义目录，请设置：

```bash
export LD_LIBRARY_PATH=<库所在目录>:$LD_LIBRARY_PATH
```

或在编译时添加 `-Wl,-rpath,<库所在目录>`。详见 [部署手册](../05-部署手册/部署手册.md)「4.1 运行时找不到 libssn_transport.so」。

### Q3. CMake 报版本过低 / `cmake --install` 不支持

**问题**：构建时提示 CMake 版本低于要求，或 `cmake --install` 无法使用。

**原因**：SSN 要求 CMake ≥ 3.12（编译器 GCC ≥ 4.8 或 Clang ≥ 3.0）；`cmake --install` 命令需 CMake ≥ 3.15。

**解决方案**：升级 CMake 至 3.12 以上；CMake ≥ 3.12 均可使用 `cd build && make install` 安装，只有指定安装前缀的 `cmake --install . --prefix <路径>` 需要 3.15 以上。环境要求详见 [部署手册](../05-部署手册/部署手册.md)「1. 环境要求」与「3.1 安装」。

## 二、连接与通信

### Q4. 客户端连接失败

**问题**：`ssn_client_connect` 返回 false，客户端无法连接服务器。

**原因**：最常见的原因是地址格式错误。地址必须采用 `协议://地址` 完整格式（`ssn_address_parse` 要求地址含 `://`），例如 `unix:///tmp/my_server`、`tcp://127.0.0.1:8080`、`udp://127.0.0.1:9090`。仅服务端创建时（`ssn_server_create`）对不带前缀的裸路径会自动补全 `unix://`，**客户端连接地址必须写全前缀**。此外也可能是服务器未运行、路径权限不足或防火墙拦截。

**解决方案**：

- 检查地址格式：客户端始终使用完整 `协议://` 形式（见 [使用指南](使用指南.md)「3.1 传输协议」、[API 使用指南](API使用指南.md)「地址格式」）；
- 确认服务器已启动并监听（`ssn_server_start` 返回 true）；
- 检查 socket 文件权限（Unix）、端口占用与防火墙（TCP/UDP）；
- 错误码可用 `ssn_ecode_message()` 转换为可读描述辅助定位（[使用指南](使用指南.md)「6. 错误处理」）。

### Q5. 服务器收不到消息 / 连接握手不完成

**问题**：客户端已发送消息，但服务器端的消息回调不触发；或客户端连接一直不成功（connect 卡住后超时）。

**原因**：SSN 的事件循环**必须由应用驱动**，库不自动运行 poll 线程。服务端的连接建立、握手与消息分发全部发生在 `ssn_server_poll` 的轮询中；服务端的全局定时器线程（50ms 周期）只负责 idle/握手超时的计时，实际处理仍需 poll 循环执行。不调用 poll，连接与消息都不会被处理。

**解决方案**：启动后在主循环或独立线程中周期调用 `ssn_server_poll(server, timeout_ms)`（timeout 为毫秒），或使用阻塞式 `ssn_server_run(server)`；客户端同理使用 `ssn_client_poll`/`ssn_client_run`。示例见 [使用指南](使用指南.md)「5.1 基础通信」、[快速上手](快速上手.md)「1.3 第一个程序（server + client）」；行为基线详见 [部署场景指南](../05-部署手册/部署场景指南.md)「4.2 事件循环驱动」。

### Q6. 基于 UDP 的服务器无法建立连接

**问题**：使用 `ssn_server_create("udp://...")` 创建服务器后，客户端连接不成功，连接/断开回调不触发。

**原因**：UDP 为无连接传输，`udp_transport_accept` 恒返回 NULL，**不支持 server 模式握手**，`ssn_server` 无法运行于 UDP 之上（框架设计限制，非缺陷）。

**解决方案**：服务器场景请改用 `unix://` 或 `tcp://`；UDP 仅适用于客户端模式点对点收发（示例 `examples/protocols/03_udp` 的 server 端仅为演示 bind/recv 流程）。详见 [部署手册](../05-部署手册/部署手册.md)「4.3 UDP server 握手限制」与 [传输层设计](../03-设计/架构设计/传输层设计.md)「UDP 传输」章节的限制标注。

## 三、消息与订阅

### Q7. 收不到订阅消息

**问题**：发布端 `ssn_server_publish` 已发布，但订阅端回调不触发。

**原因**：常见原因之一是 `url_len` 语义用错。`ssn_url_ref_t.url_len` 与 `ssn_data_ref_t.length` 均为 **strlen 语义（不含 NUL 终止符）**，与代码 `ssn_get_url`/`ssn_get_data` 的解析行为一致；若误用 `sizeof(url_buf)`（包含 `\0` 或数组容量），主题路径将不匹配，导致订阅/发布失效。其次，订阅主题与发布主题必须完全一致（同一路径字符串），且订阅必须在发布前完成；若订阅失败（`ssn_client_subscribe` 返回 false）应优先排查。

**解决方案**：

- `url_len` 一律取 `strlen(url)` 的值（如 `"/news"` 为 5），见 [使用指南](使用指南.md)「3.4 数据与 URL 引用」与 [API 使用指南](API使用指南.md)「示例代码」；
- 检查 `ssn_client_subscribe` 返回值与主题路径一致性（[使用指南](使用指南.md)「8.3 消息丢失」）；
- 确保客户端事件循环已驱动（见 Q5）；
- 对关键消息考虑改用 RPC 以获得应答确认。

### Q8. 回调里保存的指针，回调返回后数据变了

**问题**：在消息/RPC 回调中把 `url->url`、`data->data` 指针保存到全局或长期存储，之后读取内容错误或崩溃。

**原因**：回调中的 `url`/`data` 指向库内部的临时缓冲，**仅在回调返回前有效**，回调返回后即失效（可能被复用或释放）。

**解决方案**：需要长期使用的数据必须在回调内复制（如 `malloc` + `memcpy`），并自行管理释放。见 [使用指南](使用指南.md)「3.4 数据与 URL 引用」「7.3 内存管理」与 [API 使用指南](API使用指南.md)「注意事项 3. 内存管理」；线程安全语义详见 [线程安全设计](../03-设计/核心模块/线程安全设计.md)。

## 四、超时与线程

### Q9. 超时设置不生效或时间不对

**问题**：设置的超时与实际行为不符，如 poll 阻塞时间明显过长/过短。

**原因**：各 API 的超时单位不同，容易混淆：

- `ssn_client_poll(client, timeout_ms)`、`ssn_server_poll(server, timeout_ms)`、`ssn_client_call`/`ssn_client_subscribe` 的 `timeout_ms` 均为**毫秒**；
- `ssn_client_connect` 的 `timeout` 为 `struct timespec`（秒 + 纳秒）。

另外 v2.3.0 及更早版本存在 `ssn_client_poll` 毫秒超时换算错误（已在 v2.3.1 修复），请确认使用的版本。此外服务端 idle 超时的实际断开仍需 poll 循环活跃（见 Q5）；超时值建议不小于 20ms，RPC 调用超时建议不小于 100ms。

**解决方案**：确认版本 ≥ 2.3.1（`ssn_version_get_string()`）；核对各 API 超时单位（毫秒 vs. timespec 秒）；遵循最小超时建议；服务端 idle 超时通过 `server_options_t.idle_timeout_sec` 配置（默认 10 秒，≤0 禁用），详见 [部署场景指南](../05-部署手册/部署场景指南.md)「4.3 idle / keepalive 行为说明」、[API 使用指南](API使用指南.md)「注意事项 2. 超时设置」。

### Q10. 在回调中调用 close/destroy 导致崩溃或卡死

**问题**：在消息回调、RPC 回调或连接回调中调用 `ssn_client_close` / `ssn_server_destroy`，程序崩溃、死锁或行为异常。

**原因**：回调可能在其他线程执行（客户端回调线程、服务端轮询线程等），回调上下文中销毁对象会破坏库内部状态（引用计数、锁、事件循环资源），属于未定义行为。

**解决方案**：回调中**不得直接调用 `ssn_client_close` / `ssn_server_destroy`**（也不得调用 `ssn_client_auto_*` 系列中除 `ssn_client_auto_handle` 外的函数）；应置退出标志，由主流程退出事件循环后统一销毁。销毁顺序：服务端退出 poll 循环 → `ssn_server_destroy`；客户端 `ssn_client_disconnect` → `ssn_client_close`；节点 `ssn_node_stop` → `ssn_node_destroy`。详见 [线程安全设计](../03-设计/核心模块/线程安全设计.md) 与 [部署场景指南](../05-部署手册/部署场景指南.md)「4.4 优雅退出」。

### Q11. 多线程同时 poll 同一个 server / client

**问题**：多个线程同时对同一个 `ssn_server_t`/`ssn_client_t` 实例调用 poll，出现丢包、崩溃或数据竞争。

**原因**：SSN 基于**单线程事件循环模型**设计，`poll` 承担连接处理与消息分发，并发调用同一实例没有内部锁保护，不被支持。

**解决方案**：在同一线程中创建、轮询和关闭客户端/服务器对象；发送类 API（如 `ssn_client_message`、`ssn_client_call`）可在其他线程调用，但跨线程访问同一对象时需自行加锁同步。多个对端建议使用多个客户端实例，或使用节点抽象（`ssn_node_t`）统一管理。详见 [使用指南](使用指南.md)「9.1 多线程通信」与 [API 使用指南](API使用指南.md)「注意事项 1. 线程安全」。

## 版本历史

| 版本 | 日期 | 主要变化 |
|------|------|----------|
| v1.0 | 2026-08-09 | 初始版本：按「构建与安装 / 连接与通信 / 消息与订阅 / 超时与线程」四类整理 11 条高频问题 |
