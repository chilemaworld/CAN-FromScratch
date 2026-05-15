# CAN-FromScratch

## 1. 项目整体概述

本项目是一个基于 C++ 的车载通信解析与诊断服务样例，包含以下核心模块：

- `src/main.cpp`：入口程序，负责 SocketCAN 初始化、线程编排、系统运行生命周期。
- `src/can_parser.cpp` / `include/can_parser.hpp`：CAN 报文解析核心，支持信号解析、多路复用、白名单、Rolling Counter、Checksum、超时监控，以及 UDS 诊断插件。
- `src/doip_server.cpp` / `include/doip_server.hpp`：DoIP TCP 监听服务，监听标准端口 `13400`，接收车辆诊断报文。
- `src/ring_buffer.cpp` / `include/ring_buffer.hpp`：生产者-消费者环形缓冲区，实现多线程安全的数据缓存。
- `src/protocol.cpp` / `include/protocol.hpp`：测试和工具函数入口。
- `scripts/dbc_codegen.py`：解析 DBC 协议文件并生成 C++ 代码的辅助脚本。
- `tester.py`：用于模拟 UDS 诊断仪发送多帧请求。


### 1.1 项目目录结构

```
can_project/
├── Makefile
├── include/
│   ├── can_parser.hpp
│   ├── doip_server.hpp
│   ├── protocol.hpp
│   └── ring_buffer.hpp
├── obj/
├── scripts/
│   └── stress_test_can.sh
├── src/
│   ├── can_parser.cpp
│   ├── doip_server.cpp
│   ├── main.cpp
│   ├── protocol.cpp
│   └── ring_buffer.cpp
├── tester.py
└── PROJECT_STUDY_GUIDE.md
```


## 2. 关键模块说明

### 2.1 `main.cpp`

职责：

- 初始化 `vcan0` SocketCAN 接口。
- 启动多个线程：CAN 生产者、CAN 消费者、监控线程、DoIP 监听线程。
- 通过 `std::atomic<bool> g_running` 控制全局退出。
- 采用 `signal(SIGINT, signal_handler)` 捕获 Ctrl+C，实现优雅退出。

关键点：

- `producer_task`：从内核 CAN Socket 阻塞读取 `struct can_frame`，转换为内部 `CANFrame`。
- `consumer_task`：从 `RingBuffer` 读取数据，过滤注册过的 CAN ID，调用 `CanParser::handleFrame()`。
- `doip_thread`：独立启动 DoIP 服务，监听端口 13400，处理 TCP 连接。


### 2.2 `can_parser.hpp` / `can_parser.cpp`

职责：解析 CAN 报文、管理信号、实现异常容错、支持 UDS 诊断。

主要能力：

- `Signal` 类
  - 位域提取、比例因子转换、物理值计算。
  - 多路复用支持：`is_multiplexor` / `is_multiplexed` / `mux_value`。
  - 超时检测：`timeout_threshold_ms_` 与 `last_update_time_`。

- `CanMessage` 类
  - 保存多个 `Signal`。
  - 支持 `Rolling Counter` 和 `Checksum` 配置。
  - 解析过程先做帧验证，再更新信号。

- `CanParser` 类
  - 全局注册消息 ID 与信号。
  - 白名单过滤机制，提高性能，避免无关报文浪费解析时间。
  - 统计指标：总帧、有效帧、无效帧、Rolling Counter 错误、Checksum 错误、超时信号数。
  - UDS 诊断插件
    - 处理 `0x7E0` / `0x7E8` 报文
    - 识别 `Single Frame` / `First Frame` / `Consecutive Frame`
    - 收到 `0x27 01` 时返回 `0xDEADBEEF` 安全种子


### 2.3 `doip_server.hpp` / `doip_server.cpp`

职责：构建一个 DoIP 服务监听线程，接收标准 DoIP 端口数据。

实现细节：

- 使用 `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)` 创建 TCP Socket。
- 调用 `setsockopt(..., SO_REUSEADDR, ...)` 允许快速重启。
- `bind()` 到端口 `13400`，`listen()` 进入监听。
- 使用 `poll()` 等待客户端连接。
- 每个客户端连接进入 `handleClient()`，读取数据并打印。
- 对标准 DoIP 探测头部返回一条简单 ACK。


### 2.4 `ring_buffer.hpp` / `ring_buffer.cpp`

职责：实现简单的多线程环形缓存，用于 CAN 生产者 / 消费者之间数据传递。

关注点：

- `push()` / `pop()` 的线程安全性。
- `isFull()` / `isEmpty()` 用于容量检查。
- 典型嵌入式并发模式：一端写入，一端读取。
