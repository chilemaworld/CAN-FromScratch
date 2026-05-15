#include "can_parser.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <thread>
#include <random>

CanParser::CanParser()
    : random_generator_(std::random_device{}())
{
}

Signal::Signal(std::string name, int start, int len, bool be, bool sign, double factor, double offset,
               bool is_multiplexor, bool is_multiplexed, int mux_value, uint32_t timeout_ms)
    : name_(std::move(name)), start_(start), len_(len), is_be_(be), is_signed_(sign),
      factor_(factor), offset_(offset), phys_value_(0.0),
      is_multiplexor_(is_multiplexor), is_multiplexed_(is_multiplexed), mux_value_(mux_value), 
      is_valid_(false), timeout_threshold_ms_(timeout_ms), last_update_time_(std::chrono::steady_clock::now())
{
}

void Signal::update(const uint8_t *data, int current_mux_value)
{
    // 如果是多路复用信号，只有当当前多路复用值匹配时才更新
    if (is_multiplexed_ && current_mux_value != mux_value_)
    {
        is_valid_ = false;
        return; // 不匹配，不更新
    }

    uint64_t raw = extract_raw_signal(data, {start_, len_, is_be_, is_signed_});
    if (is_signed_)
    {
        phys_value_ = static_cast<int64_t>(raw) * factor_ + offset_;
    }
    else
    {
        phys_value_ = raw * factor_ + offset_;
    }
    is_valid_ = true;
    
    // 更新最后更新时间
    last_update_time_ = std::chrono::steady_clock::now();
}

void Signal::print() const
{
    if (is_valid_)
    {
        std::cout << "  - " << std::left << std::setw(15) << name_
                  << ": " << std::fixed << std::setprecision(2) << phys_value_;
        if (is_timeout())
        {
            std::cout << " [TIMEOUT]";
        }
        std::cout << std::endl;
    }
    else if (is_multiplexed_)
    {
        std::cout << "  - " << std::left << std::setw(15) << name_
                  << ": [无效]" << std::endl;
    }
    else
    {
        std::cout << "  - " << std::left << std::setw(15) << name_
                  << ": [未更新]";
        if (timeout_threshold_ms_ > 0)
        {
            std::cout << " [TIMEOUT]";
        }
        std::cout << std::endl;
    }
}

// Timeout monitoring methods
bool Signal::is_timeout() const
{
    if (timeout_threshold_ms_ == 0)
    {
        return false; // No timeout monitoring
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update_time_);
    return elapsed.count() > timeout_threshold_ms_;
}

void Signal::set_timeout_threshold(uint32_t timeout_ms)
{
    timeout_threshold_ms_ = timeout_ms;
}

void CanMessage::addSignal(const Signal &sig)
{
    signals_.push_back(sig);
}

void CanMessage::parse(const uint8_t *data)
{
    // Validate frame before parsing
    if (!isFrameValid(data))
    {
        return; // Frame validation failed, don't update signals
    }
    
    // Update rolling counter state
    if (rolling_counter_start_ >= 0 && rolling_counter_length_ > 0)
    {
        uint64_t current_counter = extract_raw_signal(data, {rolling_counter_start_, rolling_counter_length_, false, false});
        last_rolling_counter_ = static_cast<uint8_t>(current_counter);
    }
    
    // 首先找到多路复用器的值
    int current_mux_value = -1;
    for (const auto &sig : signals_)
    {
        if (sig.is_multiplexor())
        {
            // 获取多路复用器的原始值
            uint64_t raw_mux = extract_raw_signal(data, {sig.start(), sig.len(), sig.is_be(), false});
            current_mux_value = static_cast<int>(raw_mux);
            break;
        }
    }

    // 然后更新所有信号，包括多路复用信号
    for (auto &sig : signals_)
    {
        sig.update(data, current_mux_value);
    }
}

void CanMessage::display() const
{
    for (const auto &sig : signals_)
    {
        sig.print();
    }
}

// Rolling Counter and Checksum configuration
void CanMessage::setRollingCounter(int start_bit, int length, uint8_t expected_increment)
{
    rolling_counter_start_ = start_bit;
    rolling_counter_length_ = length;
    expected_increment_ = expected_increment;
    last_rolling_counter_ = 0xFF; // Reset to invalid state
}

