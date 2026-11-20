#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace mfc_tool::core {

enum class PinState { Unknown, Idle, Active };
enum class BridgeBoard { Unknown, M032, M261 };

struct OutputSnapshot {
    bool enabled = false;
    bool periodic = false;
    int slots = 0;
    int sload = 0;
    int clock_hz = 0;
    int interval_ms = 0;
    int act = 0;
    int locate = 0;
    int fail = 0;
    int sdin_level = 0;
    bool capture_valid = false;
    int capture_bits = 0;
    std::vector<std::uint32_t> capture_words;
};

struct MonitorSnapshot {
    std::uint8_t state = 0;
    int expected_slots = 0;
    int detected_slots = 0;
    std::uint8_t sload = 0;
    std::uint16_t errors = 0;
    std::uint16_t samples = 0;
    std::uint16_t rising_edges = 0;
    std::uint16_t decoded_bits = 0;
    std::uint32_t sample_rate = 0;
    std::uint32_t clock_hz = 0;
    std::uint32_t high_ns = 0;
    std::uint32_t low_ns = 0;
    std::uint16_t act = 0;
    std::uint16_t locate = 0;
    std::uint16_t fail = 0;
};

struct SgpioModel {
    BridgeBoard board = BridgeBoard::Unknown;
    bool monitor_supported = false;
    PinState output = PinState::Unknown;
    PinState monitor = PinState::Unknown;
    OutputSnapshot last_output;
    MonitorSnapshot last_monitor;
    bool Identified() const noexcept { return board != BridgeBoard::Unknown; }
    bool CanOutput() const noexcept { return Identified() && monitor == PinState::Idle && output != PinState::Unknown; }
    bool CanMonitor() const noexcept { return Identified() && monitor_supported && output == PinState::Idle && monitor == PinState::Idle; }
    void Identify(const std::vector<std::uint8_t>& data);
    void OutputStatus(const std::vector<std::uint8_t>& data);
    void MonitorStatus(const std::vector<std::uint8_t>& data);
    static OutputSnapshot DecodeOutput(const std::vector<std::uint8_t>& data);
    static MonitorSnapshot DecodeMonitor(const std::vector<std::uint8_t>& data);
    static void Require(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
};

} // namespace mfc_tool::core
