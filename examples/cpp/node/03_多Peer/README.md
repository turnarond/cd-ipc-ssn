# 示例：三 Node 多 Peer 拓扑与广播

hub 节点同时监听两个 TCP 地址，leafA/leafB 各连其一；hub 广播一条消息
（全部 Peer 各收一份），叶子再定向回复 hub——演示多监听、多 Peer 注册、
`broadcast()` 与定向 `send()` 的区别。

## 运行

```bash
make run
```

单进程三节点拓扑（`tcp://127.0.0.1:19503` 与 `tcp://127.0.0.1:19504`）。

## 预期输出要点

- `[hub] 监听 tcp://127.0.0.1:19503 与 tcp://127.0.0.1:19504`
- `[hub] 已广播给 2 个 Peer: 广播：会议 10 点开始`
- `[leafA] 收到广播: …` / `[leafB] 收到广播: …`
- `[hub] 收到叶子回复: leafA 收到` / `leafB 收到`
- `OK: 多 Peer 拓扑演示完成`

## 关键约束

- 同一 Node 可多次 `listen()` 实现多地址监听；`peers()` 返回当前全部 Peer 快照。
- `broadcast()` 逐 Peer 入队投递；定向 `send()` 需持有有效的 `PeerId`
  （PeerId 含代际，连接断开后过期 ID 不会命中新连接）。
