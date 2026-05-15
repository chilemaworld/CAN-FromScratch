#ifndef CAN_PROJECT_INCLUDE_CAN_PARSER_HPP
#define CAN_PROJECT_INCLUDE_CAN_PARSER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include "protocol.hpp"
#include <cstdint>
#include <iomanip>
#include <unordered_set>
#include <chrono>
#include <random>

// Checksum types for CAN message validation
enum class ChecksumType {
    XOR,        // XOR of all bytes
    CRC8,       // CRC-8
    CRC16,      // CRC-16
    SUM,        // Sum of all bytes
    CUSTOM      // Custom checksum algorithm
};

// UDS Diagnostic states
enum class UdsState {
    IDLE,
    RECEIVING_MULTIFRAME,
    ASSEMBLING,
    COMPLETE
};

// UDS PCI types
enum class PciType {
    SINGLE_FRAME = 0,
    FIRST_FRAME = 1,
    CONSECUTIVE_FRAME = 2,
    FLOW_CONTROL = 3
};

// UDS Service IDs
enum class UdsService {
    DIAGNOSTIC_SESSION_CONTROL = 0x10,
    ECU_RESET = 0x11,
    SECURITY_ACCESS = 0x27,
    READ_DATA_BY_IDENTIFIER = 0x22,
    WRITE_DATA_BY_IDENTIFIER = 0x2E,
    ROUTINE_CONTROL = 0x31,
    REQUEST_DOWNLOAD = 0x34,
    TRANSFER_DATA = 0x36,
    REQUEST_TRANSFER_EXIT = 0x37,
    NEGATIVE_RESPONSE = 0x7F
};

// Diagnostic message structure
struct DiagnosticMessage {
    UdsState state = UdsState::IDLE;
    std::vector<uint8_t> payload;
    uint16_t total_length = 0;
    uint8_t sequence_number = 0;
    std::chrono::steady_clock::time_point last_frame_time;
    
    void reset() {
        state = UdsState::IDLE;
        payload.clear();
        total_length = 0;
        sequence_number = 0;
    }
};

int can_parser_test();
int whitelist_test();
int fault_tolerance_test();

class Signal {
public:
    Signal(std::string name, int start, int len, bool be, bool sign, double factor, double offset,
           bool is_multiplexor = false, bool is_multiplexed = false, int mux_value = -1,
           uint32_t timeout_ms = 0);
    void update(const uint8_t* data, int current_mux_value = -1);
    void print() const;
    
    // Getter methods for multiplexing
    bool is_multiplexor() const { return is_multiplexor_; }
    bool is_multiplexed() const { return is_multiplexed_; }
    int mux_value() const { return mux_value_; }
    int start() const { return start_; }
    int len() const { return len_; }
    bool is_be() const { return is_be_; }
    bool is_signed() const { return is_signed_; }
    bool is_valid() const { return is_valid_; }
    
    // Timeout monitoring
    bool is_timeout() const;
    void set_timeout_threshold(uint32_t timeout_ms);
    uint32_t get_timeout_threshold() const { return timeout_threshold_ms_; }

private:
    std::string name_; // 信号名称
    int start_; // 起始位
    int len_; // 位长度
    bool is_be_; // true = Motorola, false = Intel
    bool is_signed_; // 是否是有符号数
    double factor_; // 比例因子
    double offset_; // 偏移量
    double phys_value_; // 物理值
    
    // 多路复用相关字段
    bool is_multiplexor_; // 是否为多路复用器
    bool is_multiplexed_; // 是否为多路复用信号
    int mux_value_; // 多路复用值（对于多路复用信号）
    bool is_valid_; // 信号是否有效（对于多路复用信号）
    
    // 超时监控相关字段
    uint32_t timeout_threshold_ms_; // 超时阈值（毫秒）
    std::chrono::steady_clock::time_point last_update_time_; // 最后更新时间
};

class CanMessage {
public:
    void addSignal(const Signal& sig);
    void parse(const uint8_t* data);
    void display() const;
    
    // Rolling Counter and Checksum configuration
    void setRollingCounter(int start_bit, int length, uint8_t expected_increment = 1);
    void setChecksum(int start_bit, int length, ChecksumType type = ChecksumType::XOR);
    
