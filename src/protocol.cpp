#include "protocol.hpp"
#include "can_parser.hpp"
#include <iostream>
#include <cstdint>
#include <iomanip>

/**
 * @brief 核心位提取函数
 * 逻辑：处理 Motorola 时，由于其起始位通常定义在 MSB，
 * 要先根据起始位和长度计算出实际的偏移量。
 */
uint64_t extract_raw_signal(const uint8_t *data, const SignalConfig &config)
{
    uint64_t raw_val = 0;

    if (config.is_big_endian)
    {
        // --- Motorola (Big Endian) 简化处理逻辑 ---
        // 1. 将 8 字节拼成一个大端序的 64 位整数
        uint64_t container = 0;
        for (int i = 0; i < 8; ++i)
        {
            container |= (static_cast<uint64_t>(data[i]) << (8 * (7 - i)));
        }

        // 2. 在 Motorola MSB 定义中，StartBit 是信号的最高位
        // 假设 StartBit 是按标号 (0-63) 给出的，直接位移
        int shift = 63 - config.start_bit;
        raw_val = (container >> (shift - config.length + 1));
    }
    else
    {
        // --- Intel (Little Endian) 处理逻辑 ---
        // 1. 拼成小端序 64 位整数
        uint64_t container = 0;
        for (int i = 0; i < 8; ++i)
        {
            container |= (static_cast<uint64_t>(data[i]) << (8 * i));
        }

        // 2. Intel 的 StartBit 就是 LSB（最低位）
        raw_val = (container >> config.start_bit);
    }

    // 3. 应用掩码提取特定长度的位
    uint64_t mask = (config.length == 64) ? ~0ULL : (1ULL << config.length) - 1;
    raw_val =  raw_val & mask;

    // --- 符号扩展逻辑 ---
    if (config.is_signed && config.length < 64) {
        int shift_bits = 64 - config.length;
        raw_val = static_cast<uint64_t>((static_cast<int64_t>(raw_val << shift_bits)) >> shift_bits);
    }

    return raw_val;
}

// 测试extract_raw_signal接口
void extract_raw_signal_test() {
    // 模拟收到一条 CAN 帧：00 00 DE AD 00 00 00 00
    uint8_t mock_data[8] = {0x00, 0x00, 0xDE, 0xAD, 0x00, 0x00, 0x00, 0x00};

    // 配置：Motorola 信号，从第 16 位开始（字节 2），长度 16 位
    // 预期：应该提取出 0xDEAD
    SignalConfig v_speed_config = {16, 16, true, false};

    uint64_t raw = extract_raw_signal(mock_data, v_speed_config);

    std::cout << "Raw Signal Value: 0x" << std::hex << raw << std::dec << std::endl;
    std::cout << "Physical Value: " << raw * 1.0 + 0 << " km/h" << std::endl;

    // 测试案例：12位有符号信号，二进制 1111 1111 1111 (0xFFF)，代表 -1
    uint8_t data[8] = {0x0F, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    SignalConfig temp_sensor = {4, 12, true, true}; // Motorola, StartBit 7 (Byte0 bit 7)

    int64_t result = static_cast<int64_t>(extract_raw_signal(data, temp_sensor));

    std::cout << "Raw Hex: 0x" << std::hex << (result & 0xFFF) << std::endl;
    std::cout << "Signed Decimal: " << std::dec << result << std::endl;

    return;
}

// 测试多路复用功能
void multiplexing_test() {
    std::cout << "\n=== 多路复用测试 ===\n";

    CanParser parser;

    // 注册多路复用信号（模拟MuxDemoFrame）
    parser.registerSignal(600, Signal("ModeSwitch", 0, 4, false, false, 1, 0, true, false, -1));  // 多路复用器
    parser.registerSignal(600, Signal("NormalData", 4, 4, false, false, 1, 0, false, false, -1)); // 普通信号
    parser.registerSignal(600, Signal("BatteryVoltage", 8, 16, false, false, 0.01, 0, false, true, 0)); // 多路复用值0
    parser.registerSignal(600, Signal("MotorCurrent", 8, 16, false, false, 0.1, -100, false, true, 1)); // 多路复用值1

    // 测试数据1: ModeSwitch = 0 (EcoMode), BatteryVoltage = 0x0A00 (256.0V)
    // ModeSwitch在位0-3，BatteryVoltage在位8-23（字节1-2）
    uint8_t data1[8] = {0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::cout << "测试1: 多路复用值 = 0 (EcoMode)\n";
    parser.handleFrame(600, data1);
    parser.showData(600);

    // 测试数据2: ModeSwitch = 1 (SportMode), MotorCurrent = 0x0A00 (25.6A)
    // ModeSwitch在位0-3设为1，MotorCurrent在位8-23（字节1-2）
    uint8_t data2[8] = {0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::cout << "测试2: 多路复用值 = 1 (SportMode)\n";
    parser.handleFrame(600, data2);
    parser.showData(600);

    std::cout << "多路复用测试完成\n";
}