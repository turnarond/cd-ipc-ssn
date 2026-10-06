# 示例：ssn::Node 生命周期（RAII）

多 Peer 节点的完整生命周期演示：创建 → 监听 → 后台事件线程自驱动 →
出站连接 → 消息收发 → 优雅停止 → RAII 兜底析构。

## 运行

```bash
make run
```

单进程内启动 hub 与 client 两个节点（`tcp://127.0.0.1:19501`），无需外部服务端。

## 预期输出要点

- `[hub] 监听 tcp://127.0.0.1:19501（后台事件线程自驱动）`
- `[hub] peer 已连入` / `[client] 已连接 hub`
- `[hub] 收到: 你好，SSN`
- `OK: 生命周期演示完成`

## 关键约束

- `listen()` 必须处于 `Created` 态（先 listen 再 `startBackground()`）。
- `connect()` 的 InProgress 路径立即返回 `Connecting` 态 PeerId，直接 send 会被
  拒绝——须轮询 `peerInfo()` 直到 `PeerState::Connected`（见 `wait_connected`）。
- 不得在事件回调内析构仍在运行的 Node；正常收尾用 `stop()` + `waitStopped()`，
  RAII（`std::unique_ptr`）作为兜底。
