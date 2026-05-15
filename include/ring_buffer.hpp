#ifndef CAN_PROJECT_INCLUDE_RING_BUFFER_HPP
#define CAN_PROJECT_INCLUDE_RING_BUFFER_HPP

#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>

// 定义CAN帧结构体(C风格内核)
typedef struct canframe
{
    uint32_t id;        // CAN ID
    uint8_t dlc;        // 数据长度 Data Length Code (0-8)
    uint8_t data[8];    // 核心数据负载
    uint64_t timestamp; // 模拟时间戳（车载诊断 NET 模块必考点）
} CANFrame;

// RingBuffer 类声明
class RingBuffer
{
public:
    explicit RingBuffer(uint32_t size);
    ~RingBuffer();

    bool push(const CANFrame &frame);
    bool pop(CANFrame &frame);
    bool isEmpty() const;
    bool isFull() const;

private:
    CANFrame *m_buffer;
    uint32_t m_size;
    std::atomic<uint32_t> m_head;
    std::atomic<uint32_t> m_tail;

    RingBuffer(const RingBuffer &) = delete;
    RingBuffer &operator=(const RingBuffer &) = delete;
};

#endif // CAN_PROJECT_INCLUDE_RING_BUFFER_HPP
