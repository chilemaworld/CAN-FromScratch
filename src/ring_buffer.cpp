#include "ring_buffer.hpp"

RingBuffer::RingBuffer(uint32_t size)
    : m_buffer(nullptr), m_size(size), m_head(0), m_tail(0)
{
    if (m_size == 0u) {
        m_size = 1u;
    }
    m_buffer = new CANFrame[m_size];
}

RingBuffer::~RingBuffer()
{
    delete[] m_buffer;
}

bool RingBuffer::push(const CANFrame& frame)
{
    const uint32_t head = m_head.load(std::memory_order_acquire);
    const uint32_t tail = m_tail.load(std::memory_order_relaxed);
    uint32_t next_tail = tail + 1;
    if (next_tail >= m_size) {
        next_tail = 0;
    }
    if (next_tail == head) {
        return false;
    }
    m_buffer[tail] = frame;
    m_tail.store(next_tail, std::memory_order_release);
    return true;
}

bool RingBuffer::pop(CANFrame& frame)
{
    const uint32_t tail = m_tail.load(std::memory_order_acquire);
    uint32_t head = m_head.load(std::memory_order_relaxed);
    if (head == tail) {
        return false;
    }
    frame = m_buffer[head];
    head += 1;
    if (head >= m_size) {
        head = 0;
    }
    m_head.store(head, std::memory_order_release);
    return true;
}

bool RingBuffer::isEmpty() const
{
    return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
}

bool RingBuffer::isFull() const
{
    const uint32_t head = m_head.load(std::memory_order_acquire);
    uint32_t next_tail = m_tail.load(std::memory_order_relaxed) + 1;
    if (next_tail >= m_size) {
        next_tail = 0;
    }
    return next_tail == head;
}