void CanMessage::setChecksum(int start_bit, int length, ChecksumType type)
{
    checksum_start_ = start_bit;
    checksum_length_ = length;
    checksum_type_ = type;
}

// Validation methods
bool CanMessage::validateRollingCounter(const uint8_t* data) const
{
    if (rolling_counter_start_ < 0 || rolling_counter_length_ == 0)
    {
        return true; // No rolling counter configured, consider valid
    }
    
    uint64_t current_counter = extract_raw_signal(data, {rolling_counter_start_, rolling_counter_length_, false, false});
    uint8_t counter_value = static_cast<uint8_t>(current_counter);
    
    if (last_rolling_counter_ == 0xFF)
    {
        // First frame, accept any value
        return true;
    }
    
    // Check if counter incremented by expected amount
    uint8_t expected_value = (last_rolling_counter_ + expected_increment_) % (1 << rolling_counter_length_);
    return counter_value == expected_value;
}

bool CanMessage::validateChecksum(const uint8_t* data) const
{
    if (checksum_start_ < 0 || checksum_length_ == 0)
    {
        return true; // No checksum configured, consider valid
    }
    
    uint64_t expected_checksum = extract_raw_signal(data, {checksum_start_, checksum_length_, false, false});
    uint8_t calculated_checksum = calculateChecksum(data, checksum_type_);
    
    return static_cast<uint8_t>(expected_checksum) == calculated_checksum;
}

bool CanMessage::isFrameValid(const uint8_t* data) const
{
    return validateRollingCounter(data) && validateChecksum(data);
}

// Helper function to calculate checksum
uint8_t CanMessage::calculateChecksum(const uint8_t* data, ChecksumType type) const
{
    switch (type)
    {
        case ChecksumType::XOR:
        {
            uint8_t checksum = 0;
            for (int i = 0; i < 8; ++i)
            {
                if (i * 8 >= checksum_start_ && i * 8 < checksum_start_ + checksum_length_)
                {
                    checksum ^= data[i];
                }
            }
            return checksum;
        }
        case ChecksumType::SUM:
        {
            uint16_t sum = 0;
            for (int i = 0; i < 8; ++i)
            {
                if (i * 8 >= checksum_start_ && i * 8 < checksum_start_ + checksum_length_)
                {
                    sum += data[i];
                }
            }
            return static_cast<uint8_t>(sum & 0xFF);
        }
        case ChecksumType::CRC8:
        {
            // Simple CRC-8 implementation (polynomial 0x07)
            uint8_t crc = 0;
            for (int i = 0; i < 8; ++i)
            {
                if (i * 8 >= checksum_start_ && i * 8 < checksum_start_ + checksum_length_)
                {
                    crc ^= data[i];
                    for (int j = 0; j < 8; ++j)
                    {
                        if (crc & 0x80)
                        {
                            crc = (crc << 1) ^ 0x07;
                        }
                        else
                        {
                            crc <<= 1;
                        }
                    }
                }
            }
            return crc;
        }
        case ChecksumType::CRC16:
        {
            // CRC-16-CCITT implementation
            uint16_t crc = 0xFFFF;
            for (int i = 0; i < 8; ++i)
            {
                if (i * 8 >= checksum_start_ && i * 8 < checksum_start_ + checksum_length_)
                {
                    crc ^= (data[i] << 8);
                    for (int j = 0; j < 8; ++j)
                    {
                        if (crc & 0x8000)
                        {
                            crc = (crc << 1) ^ 0x1021;
                        }
                        else
                        {
                            crc <<= 1;
                        }
                    }
                }
            }
            return static_cast<uint8_t>(crc & 0xFF);
        }
        default:
            return 0;
    }
}

void CanParser::registerSignal(uint32_t id, const Signal &sig)
{
    message_registry_[id].addSignal(sig);
}

void CanParser::handleFrame(uint32_t id, const uint8_t *data)
{
    m_total_frames++;

    // 诊断插件优先处理 0x7E0 / 0x7E8 的 UDS 报文。
    if (diagnostic_mode_enabled_ && (id == 0x7E0 || id == 0x7E8))
    {
        handleDiagnosticFrame(id, data);
        return;
    }

    // 白名单检查：如果启用白名单且ID不在白名单中，立即返回
    if (!isInWhitelist(id))
    {
        m_invalid_frames++;
        return; // 快速拒绝，不进行任何解析
    }

    auto it = message_registry_.find(id);
    if (it != message_registry_.end())
    {
        // Validate frame before parsing
        if (!it->second.isFrameValid(data))
        {
            m_invalid_frames++;
            // Check what validation failed
            if (!it->second.validateRollingCounter(data))
            {
                m_rolling_counter_errors++;
            }
            if (!it->second.validateChecksum(data))
            {
                m_checksum_errors++;
            }
            return;
        }
        
        it->second.parse(data);
        m_valid_frames++;
    }
    else
    {
        m_invalid_frames++;
    }
}

