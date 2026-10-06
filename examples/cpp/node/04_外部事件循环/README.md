# 示例：外部事件循环

外部 `poll()` 与 `startBackground()` 是二选一的生命周期约定。本示例中服务
节点不调用 `startBackground()`，由用户主循环周期性调用 `poll(10ms)` 驱动
accept、收包与回复；客户端节点使用后台模式——两种驱动方式在同一进程内各自
独立工作。

## 运行

```bash
make run
```

单进程两节点（`tcp://127.0.0.1:19505`）。

## 预期输出要点

- `[client] 已发送: 任务-1/2/3`
- `[server] 外部循环处理: 任务-N`（由用户主循环的 `poll()` 驱动）
- `[client] 收到: 已处理: 任务-N`
- `OK: 外部事件循环演示完成（poll 与 startBackground 二选一）`

## 关键约束

- 同一 Node 生命周期内 `poll()` 与 `startBackground()` 不得混用；首次成功
  调用后即锁定驱动方式。
- 外部模式收尾：主循环退出后调用 `stop()` + `waitStopped()`。
- 回调内 `send()` 安全；`poll()`/`waitStopped()` 不得在回调内调用。
