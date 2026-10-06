#include "EventQueue.hpp"

#include <algorithm>
#include <new>

namespace ssn::detail {

Status EventQueue::push(NodeEvent event) {
    try {
        auto owned = event.message.copy();
        if (!owned) { return owned.status(); }
        QueuedEvent queued{std::move(event), std::move(owned).value()};
        queued.event.message = MessageView{queued.message.bytes()};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (interrupted_) { return Status::error(ErrorCode::InvalidState); }
            events_.push_back(std::move(queued));
        }
        ready_.notify_one();
        if (on_push_) { on_push_(); }
        return {};
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::ResourceLimit, "事件队列内存不足");
    }
}

void EventQueue::setOnPush(std::function<void()> handler) {
    on_push_ = std::move(handler);
}

std::vector<QueuedEvent> EventQueue::take(std::chrono::milliseconds timeout,
                                         std::size_t limit) {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait_for(lock, timeout, [&] { return interrupted_ || !events_.empty(); });
    std::vector<QueuedEvent> batch;
    if (interrupted_) {
        events_.clear();
        return batch;
    }
    const auto count = std::min(limit, events_.size());
    batch.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        batch.push_back(std::move(events_.front()));
        events_.pop_front();
    }
    return batch;
}

void EventQueue::interrupt() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        interrupted_ = true;
    }
    ready_.notify_all();
}

bool EventQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.empty();
}
}