void CanParser::showData(uint32_t id) const
{
    auto it = message_registry_.find(id);
    if (it != message_registry_.end())
    {
        std::cout << "[ID: 0x" << std::hex << id << std::dec << " 数据更新]" << std::endl;
        it->second.display();
    }
}

void CanParser::incrementFrame() { m_total_frames++; }
void CanParser::renderDashboard(uint64_t latest_ts) const
{
    // 利用 ANSI Escape Code 实现终端清屏并复位光标，做出动态刷新效果
    std::cout << "\033[2J\033[1;1H";
    std::cout << "==================================================\n";
    std::cout << "🚗  CAN 协议栈多线程运行看板         \n";
    std::cout << "==================================================\n";
    std::cout << " 📊 系统总计接收帧数 : " << m_total_frames << " 帧\n";
    std::cout << " ✅ 有效帧数 : " << m_valid_frames << " 帧\n";
    std::cout << " ❌ 无效帧数 : " << m_invalid_frames << " 帧\n";
    std::cout << " 🔄 Rolling Counter错误 : " << m_rolling_counter_errors << " 次\n";
    std::cout << " 🔒 Checksum错误 : " << m_checksum_errors << " 次\n";
    std::cout << " ⏰ 超时信号数 : " << getTimeoutSignals() << " 个\n";
    std::cout << " 🕒 最新帧内部时间戳 : " << latest_ts << " us\n";
    std::cout << "==================================================\n";
    std::cout << " 信号实时物理值快照 :\n";

    // 打印当前注册的所有信号最新的物理值
    for (const auto &pair : message_registry_)
    {
        pair.second.display();
    }
    std::cout << "==================================================\n";
}

bool CanParser::hasMessage(uint32_t id) const
{
    return message_registry_.find(id) != message_registry_.end();
}

// 白名单管理方法实现
void CanParser::addToWhitelist(uint32_t id)
{
    whitelist_.insert(id);
}

void CanParser::removeFromWhitelist(uint32_t id)
{
    whitelist_.erase(id);
}

void CanParser::clearWhitelist()
{
    whitelist_.clear();
}

bool CanParser::isInWhitelist(uint32_t id) const
{
    if (!whitelist_enabled_)
    {
        return true; // 如果白名单未启用，允许所有ID
    }
    return whitelist_.find(id) != whitelist_.end();
}

void CanParser::enableWhitelist(bool enable)
{
    whitelist_enabled_ = enable;
}

bool CanParser::isWhitelistEnabled() const
{
    return whitelist_enabled_;
}

void CanParser::enableDiagnosticMode(bool enable)
{
    diagnostic_mode_enabled_ = enable;
}

bool CanParser::isDiagnosticModeEnabled() const
{
    return diagnostic_mode_enabled_;
}

void CanParser::sendDiagnosticResponse(uint32_t id, const std::vector<uint8_t>& data)
{
    // 这里模拟发送诊断应答，当前实现直接打印以供调试。
    std::cout << "[诊断应答] ID=0x" << std::hex << id << std::dec << " 数据=";
    for (uint8_t byte : data)
    {
        std::cout << std::hex << std::uppercase << static_cast<int>(byte) << " ";
    }
    std::cout << std::dec << std::endl;
}

const DiagnosticMessage& CanParser::getDiagnosticMessage(uint32_t id) const
{
    static DiagnosticMessage empty;
    auto it = diagnostic_messages_.find(id);
    return it != diagnostic_messages_.end() ? it->second : empty;
}

uint64_t CanParser::getTimeoutSignals() const
{
    uint64_t timeout_count = 0;
    for (const auto& pair : message_registry_)
    {
        for (const auto& sig : pair.second.getSignals())
        {
            if (sig.is_timeout())
            {
                timeout_count++;
            }
        }
    }
    return timeout_count;
}

