#ifndef CAN_PROJECT_INCLUDE_PROTOCOL_HPP
#define CAN_PROJECT_INCLUDE_PROTOCOL_HPP

#include <cstdint>

/*
 * @brief 信号配置结构体
 */
struct SignalConfig {
    int  start_bit;      // 起始位
    int  length;         // 位长度
    bool is_big_endian;  // true = Motorola, false = Intel
    bool is_signed;      // 新增：是否是有符号数
} ;

/**
 * @brief 从 CAN 数据帧中提取原始信号值
 * @param data 8 字节的 CAN 数据载荷
 * @param config 信号配置，包含起始位、长度、字节序和符号信息
 * @return 提取出的原始信号值，已根据配置进行符号扩展（如果适用）
 *
 * 逻辑说明：
 * - 首先根据字节序将 8 字节数据拼成一个 64 位整数。
 * - 根据起始位和长度计算出信号在这个整数中的位置，并提取出对应的位。
 * - 如果信号是有符号的且长度小于 64 位，则进行符号扩展，确保负数正确表示。
 *
 * 注意：此函数假设输入数据已经按照 CAN 协议正确填充，并且配置参数有效。
 */
uint64_t extract_raw_signal(const uint8_t *data, const SignalConfig &config);

// 测试extract_raw_signal接口
void extract_raw_signal_test();

// 测试多路复用功能
void multiplexing_test();



#endif // CAN_PROJECT_INCLUDE_PROTOCOL_HPP
