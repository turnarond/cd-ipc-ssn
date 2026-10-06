#ifndef SSN_NODE_NODE_HPP
#define SSN_NODE_NODE_HPP

#include "ssn/node/Types.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace ssn {
namespace detail { struct NodeTestAccess; }

// 调用方必须保证 Node 对象在任何回调返回之前一直存活：不得在事件回调线程之上
// （回调内）析构或移动赋值一个仍在运行的 Node。回调内自析构属于契约违例；实现
// 检测到后会转为 stop + 后台线程 detach，并由事件线程收尾时延迟回收 Impl（避免
// terminate/死锁），同时记错误日志。该防御只控制爆炸半径，不解除上述存活义务。
//
// 导出标注必须逐成员写，不能整类标注 SSN_FRAMEWORK_API：类级标注会把可见性
// 传递给嵌套的私有 Impl，使 Node::Impl 的 11 个私有符号进入动态符号表
// （nm -D 实测），与「私有实现不导出」的治理要求冲突。
class Node final {
public:
    using EventHandler = std::function<void(const NodeEvent&)>;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    SSN_FRAMEWORK_API Node(Node&& other) noexcept;
    SSN_FRAMEWORK_API Node& operator=(Node&& other) noexcept;
    SSN_FRAMEWORK_API ~Node() noexcept;

    SSN_FRAMEWORK_API static Result<Node> create(const NodeConfig& config);
    SSN_FRAMEWORK_API Status listen(const ListenAddress& address);
    SSN_FRAMEWORK_API Result<PeerId> connect(
        const ListenAddress& address, const ConnectOptions& options = {});
    SSN_FRAMEWORK_API Status disconnect(PeerId peer);
    SSN_FRAMEWORK_API Status send(PeerId peer, ByteView bytes);
    SSN_FRAMEWORK_API Status broadcast(ByteView bytes);
    SSN_FRAMEWORK_API Status poll(std::chrono::milliseconds timeout);
    SSN_FRAMEWORK_API Status startBackground();
    SSN_FRAMEWORK_API Status stop();
    SSN_FRAMEWORK_API Status waitStopped();
    SSN_FRAMEWORK_API void setEventHandler(EventHandler handler);
    SSN_FRAMEWORK_API std::vector<PeerInfo> peers() const;
    SSN_FRAMEWORK_API Result<PeerInfo> peerInfo(PeerId peer) const;

private:
    friend struct detail::NodeTestAccess;
    class Impl;
    explicit Node(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
}

#endif  // SSN_NODE_NODE_HPP
