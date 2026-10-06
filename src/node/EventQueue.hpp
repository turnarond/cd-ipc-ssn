#ifndef SSN_NODE_EVENT_QUEUE_HPP
#define SSN_NODE_EVENT_QUEUE_HPP

#include "ssn/node/Types.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace ssn::detail {

struct QueuedEvent final {
    NodeEvent event;
    Message message;
};

class EventQueue final {
public:
    Status push(NodeEvent event);
    std::vector<QueuedEvent> take(std::chrono::milliseconds timeout,
                                  std::size_t limit);
    void interrupt();
    // 是否还有已入队未派发的事件（stop 冲刷的完成判定用）
    bool empty() const;

    // 入队后回调（在队列锁外调用）：用于唤醒正在 pselect 等待的事件线程。
    // 只在单线程构造期设置，不参与并发写。
    void setOnPush(std::function<void()> handler);

private:
    mutable std::mutex mutex_; // mutable：const empty() 查询也需加锁
    std::condition_variable ready_;
    std::deque<QueuedEvent> events_;
    bool interrupted_{false};
    std::function<void()> on_push_;
};
}

#endif  // SSN_NODE_EVENT_QUEUE_HPP
