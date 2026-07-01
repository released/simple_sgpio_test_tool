#include "bridge_service.h"

#include <algorithm>

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

std::vector<mfc_tool::hid::DeviceInfo> BridgeService::ScanDevices(std::uint16_t vid, std::uint16_t pid) const {
    return mfc_tool::hid::EnumerateHidDevices(vid, pid, true);
}

void BridgeService::Connect(std::uint16_t vid, std::uint16_t pid, const std::wstring& path, int timeout_ms) {
    client_.Connect(vid, pid, path, timeout_ms);
}

void BridgeService::Disconnect() {
    client_.Disconnect();
}

bool BridgeService::IsConnected() const noexcept {
    return client_.IsConnected();
}

std::vector<std::uint8_t> BridgeService::Tx(std::uint8_t cmd, const std::vector<std::uint8_t>& payload) {
    return client_.Transact(cmd, payload);
}

std::vector<std::uint8_t> BridgeService::Ping(const std::vector<std::uint8_t>& data) {
    return Tx(CMD_PING, data);
}

std::vector<std::uint8_t> BridgeService::GetInfo() {
    return Tx(CMD_GET_INFO, {});
}

std::vector<std::uint8_t> BridgeService::ResetMcu() {
    return Tx(CMD_RESET_MCU, {});
}

std::vector<std::uint8_t> BridgeService::SgpioConfig(int slot_count, int clock_hz) {
    std::vector<std::uint8_t> p;
    p.push_back(static_cast<std::uint8_t>(std::clamp(slot_count, 1, 16)));
    AppendLe32(&p, static_cast<std::uint32_t>(std::clamp(clock_hz, 100000, 400000)));
    return Tx(CMD_SGPIO_CONFIG, p);
}

std::vector<std::uint8_t> BridgeService::SgpioApply(bool enable, bool periodic, int interval_ms, int sload_raw,
                                                    int act_mask, int locate_mask, int fail_mask) {
    std::vector<std::uint8_t> p;
    p.push_back(enable ? 1u : 0u);
    p.push_back(periodic ? 1u : 0u);
    AppendLe16(&p, static_cast<std::uint16_t>(std::clamp(interval_ms, 0, 0xFFFF)));
    p.push_back(static_cast<std::uint8_t>(sload_raw & 0x0F));
    AppendLe16(&p, static_cast<std::uint16_t>(act_mask & 0xFFFF));
    AppendLe16(&p, static_cast<std::uint16_t>(locate_mask & 0xFFFF));
    AppendLe16(&p, static_cast<std::uint16_t>(fail_mask & 0xFFFF));
    return Tx(CMD_SGPIO_APPLY, p);
}

std::vector<std::uint8_t> BridgeService::SgpioStatus() {
    return Tx(CMD_SGPIO_STATUS, {});
}

std::vector<std::uint8_t> BridgeService::SgpioOff() {
    return Tx(CMD_SGPIO_OFF, {});
}

} // namespace mfc_tool::core