PciType CanParser::getPciType(uint8_t pci_byte) const
{
    return static_cast<PciType>((pci_byte & 0xF0) >> 4);
}

uint16_t CanParser::getFrameLength(const uint8_t* data) const
{
    PciType pci = getPciType(data[0]);
    if (pci == PciType::SINGLE_FRAME)
    {
        return data[0] & 0x0F;
    }
    if (pci == PciType::FIRST_FRAME)
    {
        return static_cast<uint16_t>((data[0] & 0x0F) << 8 | data[1]);
    }
    return 0;
}

std::vector<uint8_t> CanParser::createSecuritySeed()
{
    // 这里返回一个固定的“随机”安全种子 0xDEADBEEF
    // 如果将来需要真正随机，可以改为 random_generator_ 生成。
    return {0xDE, 0xAD, 0xBE, 0xEF};
}

void CanParser::processUdsMessage(uint32_t /*id*/, const std::vector<uint8_t>& payload)
{
    if (payload.empty())
    {
        std::cout << "[UDS] 空的诊断负载，不处理。" << std::endl;
        return;
    }

    uint8_t service_id = payload[0];
    if (service_id == static_cast<uint8_t>(UdsService::SECURITY_ACCESS) && payload.size() >= 2 && payload[1] == 0x01)
    {
        // 收到安全访问请求 0x27 01，返回 0x67 01 DE AD BE EF
        std::vector<uint8_t> response = {0x67, 0x01};
        auto seed = createSecuritySeed();
        response.insert(response.end(), seed.begin(), seed.end());
        std::cout << "[UDS] 收到 0x27 01 请求，发送安全种子 0xDEADBEEF。" << std::endl;
        sendDiagnosticResponse(0x7E8, response);
        return;
    }

    if (service_id == static_cast<uint8_t>(UdsService::READ_DATA_BY_IDENTIFIER))
    {
        std::cout << "[UDS] 读取数据请求完成，payload=";
    }
    else
    {
        std::cout << "[UDS] 诊断消息处理完成，payload=";
    }

    for (uint8_t byte : payload)
    {
        std::cout << std::hex << std::uppercase << static_cast<int>(byte) << " ";
    }
    std::cout << std::dec << std::endl;
}

void CanParser::handleDiagnosticFrame(uint32_t id, const uint8_t* data)
{
    uint8_t pci_byte = data[0];
    PciType pci = getPciType(pci_byte);
    auto& diag = diagnostic_messages_[id];
    diag.last_frame_time = std::chrono::steady_clock::now();

    if (pci == PciType::SINGLE_FRAME)
    {
        uint16_t length = pci_byte & 0x0F;
        diag.reset();
        diag.state = UdsState::COMPLETE;
        diag.payload.assign(data + 1, data + 1 + length);
        std::cout << "[UDS] 单帧消息接收完成，payload 长度=" << length << std::endl;
        processUdsMessage(id, diag.payload);
        return;
    }

    if (pci == PciType::FIRST_FRAME)
    {
        uint16_t total_length = ((pci_byte & 0x0F) << 8) | data[1];
        diag.reset();
        diag.state = UdsState::RECEIVING_MULTIFRAME;
        diag.total_length = total_length;
        diag.sequence_number = 1;
        diag.payload.assign(data + 2, data + 8);
        if (diag.payload.size() > total_length)
        {
            diag.payload.resize(total_length);
        }

        std::cout << "[UDS] 收到第一帧，正在接收长报文，总长度=" << total_length << " 字节" << std::endl;
        sendDiagnosticResponse(0x7E8, {0x30, 0x00, 0x00});
        return;
    }

    if (pci == PciType::CONSECUTIVE_FRAME)
    {
        if (diag.state != UdsState::RECEIVING_MULTIFRAME)
        {
            std::cout << "[UDS] 未接收第一帧就收到后续帧，丢弃。" << std::endl;
            return;
        }

        uint8_t seq = pci_byte & 0x0F;
        if (seq != diag.sequence_number)
        {
            std::cout << "[UDS] 序号不匹配，期望=" << static_cast<int>(diag.sequence_number)
                      << " 实际=" << static_cast<int>(seq) << "。" << std::endl;
            diag.reset();
            return;
        }

        size_t remaining = diag.total_length - diag.payload.size();
        size_t append_len = std::min<size_t>(7, remaining);
        diag.payload.insert(diag.payload.end(), data + 1, data + 1 + append_len);
        diag.sequence_number++;

        if (diag.payload.size() >= diag.total_length)
        {
            diag.state = UdsState::COMPLETE;
            diag.payload.resize(diag.total_length);
            std::cout << "[UDS] 长报文拼装完成，payload=";
            for (uint8_t byte : diag.payload)
            {
                std::cout << std::hex << std::uppercase << static_cast<int>(byte) << " ";
            }
            std::cout << std::dec << std::endl;
            processUdsMessage(id, diag.payload);
        }
        else
        {
            std::cout << "[UDS] 已接收 " << diag.payload.size() << "/" << diag.total_length << " 字节" << std::endl;
        }
        return;
    }

    if (pci == PciType::FLOW_CONTROL)
    {
        std::cout << "[UDS] 收到流控帧，忽略。" << std::endl;
        return;
    }

    std::cout << "[UDS] 未知 PCI 类型 " << static_cast<int>(pci_byte) << "，忽略。" << std::endl;
}

