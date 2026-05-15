#include "doip_server.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

constexpr int kDoipPort = 13400;
constexpr int kBacklog = 4;
constexpr int kPollTimeoutMs = 500;

DoipServer::DoipServer(std::atomic<bool> &running)
    : m_running(running)
{
}

DoipServer::~DoipServer()
{
    closeListenSocket();
}

void DoipServer::closeListenSocket()
{
    if (m_listen_fd >= 0)
    {
        close(m_listen_fd);
        m_listen_fd = -1;
    }
}

void DoipServer::run()
{
    m_listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listen_fd < 0)
    {
        perror("DoIP socket creation failed");
        return;
    }

    int enable = 1;
    if (setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
    {
        perror("DoIP setsockopt SO_REUSEADDR failed");
        closeListenSocket();
        return;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(kDoipPort);

    if (bind(m_listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        perror("DoIP bind failed");
        closeListenSocket();
        return;
    }

    if (listen(m_listen_fd, kBacklog) < 0)
    {
        perror("DoIP listen failed");
        closeListenSocket();
        return;
    }

    std::cout << "DoIP listener started on port " << kDoipPort << "." << std::endl;

    struct pollfd pfd;
    pfd.fd = m_listen_fd;
    pfd.events = POLLIN;

    while (m_running.load(std::memory_order_relaxed))
    {
        int ret = poll(&pfd, 1, kPollTimeoutMs);
        if (ret < 0)
        {
            perror("DoIP poll failed");
            break;
        }

        if (ret == 0)
        {
            continue; // 超时，继续检查运行标志
        }

        if (pfd.revents & POLLIN)
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(m_listen_fd, reinterpret_cast<struct sockaddr *>(&client_addr), &client_len);
            if (client_fd < 0)
            {
                perror("DoIP accept failed");
                continue;
            }

            char remote_ip[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &client_addr.sin_addr, remote_ip, sizeof(remote_ip));
            std::cout << "DoIP client connected from " << remote_ip << ":" << ntohs(client_addr.sin_port) << std::endl;
            handleClient(client_fd);
            close(client_fd);
            std::cout << "DoIP client disconnected." << std::endl;
        }
    }

    closeListenSocket();
    std::cout << "DoIP listener stopped." << std::endl;
}

void DoipServer::handleClient(int client_fd)
{
    constexpr size_t kBufferSize = 1024;
    uint8_t buffer[kBufferSize];

    while (m_running.load(std::memory_order_relaxed))
    {
        ssize_t nbytes = recv(client_fd, buffer, sizeof(buffer), 0);
        if (nbytes < 0)
        {
            perror("DoIP recv failed");
            break;
        }
        if (nbytes == 0)
        {
            break; // 客户端主动关闭
        }

        printDoipPacket(buffer, static_cast<size_t>(nbytes));

        // 简单回应探测：如果客户端发送标准 DoIP 头部，返回一个基本的 ACK 报文。
        if (nbytes >= 8 && buffer[0] == 0x02)
        {
            uint8_t response[8] = {0x02, 0xFD, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
            send(client_fd, response, sizeof(response), 0);
        }
    }
}

void DoipServer::printDoipPacket(const uint8_t *buffer, size_t length)
{
    std::cout << "[DoIP] 收到 " << length << " 字节: ";
    for (size_t i = 0; i < length; ++i)
    {
        std::cout << std::hex << std::uppercase << static_cast<int>(buffer[i]);
        if (i + 1 < length)
        {
            std::cout << " ";
        }
    }
    std::cout << std::dec << std::endl;
}
