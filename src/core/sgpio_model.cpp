#include "sgpio_model.h"
#include "fixed_topology.h"
#include <algorithm>

namespace mfc_tool::core {
namespace {
bool Slots(unsigned int n) { return n == 0 || n == 4 || n == 8 || n == 16; }
std::uint16_t Le16(const std::vector<std::uint8_t>& d, size_t p) {
    return static_cast<std::uint16_t>(d[p] | (static_cast<unsigned int>(d[p+1]) << 8));
}
std::uint32_t Le32(const std::vector<std::uint8_t>& d, size_t p) {
    return static_cast<std::uint32_t>(d[p]) | (static_cast<std::uint32_t>(d[p+1]) << 8) |
        (static_cast<std::uint32_t>(d[p+2]) << 16) | (static_cast<std::uint32_t>(d[p+3]) << 24);
}
}
void SgpioModel::Identify(const std::vector<std::uint8_t>& data) {
    const auto end = std::find(data.begin(), data.end(), 0);
    Require(end != data.end(), "missing bridge identity terminator");
    const std::string name(data.begin(), end);
    BridgeBoard candidate = BridgeBoard::Unknown;
    std::string version;
    for (const auto& prefix : {std::string("m032-sgpio-bridge/"), std::string("m261-sgpio-bridge/")}) {
        if (name.compare(0, prefix.size(), prefix) == 0) {
            candidate = prefix[1] == '0' ? BridgeBoard::M032 : BridgeBoard::M261;
            version = name.substr(prefix.size());
        }
    }
    Require(candidate != BridgeBoard::Unknown && (version == "1.0.0" || version == "1.1.0"),
            "unsupported SGPIO board or firmware identity");
    Require(board == BridgeBoard::Unknown || (board == candidate && monitor_supported == (version == "1.1.0")),
            "bridge identity changed within this connection");
    board = candidate;
    monitor_supported = version == "1.1.0";
    if (!monitor_supported) monitor = PinState::Idle;
}
void SgpioModel::OutputStatus(const std::vector<std::uint8_t>& d) {
    last_output = DecodeOutput(d);
    output = last_output.enabled ? PinState::Active : PinState::Idle;
}
OutputSnapshot SgpioModel::DecodeOutput(const std::vector<std::uint8_t>& d) {
    Require(d.size() >= 21, "short output status");
    Require(d[16] == fixed_topology::kOutputClock && d[17] == fixed_topology::kOutputData &&
            d[18] == fixed_topology::kOutputLoad && d[19] == fixed_topology::kInputData, "output pin contract mismatch");
    Require(d[0] <= 1 && d[1] <= 1 && d[2] >= 1 && d[2] <= 16 && d[3] <= 15 && d[20] <= 1,
            "invalid output status values");
    Require(Le32(d,4) >= 100000 && Le32(d,4) <= 400000, "invalid output clock");
    if (d.size() > 21) {
        Require(d.size() >= 24 && d[21] <= 1 && d[22] <= 4 && d[23] <= 128 &&
                d.size() >= 24u + 4u * d[22] && d[23] <= 32u * d[22], "invalid SDIN capture");
    }
    OutputSnapshot result;
    result.enabled = d[0] != 0;
    result.periodic = d[1] != 0;
    result.slots = d[2];
    result.sload = d[3];
    result.clock_hz = static_cast<int>(Le32(d,4));
    result.interval_ms = Le16(d,8);
    result.act = Le16(d,10);
    result.locate = Le16(d,12);
    result.fail = Le16(d,14);
    result.sdin_level = d[20];
    if (d.size() >= 24) {
        result.capture_valid = d[21] != 0;
        result.capture_bits = d[23];
        for (size_t i = 0; i < d[22]; ++i) result.capture_words.push_back(Le32(d,24+4*i));
    }
    return result;
}
void SgpioModel::MonitorStatus(const std::vector<std::uint8_t>& d) {
    last_monitor = DecodeMonitor(d);
    monitor = last_monitor.state == 1 || last_monitor.state == 2 ? PinState::Active : PinState::Idle;
}
MonitorSnapshot SgpioModel::DecodeMonitor(const std::vector<std::uint8_t>& d) {
    Require(d.size() == 37, "invalid monitor status size");
    Require(d[34] == fixed_topology::kMonitorClock && d[35] == fixed_topology::kMonitorData &&
            d[36] == fixed_topology::kMonitorLoad, "monitor pin contract mismatch");
    Require(d[0] <= 5 && Slots(d[1]) && Slots(d[2]) && d[3] <= 15 &&
            (static_cast<unsigned int>(d[6]) | (static_cast<unsigned int>(d[7]) << 8)) <= 2304 &&
            Le32(d,12) != 0, "invalid monitor status values");
    return {d[0], d[1], d[2], d[3], Le16(d,4), Le16(d,6), Le16(d,8), Le16(d,10),
            Le32(d,12), Le32(d,16), Le32(d,20), Le32(d,24), Le16(d,28), Le16(d,30), Le16(d,32)};
}
} // namespace mfc_tool::core