int can_parser_test()
{
    CanParser parser;

    // 场景：注册动力系统报文 (0x123) 和 车身系统报文 (0x456)
    parser.registerSignal(0x123, Signal("WheelSpeed", 7, 16, true, false, 0.01, 0));
    parser.registerSignal(0x123, Signal("EngineTemp", 23, 8, true, true, 1.0, -40));
    parser.registerSignal(0x456, Signal("FuelLevel", 7, 8, false, false, 0.4, 0));

    // 模拟收到 ID 0x123 的原始帧
    uint8_t data1[8] = {0x0F, 0xFF, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00};
    parser.handleFrame(0x123, data1);
    parser.showData(0x123);

    // 模拟收到 ID 0x456 的原始帧
    uint8_t data2[8] = {0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // 0x64 = 100
    parser.handleFrame(0x456, data2);
    parser.showData(0x456);

    return 0;
}

// 白名单功能测试
int whitelist_test()
{
    std::cout << "\n=== 白名单功能测试 ===\n";

    CanParser parser;

    // 注册几个信号
    parser.registerSignal(0x123, Signal("WheelSpeed", 7, 16, true, false, 0.01, 0));
    parser.registerSignal(0x456, Signal("FuelLevel", 7, 8, false, false, 0.4, 0));
    parser.registerSignal(0x789, Signal("EngineRPM", 7, 16, true, false, 1.0, 0));

    // 启用白名单
    parser.enableWhitelist(true);
    std::cout << "启用白名单模式\n";

    // 只添加 0x123 到白名单
    parser.addToWhitelist(0x123);
    std::cout << "将 0x123 添加到白名单\n";

    // 测试数据
    uint8_t test_data[8] = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // 测试白名单内的ID
    std::cout << "\n测试白名单内ID (0x123):\n";
    parser.handleFrame(0x123, test_data);
    parser.showData(0x123);

    // 测试白名单外的ID
    std::cout << "\n测试白名单外ID (0x456):\n";
    parser.handleFrame(0x456, test_data);
    // 检查是否有数据被解析（应该没有，因为被白名单过滤了）
    if (!parser.hasMessage(0x456)) {
        std::cout << "ID 0x456 被白名单过滤，未解析\n";
    } else {
        parser.showData(0x456);
    }

    // 添加更多ID到白名单
    parser.addToWhitelist(0x456);
    std::cout << "\n将 0x456 添加到白名单\n";

    // 再次测试
    std::cout << "\n再次测试ID 0x456:\n";
    parser.handleFrame(0x456, test_data);
    parser.showData(0x456);

    // 禁用白名单
    parser.enableWhitelist(false);
    std::cout << "\n禁用白名单模式\n";

    // 测试所有ID都能通过
    std::cout << "\n测试所有ID都能通过:\n";
    parser.handleFrame(0x789, test_data);
    parser.showData(0x789);

    std::cout << "\n白名单测试完成\n";
    return 0;
}

// 异常与容错测试
int fault_tolerance_test()
{
    std::cout << "\n=== 异常与容错测试 ===\n";

    CanParser parser;

    // 注册测试信号，包含超时监控
    parser.registerSignal(0x200, Signal("EngineSpeed", 0, 16, true, false, 0.1, 0, false, false, -1, 1000)); // 1秒超时
    parser.registerSignal(0x200, Signal("ThrottlePos", 16, 8, true, false, 0.5, 0, false, false, -1, 500));  // 0.5秒超时

    // 配置Rolling Counter和Checksum
    auto& engine_msg = const_cast<CanMessage&>(parser.getMessageRegistry().at(0x200));
    engine_msg.setRollingCounter(56, 8, 1); // 8位Rolling Counter，从第56位开始，每次递增1
    engine_msg.setChecksum(48, 8, ChecksumType::XOR); // 8位XOR校验和，从第48位开始

    std::cout << "配置完成：Rolling Counter (位56-63), Checksum (位48-55, XOR)\n";

    // 测试1: 正常帧
    uint8_t normal_data[8] = {0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01}; // Rolling Counter = 1
    // 计算XOR校验和：0xFF ^ 0xFF ^ 0x80 ^ 0x00 ^ 0x00 ^ 0x00 ^ 0x00 ^ 0x00 = 0x7F，但这里我们设置为0x00作为校验和
    normal_data[6] = 0x7F; // 设置正确的校验和

    std::cout << "\n测试1: 正常帧 (Rolling Counter=1)\n";
    parser.handleFrame(0x200, normal_data);
    parser.showData(0x200);
    std::cout << "统计信息: 总帧=" << parser.getTotalFrames() 
              << ", 有效帧=" << parser.getValidFrames() 
              << ", 无效帧=" << parser.getInvalidFrames() << std::endl;

    // 测试2: Rolling Counter错误
    uint8_t bad_counter_data[8] = {0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x05}; // Rolling Counter = 5 (应该递增到2)
    bad_counter_data[6] = 0x7F; // 正确的校验和

    std::cout << "\n测试2: Rolling Counter错误 (期望=2, 实际=5)\n";
    parser.handleFrame(0x200, bad_counter_data);
    std::cout << "统计信息: 总帧=" << parser.getTotalFrames() 
              << ", 有效帧=" << parser.getValidFrames() 
              << ", 无效帧=" << parser.getInvalidFrames()
              << ", Rolling Counter错误=" << parser.getRollingCounterErrors() << std::endl;

    // 测试3: Checksum错误
    uint8_t bad_checksum_data[8] = {0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x02}; // Rolling Counter = 2 (正确)
    bad_checksum_data[6] = 0x00; // 错误的校验和 (应该是0x7F)

    std::cout << "\n测试3: Checksum错误\n";
    parser.handleFrame(0x200, bad_checksum_data);
    std::cout << "统计信息: 总帧=" << parser.getTotalFrames() 
              << ", 有效帧=" << parser.getValidFrames() 
              << ", 无效帧=" << parser.getInvalidFrames()
              << ", Checksum错误=" << parser.getChecksumErrors() << std::endl;

    // 测试4: 正确的下一帧
    uint8_t next_data[8] = {0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x02}; // Rolling Counter = 2
    next_data[6] = 0x7F; // 正确的校验和

    std::cout << "\n测试4: 正确的下一帧 (Rolling Counter=2)\n";
    parser.handleFrame(0x200, next_data);
    parser.showData(0x200);
    std::cout << "统计信息: 总帧=" << parser.getTotalFrames() 
              << ", 有效帧=" << parser.getValidFrames() 
              << ", 无效帧=" << parser.getInvalidFrames() << std::endl;

    // 测试5: 超时检测 (等待超过超时时间)
    std::cout << "\n测试5: 等待超时...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1200)); // 等待1.2秒
    parser.showData(0x200); // 显示超时状态
    std::cout << "超时信号数: " << parser.getTimeoutSignals() << std::endl;

    // 测试6: 动态调整超时阈值
    std::cout << "\n测试6: 动态调整超时阈值\n";
    // 重新设置信号的超时阈值
    auto& engine_msg_ref = const_cast<CanMessage&>(parser.getMessageRegistry().at(0x200));
    auto& signals = engine_msg_ref.getSignals();
    if (signals.size() >= 2) {
        signals[0].set_timeout_threshold(2000); // 将EngineSpeed的超时调整为2秒
        signals[1].set_timeout_threshold(3000); // 将ThrottlePos的超时调整为3秒
        std::cout << "已调整EngineSpeed超时为2秒, ThrottlePos超时为3秒\n";
    }

    std::cout << "\n异常与容错测试完成\n";
    return 0;
}