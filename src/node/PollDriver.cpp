#include "PollDriver.hpp"

#include "util/ssn_log.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>

namespace ssn::detail {

namespace {
// 把管道两端都置为非阻塞：唤醒写入不得阻塞调用方，排空读取不得卡住事件线程
bool set_non_blocking(int fd) noexcept {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}
}

PollDriver::PollDriver() noexcept {
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) {
        LOG_ERROR("Node: 唤醒管道创建失败，poll 将无法被跨线程唤醒");
        return;
    }
    if (!set_non_blocking(fds[0]) || !set_non_blocking(fds[1])) {
        LOG_ERROR("Node: 唤醒管道置非阻塞失败: %s", strerror(errno));
        close(fds[0]);
        close(fds[1]);
        return;
    }
    read_fd_ = fds[0];
    write_fd_ = fds[1];
}

PollDriver::~PollDriver() noexcept {
    if (read_fd_ >= 0) { close(read_fd_); }
    if (write_fd_ >= 0) { close(write_fd_); }
    read_fd_ = -1;
    write_fd_ = -1;
}

bool PollDriver::valid() const noexcept {
    return read_fd_ >= 0 && write_fd_ >= 0;
}

bool PollDriver::usable() const noexcept {
    return valid() && read_fd_ < FD_SETSIZE;
}

void PollDriver::wake() noexcept {
    if (write_fd_ < 0) { return; }
    const char token = 1;
    // 管道满时说明已有待处理唤醒信号，丢弃本次即可（信号只用于唤醒，不计数）
    ssize_t written = write(write_fd_, &token, sizeof(token));
    (void)written;
}

Status PollDriver::pollOnce(const std::vector<int>& watch_read,
                            const std::vector<int>& watch_write,
                            std::chrono::milliseconds timeout,
                            Ready& out) noexcept {
    out = Ready{};

    if (timeout.count() < 0) {
        return Status::error(ErrorCode::InvalidArgument, "timeout 不得为负");
    }

    // 唤醒句柄不可聚合时必须快速失败：静默退化会使本轮 pselect 无法被
    // stop()/事件入队唤醒（后台模式最长挂死 24h，评审 C1）。
    if (!usable()) {
        return Status::error(ErrorCode::ResourceLimit, "唤醒管道不可聚合轮询");
    }

    fd_set read_set;
    fd_set write_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    int max_fd = -1;

    // 超出 FD_SETSIZE 的 fd 无法放进 fd_set：跳过并告警，避免越界写
    // （长期运行下 peer 数超过 1024 时宁可漏掉本轮，也不能踩内存）。
    auto add = [&](int fd, fd_set* set) {
        if (fd < 0) { return; }
        if (fd >= FD_SETSIZE) {
            LOG_WARN("Node: fd %d 超出 FD_SETSIZE(%d)，本轮跳过", fd, (int)FD_SETSIZE);
            return;
        }
        FD_SET(fd, set);
        if (fd > max_fd) { max_fd = fd; }
    };

    for (int fd : watch_read) { add(fd, &read_set); }
    for (int fd : watch_write) { add(fd, &write_set); }
    add(read_fd_, &read_set);

    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    ts.tv_nsec = static_cast<long>((timeout.count() % 1000) * 1000000L);

    const int ready = pselect(max_fd + 1, &read_set, &write_set, nullptr, &ts, nullptr);
    if (ready < 0) {
        if (errno == EINTR) {
            return {};
        }
        return Status::error(ErrorCode::IoError, "pselect 失败", errno);
    }

    if (FD_ISSET(read_fd_, &read_set)) {
        char token = 0;
        while (read(read_fd_, &token, sizeof(token)) > 0) { /* 排空 */ }
        out.woken = true;
    }

    for (int fd : watch_read) {
        if (fd >= 0 && fd < FD_SETSIZE && FD_ISSET(fd, &read_set)) {
            out.readable.push_back(fd);
        }
    }
    for (int fd : watch_write) {
        if (fd >= 0 && fd < FD_SETSIZE && FD_ISSET(fd, &write_set)) {
            out.writable.push_back(fd);
        }
    }

    return {};
}

}  // namespace ssn::detail
