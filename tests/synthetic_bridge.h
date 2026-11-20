#pragma once
#include "../src/hid/hid_bridge_client.h"
#include "../src/core/bridge_commands.h"
#include <atomic>
#include <chrono>
#include <thread>

class SyntheticBridge : public mfc_tool::hid::BridgeTransport {
public:
    std::atomic<bool> cancelled{false};
    std::atomic<bool> entered{false};
    std::atomic<bool> hold{false};
    bool opened = false;
    bool output = false;
    bool monitor = false;
    bool lose_apply_reply = false;
    bool reject_apply = false;
    bool bad_pins = false;
    std::atomic<bool> fail_poll{false};
    int calls = 0;
    int closes = 0;
    std::uint8_t last_command = 0;
    std::vector<std::uint8_t> last_apply;
    std::string identity = "m032-sgpio-bridge/1.1.0";
    void Delay() {
        entered = true;
        while (hold.load() && !cancelled.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void Connect(std::uint16_t, std::uint16_t, const std::wstring&, int) override {
        Delay();
        opened = true; // Deliberate late success after cancellation.
    }
    void Disconnect() override { if (opened) ++closes; opened = false; }
    bool IsConnected() const noexcept override { return opened; }
    void RequestCancel() noexcept override { cancelled = true; }
    void ResetCancel() noexcept override { cancelled = false; entered = false; }
    std::vector<mfc_tool::hid::DeviceInfo> Scan(std::uint16_t, std::uint16_t) const override { return {}; }
    std::vector<std::uint8_t> Transact(std::uint8_t cmd, const std::vector<std::uint8_t>& p) override {
        using namespace mfc_tool::core;
        ++calls;
        last_command = cmd;
        if (cancelled.load()) throw mfc_tool::hid::BridgeException("synthetic cancelled");
        if (!opened) throw mfc_tool::hid::BridgeException("synthetic disconnected");
        if (cmd == CMD_GET_INFO) {
            std::vector<std::uint8_t> data(identity.begin(), identity.end()); data.push_back(0); data.push_back(0); return data;
        }
        if (cmd == CMD_SGPIO_STATUS) {
            std::vector<std::uint8_t> d = {0,1,8,0,0xA0,0x86,1,0,100,0,1,0,0,0,0,0,2,0,3,1,0,0,0,0};
            d[0] = output ? 1u : 0u;
            if (bad_pins) d[16] = 7;
            return d;
        }
        if (cmd == CMD_SGPIO_MONITOR_STATUS) {
            if (fail_poll) throw mfc_tool::hid::BridgeException("synthetic response lost");
            std::vector<std::uint8_t> d(37,0); d[0] = monitor ? 1u : 0u;
            d[12]=0; d[13]=0xD4; d[14]=0x30; // 3,200,000 Sa/s.
            d[34]=6; d[35]=4; d[36]=5; return d;
        }
        if (cmd == CMD_SGPIO_APPLY) {
            if (reject_apply) throw mfc_tool::hid::BridgeStatusException(7);
            last_apply = p; output = p.at(0) != 0;
            if (lose_apply_reply) throw mfc_tool::hid::BridgeException("synthetic accepted, reply lost");
            return p;
        }
        if (cmd == CMD_SGPIO_MONITOR_ARM) { monitor = true; return {1,6,4,5}; }
        if (cmd == CMD_SGPIO_MONITOR_STOP) { monitor = false; return {0}; }
        if (cmd == CMD_SGPIO_OFF) { output = false; return {0}; }
        return p;
    }
};
