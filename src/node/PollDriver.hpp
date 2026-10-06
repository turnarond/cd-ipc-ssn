#ifndef SSN_NODE_POLL_DRIVER_HPP
#define SSN_NODE_POLL_DRIVER_HPP

#include "ssn/node/Status.hpp"

#include <chrono>
#include <vector>

namespace ssn::detail {

// 聚合轮询：把本轮关注的 fd 快照与唤醒句柄放进同一次 pselect，
// 总等待时长不超过传入 timeout；就绪结果转为内部值，不直接调用用户回调。
// 私有实现：不安装、不导出。
class PollDriver final {
public:
    struct Ready final {
        std::vector<int> readable;
        std::vector<int> writable;
        bool woken{false};
    };

    PollDriver() noexcept;
    ~PollDriver() noexcept;
    PollDriver(const PollDriver&) = delete;
    PollDriver& operator=(const PollDriver&) = delete;

    bool valid() const noexcept;
    // 唤醒读端必须能参与 pselect：fd 超出 FD_SETSIZE 时聚合轮询无法承载，
    // 静默退化会让后台线程长眠无法被 stop() 唤醒（评审 C1）。
    bool usable() const noexcept;

    Status pollOnce(const std::vector<int>& watch_read,
                    const std::vector<int>& watch_write,
                    std::chrono::milliseconds timeout,
                    Ready& out) noexcept;

    // 跨线程唤醒正在等待的 poll（本线程也可调用；未等待时信号保留）
    void wake() noexcept;

private:
    // 自持唤醒管道：读端参与 pselect，写端用于跨线程唤醒。
    // 不复用 VSI 的 ipc_event_pair——那是隐藏符号的内部平台层，
    // 为 Node 导出它会扩大 VSI 的符号面。
    int read_fd_{-1};
    int write_fd_{-1};
};

}  // namespace ssn::detail

#endif  // SSN_NODE_POLL_DRIVER_HPP
