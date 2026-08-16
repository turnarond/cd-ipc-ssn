// ============================================================================
// echo 客户端示例 —— 与 echo_server 配对，展示 SsnClient 同步调用
//
// 教学要点：
//   1. connect：地址格式与传输层一致（tcp://host:port）；
//   2. callJson：同步调用——阻塞至收到应答，超时（默认 3 秒）或
//      服务端返回框架错误（如 1001 方法不存在）时返回 false；
//   3. nlohmann/json 初始化列表语法：{{"msg", "..."}, {"n", 42}}。
// ============================================================================
#include "ssn/framework/SsnClient.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

int main() {
    // 创建客户端并连接服务端
    ssn::SsnClient cli;
    if (!cli.connect("tcp://127.0.0.1:18880")) {
        std::cerr << "连接失败：请确认 echo_server 已启动" << std::endl;
        return 1;
    }

    // 构造请求体：一个 JSON 对象（msg 字符串 + n 数字）
    nlohmann::json req = {{"msg", "你好，SSN C++ 框架！"}, {"n", 42}};

    // 同步调用并检查结果
    nlohmann::json resp;
    if (cli.callJson("/echo", req, resp)) {
        std::cout << "应答: " << resp.dump() << std::endl;   // dump() 序列化为字符串
    } else {
        std::cerr << "调用失败" << std::endl;
        cli.disconnect();
        return 1;
    }

    // 断开连接（析构函数也会兜底清理，显式调用更清晰）
    cli.disconnect();
    return 0;
}