    // Validation methods
    bool validateRollingCounter(const uint8_t* data) const;
    bool validateChecksum(const uint8_t* data) const;
    bool isFrameValid(const uint8_t* data) const;
    
    // Rolling Counter state
    uint8_t getLastRollingCounter() const { return last_rolling_counter_; }
    void resetRollingCounter() { last_rolling_counter_ = 0xFF; } // Invalid initial value
    
    // Access to signals for timeout checking
    const std::vector<Signal>& getSignals() const { return signals_; }
    std::vector<Signal>& getSignals() { return signals_; }

private:
    std::vector<Signal> signals_;
    
    // Rolling Counter configuration
    int rolling_counter_start_ = -1;
    int rolling_counter_length_ = 0;
    uint8_t expected_increment_ = 1;
    uint8_t last_rolling_counter_ = 0xFF; // Invalid initial value
    
    // Checksum configuration
    int checksum_start_ = -1;
    int checksum_length_ = 0;
    ChecksumType checksum_type_ = ChecksumType::XOR;
    
    // Helper method for checksum calculation
    uint8_t calculateChecksum(const uint8_t* data, ChecksumType type) const;
};

class CanParser {
public:
    CanParser();

    void registerSignal(uint32_t id, const Signal& sig);
    void handleFrame(uint32_t id, const uint8_t* data);
    void showData(uint32_t id) const;

    // --- 📥 D7 看板 ---
    void incrementFrame();
    void renderDashboard(uint64_t latest_ts) const;

    // 新增：判断某个 ID 是否在 DBC 中注册过
    bool hasMessage(uint32_t id) const ;

    // 白名单管理方法
    void addToWhitelist(uint32_t id);
    void removeFromWhitelist(uint32_t id);
    void clearWhitelist();
    bool isInWhitelist(uint32_t id) const;
    void enableWhitelist(bool enable);
    bool isWhitelistEnabled() const;
    
    // Error statistics
    uint64_t getTotalFrames() const { return m_total_frames; }
    uint64_t getValidFrames() const { return m_valid_frames; }
    uint64_t getInvalidFrames() const { return m_invalid_frames; }
    uint64_t getRollingCounterErrors() const { return m_rolling_counter_errors; }
    uint64_t getChecksumErrors() const { return m_checksum_errors; }
    uint64_t getTimeoutSignals() const;
    
    // Access to message registry for testing (should be used carefully)
    const std::unordered_map<uint32_t, CanMessage>& getMessageRegistry() const { return message_registry_; }
    
    // Diagnostic plugin methods
    void enableDiagnosticMode(bool enable);
    bool isDiagnosticModeEnabled() const;
    void sendDiagnosticResponse(uint32_t id, const std::vector<uint8_t>& data);
    const DiagnosticMessage& getDiagnosticMessage(uint32_t id) const;

private:
    std::unordered_map<uint32_t, CanMessage> message_registry_;
    uint64_t m_total_frames = 0; // D7：总帧数统计
    uint64_t m_valid_frames = 0; // 有效帧数统计
    uint64_t m_invalid_frames = 0; // 无效帧数统计
    uint64_t m_rolling_counter_errors = 0; // Rolling Counter错误统计
    uint64_t m_checksum_errors = 0; // Checksum错误统计
    
    // 白名单功能
    std::unordered_set<uint32_t> whitelist_;
    bool whitelist_enabled_ = false;
    
    // Diagnostic plugin
    bool diagnostic_mode_enabled_ = false;
    std::unordered_map<uint32_t, DiagnosticMessage> diagnostic_messages_;
    std::mt19937 random_generator_;
    
    // Diagnostic helper methods
    void handleDiagnosticFrame(uint32_t id, const uint8_t* data);
    void processUdsMessage(uint32_t id, const std::vector<uint8_t>& payload);
    std::vector<uint8_t> createSecuritySeed();
    PciType getPciType(uint8_t pci_byte) const;
    uint16_t getFrameLength(const uint8_t* data) const;
};

#endif // CAN_PROJECT_INCLUDE_CAN_PARSER_HPP
