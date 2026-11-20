#pragma once

#include <cstdint>
#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

#include "hid_types.h"

namespace mfc_tool::hid {

class BridgeException : public std::runtime_error {
public:
    explicit BridgeException(const std::string& msg) : std::runtime_error(msg) {}
};

class BridgeStatusException : public BridgeException {
public:
    explicit BridgeStatusException(std::uint8_t status);
    [[nodiscard]] std::uint8_t status() const noexcept { return status_; }

private:
    std::uint8_t status_ = 0;
};

class BridgeTransport {
public:
    virtual ~BridgeTransport() = default;
    virtual void Connect(std::uint16_t vid, std::uint16_t pid, const std::wstring& path, int timeout_ms) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const noexcept = 0;
    virtual std::vector<std::uint8_t> Transact(std::uint8_t cmd, const std::vector<std::uint8_t>& payload) = 0;
    virtual void RequestCancel() noexcept = 0;
    virtual void ResetCancel() noexcept = 0;
    virtual std::vector<DeviceInfo> Scan(std::uint16_t vid, std::uint16_t pid) const = 0;
};

class HidBridgeClient : public BridgeTransport {
public:
    HidBridgeClient() = default;
    ~HidBridgeClient();

    HidBridgeClient(const HidBridgeClient&) = delete;
    HidBridgeClient& operator=(const HidBridgeClient&) = delete;

    void Connect(std::uint16_t vid, std::uint16_t pid, const std::wstring& path, int timeout_ms);
    void Disconnect();

    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] const DeviceInfo& CurrentDevice() const noexcept { return device_; }

    std::vector<std::uint8_t> Transact(std::uint8_t cmd, const std::vector<std::uint8_t>& payload);
    void RequestCancel() noexcept override { cancelled_ = true; }
    void ResetCancel() noexcept override { cancelled_ = false; }
    std::vector<DeviceInfo> Scan(std::uint16_t vid, std::uint16_t pid) const override;

private:
    void OpenHandle(const std::wstring& path);
    void PopulateDeviceMeta();

    std::vector<std::uint8_t> BuildWriteReport(const std::vector<std::uint8_t>& frame) const;
    std::vector<std::uint8_t> ReadFrame(std::uint8_t cmd, std::uint8_t seq, int timeout_ms);

    static std::string LastErrorMessage(const char* prefix);

private:
    void* handle_ = nullptr;
    DeviceInfo device_ = {};
    int timeout_ms_ = 2000;
    std::uint8_t seq_ = 1;
    std::atomic<bool> cancelled_{false};
};

} // namespace mfc_tool::hid
