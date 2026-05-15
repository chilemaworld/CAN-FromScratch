#ifndef CAN_PROJECT_INCLUDE_DOIP_SERVER_HPP
#define CAN_PROJECT_INCLUDE_DOIP_SERVER_HPP

#include <atomic>
#include <cstdint>
#include <string>

// DoIP TCP 端口监听服务，用于接收标准 DoIP 13400 端口数据。
// 该服务运行在独立线程中，接收连接后打印 DoIP 报文并保持连接。
class DoipServer
{
public:
    explicit DoipServer(std::atomic<bool> &running);
    ~DoipServer();

    // 启动监听服务。该函数会阻塞，直到运行标志置为 false 或者监听失败。
    void run();

private:
    std::atomic<bool> &m_running;
    int m_listen_fd = -1;

    // DoIP 报文解析及打印
    void handleClient(int client_fd);
    void printDoipPacket(const uint8_t *buffer, size_t length);
    void closeListenSocket();
};

#endif // CAN_PROJECT_INCLUDE_DOIP_SERVER_HPP
