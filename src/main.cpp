#include "ring_buffer.hpp"
#include "doip_server.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <iomanip>
#include <can_parser.hpp>
#include "generated_can_network.hpp"

// 全局运行标志，用于控制线程停止。
// 这里使用 std::atomic<bool> 以保证多个线程之间读写安全。
static std::atomic<bool> g_running{true};

// D3 新增：全局统计变量
std::atomic<uint64_t> g_total_received{0};
std::atomic<uint64_t> g_total_dropped{0};

// 信号处理函数，用于接收 SIGINT (Ctrl+C) 并触发安全退出。
static void signal_handler(int /*signum*/)
{
    g_running.store(false, std::memory_order_relaxed);
}

// 函数声明：生产者/消费者线程入口函数
void producer_task(int can_fd, RingBuffer &ring_buffer, std::atomic<bool> &running);
void consumer_task(RingBuffer &ring_buffer, std::atomic<bool> &running, CanParser &parser);
void monitor_task(std::atomic<bool> &running);

int main(int argc, char const *argv[])
{
    (void)argc; // 避免未使用参数的编译警告
    (void)argv;

    // 定义环形缓冲区大小和要绑定的 CAN 接口名
    constexpr uint32_t kRingBufferSize = 1024u;
    constexpr char kCanInterface[] = "vcan0";

    // extract_raw_signal_test();
    // can_parser_test();
    // multiplexing_test();
    // whitelist_test();
    fault_tolerance_test();

    // BSW: 初始化环形缓冲区对象
    RingBuffer ring_buffer(kRingBufferSize);

    // 注册 SIGINT 信号处理程序，确保按 Ctrl+C 时能退出循环并清理资源。
    std::signal(SIGINT, signal_handler);

    // 创建 SocketCAN 原始套接字，用于接收 CAN 帧。
    int can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_fd < 0)
    {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    // 查找 CAN 接口索引
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, kCanInterface, IFNAMSIZ - 1);
    if (ioctl(can_fd, SIOCGIFINDEX, &ifr) < 0)
    {
        perror("CAN interface lookup failed");
        close(can_fd);
        return EXIT_FAILURE;
    }

    // 绑定 Socket 到指定 CAN 接口
    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(can_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        perror("CAN bind failed");
        close(can_fd);
        return EXIT_FAILURE;
    }

    // 启动生产者线程，将 CAN 报文读入 RingBuffer
    std::thread producer(producer_task, can_fd, std::ref(ring_buffer), std::ref(g_running));
    // 启动消费者线程，从 RingBuffer 中取出数据并打印
    CanParser my_parser;
    init_generated_database(my_parser);
    std::thread consumer(consumer_task, std::ref(ring_buffer), std::ref(g_running), std::ref(my_parser));
    // D3 新增：启动监控线程，定期显示统计信息
    std::thread monitor(monitor_task, std::ref(g_running));
    // 启动 DoIP 监听线程，监听 TCP 13400 端口，独立于 CAN 线程
    DoipServer doip_server(g_running);
    std::thread doip_thread(&DoipServer::run, &doip_server);

    std::cout << "CAN buffer running on " << kCanInterface << ". Press Ctrl+C to stop." << std::endl;

    // 主线程仅负责等待退出信号，在此循环中可以做其他管理任务。
    while (g_running.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 等待子线程退出，确保资源安全释放
    doip_thread.join();
    producer.join();
    consumer.join();
    monitor.join();
    close(can_fd);

    std::cout << "Shutdown complete." << std::endl;
    return 0;
}

// 生产者线程逻辑：从内核 Socket 读取原始报文，封装后推入 RingBuffer
void producer_task(int can_fd, RingBuffer &ring_buffer, std::atomic<bool> &running)
{
    struct can_frame raw_frame;

    while (running.load(std::memory_order_relaxed))
    {
        // 阻塞读取 CAN 帧，直到总线上有数据
        int nbytes = read(can_fd, &raw_frame, sizeof(struct can_frame));

        if (nbytes < 0)
        {
            perror("CAN Read Error");
            continue; // 读取失败持续循环，不终止整个线程
        }

        if (nbytes == sizeof(struct can_frame))
        {
            // 统计：接收到一帧
            g_total_received.fetch_add(1, std::memory_order_relaxed);
            // 将 SocketCAN 原始帧转换为内部 CANFrame 格式
            CANFrame myFrame;
            // 过滤掉 SocketCAN 中的标志位，例如 CAN_EFF_FLAG/CAN_RTR_FLAG/CAN_ERR_FLAG，仅保留实际 ID
            myFrame.id = raw_frame.can_id & CAN_EFF_MASK;
            myFrame.dlc = raw_frame.can_dlc;
            std::memcpy(myFrame.data, raw_frame.data, sizeof(myFrame.data));
            myFrame.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

            if (!ring_buffer.push(myFrame))
            {
                // 如果缓冲区已满，则丢弃该帧。
                // 在实际 BSW 应用中，这里通常应记录错误或触发错误处理。
                // 统计：Buffer 满了，被迫丢弃
                g_total_dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

// 消费者线程逻辑：从 RingBuffer 中读取帧并打印日志
void consumer_task(RingBuffer &ring_buffer, std::atomic<bool> &running, CanParser &parser)
{
    while (running.load(std::memory_order_relaxed))
    {
        CANFrame frame;
        uint32_t display_divider = 0; // 用于稀释打印频率的计数器

        if (ring_buffer.pop(frame))
        {
            // 1. 递增统计
            parser.incrementFrame();

            // 2. 只有当这个 ID 在解析器里注册过（即我们关心的有效报文）时，才执行解析并触发刷新
            // 这样既过滤了总线杂讯，又彻底告别了硬编码！
            if (parser.hasMessage(frame.id))
            {
                parser.handleFrame(frame.id, frame.data);
                parser.renderDashboard(frame.timestamp);
                display_divider = 0; // 收到有效核心数据，重置计数器
            }
            else
            {
                // 如果是总线上其他不关心的盲区数据，每 5000 帧强制刷新一次看板（防止看板完全不刷新）
                if (++display_divider >= 5000)
                {
                    parser.renderDashboard(frame.timestamp);
                    display_divider = 0;
                }
            }
        }
        else
        {
            // 如果缓冲区为空，则短暂休眠，避免忙等待
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

// D3 新增：监控显示线程
void monitor_task(std::atomic<bool> &running)
{
    while (running.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        uint64_t rec = g_total_received.load(std::memory_order_relaxed);
        uint64_t drop = g_total_dropped.load(std::memory_order_relaxed);
        double drop_rate = (rec == 0) ? 0 : (static_cast<double>(drop) / rec) * 100.0;

        std::cout << "\r[Monitor] Rec: " << rec
                  << " | Drop: " << drop
                  << " | Rate: " << std::fixed << std::setprecision(2) << drop_rate << "%"
                  << std::flush;
    }
}