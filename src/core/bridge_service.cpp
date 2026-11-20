#include "bridge_service.h"

#include <algorithm>
#include "fixed_topology.h"

namespace mfc_tool::core {
namespace {

void AppendLe16(std::vector<std::uint8_t>* out, std::uint16_t v) {
    out->push_back(static_cast<std::uint8_t>(v & 0xFF));
    out->push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void AppendLe32(std::vector<std::uint8_t>* out, std::uint32_t v) {
    out->push_back(static_cast<std::uint8_t>(v & 0xFF));
    out->push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out->push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out->push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

} // namespace

BridgeService::BridgeService() : BridgeService(std::make_unique<mfc_tool::hid::HidBridgeClient>()) {}
BridgeService::BridgeService(std::unique_ptr<mfc_tool::hid::BridgeTransport> transport) : client_(std::move(transport)) {
    if (!client_) throw std::invalid_argument("missing bridge transport");
}

std::vector<mfc_tool::hid::DeviceInfo> BridgeService::ScanDevices(std::uint16_t vid, std::uint16_t pid) const {
    return client_->Scan(vid, pid);
}

void BridgeService::Connect(std::uint16_t vid, std::uint16_t pid, const std::wstring& path, int timeout_ms) {
    model_ = {};
    try {
        client_->Connect(vid, pid, path, timeout_ms);
        GetInfo();
        SgpioStatus();
        if (model_.monitor_supported) SgpioMonitorStatus();
    } catch (...) { client_->Disconnect(); model_ = {}; throw; }
}

void BridgeService::Disconnect() {
    client_->Disconnect();
    model_ = {};
}

bool BridgeService::IsConnected() const noexcept {
    return client_->IsConnected();
}

std::vector<std::uint8_t> BridgeService::Tx(std::uint8_t cmd, const std::vector<std::uint8_t>& payload) {
    try { return client_->Transact(cmd, payload); }
    catch (const mfc_tool::hid::BridgeStatusException&) { throw; }
    catch (...) {
        // A missing reply cannot prove that a state-changing request was rejected.
        model_.output = PinState::Unknown;
        model_.monitor = PinState::Unknown;
        client_->Disconnect();
        throw;
    }
}

std::vector<std::uint8_t> BridgeService::Ping(const std::vector<std::uint8_t>& data) {
    return Tx(CMD_PING, data);
}

std::vector<std::uint8_t> BridgeService::GetInfo() {
    const auto data = Tx(CMD_GET_INFO, {});
    try { model_.Identify(data); }
    catch (...) { Disconnect(); throw; }
    return data;
}

std::vector<std::uint8_t> BridgeService::ResetMcu() {
    SgpioModel::Require(model_.Identified(), "bridge identity is not confirmed");
    auto data = Tx(CMD_RESET_MCU, {});
    Disconnect();
    return data;
}

std::vector<std::uint8_t> BridgeService::EnterIap() {
    SgpioModel::Require(model_.Identified(), "bridge identity is not confirmed");
    auto data = Tx(CMD_ENTER_IAP, {});
    Disconnect();
    return data;
}

std::vector<std::uint8_t> BridgeService::SgpioConfig(int slot_count, int clock_hz) {
    SgpioModel::Require(model_.CanOutput(), "read both SGPIO states and stop Monitor before configuring Output");
    std::vector<std::uint8_t> p;
    p.push_back(static_cast<std::uint8_t>(std::clamp(slot_count, 1, 16)));
    AppendLe32(&p, static_cast<std::uint32_t>(std::clamp(clock_hz, 100000, 400000)));
    model_.output = PinState::Unknown;
    auto data = Tx(CMD_SGPIO_CONFIG, p);
    SgpioModel::Require(data == p, "invalid Output config response; state unknown");
    SgpioStatus();
    return data;
}

std::vector<std::uint8_t> BridgeService::SgpioApply(bool enable, bool periodic, int interval_ms, int sload_raw,
                                                    int act_mask, int locate_mask, int fail_mask) {
    SgpioModel::Require(model_.CanOutput(),
                        "Monitor state must be confirmed idle before Output apply");
    std::vector<std::uint8_t> p;
    p.push_back(enable ? 1u : 0u);
    p.push_back(periodic ? 1u : 0u);
    AppendLe16(&p, static_cast<std::uint16_t>(std::clamp(interval_ms, 0, 0xFFFF)));
    p.push_back(static_cast<std::uint8_t>(sload_raw & 0x0F));
    AppendLe16(&p, static_cast<std::uint16_t>(act_mask & 0xFFFF));
    AppendLe16(&p, static_cast<std::uint16_t>(locate_mask & 0xFFFF));
    AppendLe16(&p, static_cast<std::uint16_t>(fail_mask & 0xFFFF));
    model_.output = PinState::Unknown;
    const auto data = Tx(CMD_SGPIO_APPLY, p);
    SgpioModel::Require(data == p, "invalid Output apply response; state unknown");
    SgpioStatus();
    return data;
}

std::vector<std::uint8_t> BridgeService::SgpioStatus() {
    model_.output = PinState::Unknown;
    const auto data = Tx(CMD_SGPIO_STATUS, {});
    model_.OutputStatus(data);
    return data;
}

std::vector<std::uint8_t> BridgeService::SgpioOff() {
    SgpioModel::Require(model_.Identified(), "bridge identity is not confirmed");
    model_.output = PinState::Unknown;
    auto data = Tx(CMD_SGPIO_OFF, {});
    SgpioModel::Require(data.size() == 1 && data[0] == 0, "invalid Output off response; state unknown");
    SgpioStatus();
    SgpioModel::Require(model_.output == PinState::Idle, "Output remains active after Off");
    return data;
}

std::vector<std::uint8_t> BridgeService::SgpioMonitorConfig(int expected_slot_count, int trigger_timeout_ms) {
    SgpioModel::Require(model_.CanMonitor(), "read both SGPIO states and stop Output before configuring Monitor");
    std::vector<std::uint8_t> p;
    const bool valid_slot_count = expected_slot_count == 0 || expected_slot_count == 4 ||
                                  expected_slot_count == 8 || expected_slot_count == 16;
    p.push_back(static_cast<std::uint8_t>(valid_slot_count ? expected_slot_count : 0));
    AppendLe16(&p, static_cast<std::uint16_t>(std::clamp(trigger_timeout_ms, 100, 30000)));
    const auto data = Tx(CMD_SGPIO_MONITOR_CONFIG, p);
    SgpioModel::Require(data == p, "invalid Monitor config response");
    return data;
}

std::vector<std::uint8_t> BridgeService::SgpioMonitorArm() {
    SgpioModel::Require(model_.CanMonitor(), "Output and Monitor must be confirmed idle before Arm");
    model_.monitor = PinState::Unknown;
    const auto data = Tx(CMD_SGPIO_MONITOR_ARM, {});
    SgpioModel::Require(data.size() == 4 && data[0] == 1 && data[1] == fixed_topology::kMonitorClock &&
                        data[2] == fixed_topology::kMonitorData && data[3] == fixed_topology::kMonitorLoad,
                        "invalid Monitor Arm pin/state response; state unknown");
    model_.monitor = PinState::Active;
    return data;
}

std::vector<std::uint8_t> BridgeService::SgpioMonitorStatus() {
    SgpioModel::Require(model_.Identified() && model_.monitor_supported, "Monitor is not supported by this bridge");
    model_.monitor = PinState::Unknown;
    const auto data = Tx(CMD_SGPIO_MONITOR_STATUS, {});
    model_.MonitorStatus(data);
    return data;
}

std::vector<std::uint8_t> BridgeService::SgpioMonitorStop() {
    SgpioModel::Require(model_.Identified() && model_.monitor_supported, "Monitor is not supported by this bridge");
    model_.monitor = PinState::Unknown;
    const auto data = Tx(CMD_SGPIO_MONITOR_STOP, {});
    SgpioModel::Require(data.size() == 1 && data[0] == 0, "invalid Monitor Stop response; state unknown");
    SgpioMonitorStatus();
    SgpioModel::Require(model_.monitor == PinState::Idle, "Monitor remains active after Stop");
    return data;
}

OutputSnapshot BridgeService::ApplyOutput(int slots, int clock, bool periodic, int interval, int sload, int act, int locate, int fail) {
    SgpioConfig(slots, clock);
    SgpioApply(true, periodic, interval, sload, act, locate, fail);
    return model_.last_output;
}

} // namespace mfc_tool::core
