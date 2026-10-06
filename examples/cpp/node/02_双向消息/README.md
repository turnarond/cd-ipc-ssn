# 示例：回调内双向消息

演示 `MessageReceived` 回调中的重入 `send`：应答方在事件回调里直接回发消息
（回调不持 Node 内部锁，重入 send 安全），请求方在另一节点收到回复——同一
进程内完成一问一答。

## 运行

```bash
make run
```

单进程内启动 responder 与 requester 两个节点（`tcp://127.0.0.1:19502`）。

## 预期输出要点

- `[responder] 收到: ping-1 → 回调内直接回发`
- `[requester] 收到回复: 回复: ping-1`（ping-2 同）
- `OK: 双向消息演示完成（回调内重入 send 安全）`

## 关键约束

- 回调执行期间不持有 Node 内部锁，可在回调内 `send()`；但不得调用会等待
  当前事件线程的操作（`poll()` 返回 `InvalidState`、`waitStopped()` 返回
  `WouldDeadlock`）。
- `MessageView` 只在回调期间有效，跨线程/跨回调使用须 `copy()`。
