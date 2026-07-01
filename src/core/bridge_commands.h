#pragma once

#include <cstdint>
#include <string>

namespace mfc_tool::core {

constexpr uint8_t kBridgeMagic = 0xA5;
constexpr uint8_t kBridgeReportSize = 64;
constexpr uint8_t kBridgeHeaderSize = 6;
constexpr uint8_t kBridgeMaxPayload = kBridgeReportSize - kBridgeHeaderSize;

constexpr uint8_t CMD_PING = 0x01;
constexpr uint8_t CMD_GET_INFO = 0x02;
constexpr uint8_t CMD_RESET_MCU = 0x03;

constexpr uint8_t CMD_SGPIO_CONFIG = 0x74;
constexpr uint8_t CMD_SGPIO_APPLY = 0x75;
constexpr uint8_t CMD_SGPIO_STATUS = 0x76;
constexpr uint8_t CMD_SGPIO_OFF = 0x77;

inline std::wstring StatusText(uint8_t status) {
    switch (status) {
    case 0x00: return L"OK";
    case 0x01: return L"BAD_MAGIC";
    case 0x02: return L"BAD_COMMAND";
    case 0x03: return L"BAD_PAYLOAD";
    case 0x04: return L"IO_ERROR";
    case 0x05: return L"TIMEOUT";
    case 0x06: return L"NOT_READY";
    case 0x07: return L"BUSY";
    case 0x08: return L"UNSUPPORTED";
    default: return L"UNKNOWN";
    }
}

} // namespace mfc_tool::core
