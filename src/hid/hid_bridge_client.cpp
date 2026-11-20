#include "hid_bridge_client.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <hidsdi.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "hid_scan.h"
#include "overlapped_io.h"
#include "../core/bridge_commands.h"

namespace mfc_tool::hid {
namespace {

constexpr std::uint8_t kReportSize = mfc_tool::core::kBridgeReportSize;
constexpr std::uint8_t kHeaderSize = mfc_tool::core::kBridgeHeaderSize;
constexpr std::uint8_t kMagic = mfc_tool::core::kBridgeMagic;
constexpr std::uint8_t kMaxPayload = mfc_tool::core::kBridgeMaxPayload;

HANDLE AsHandle(void* h) {
    return reinterpret_cast<HANDLE>(h);
}


void QueryCaps(HANDLE handle, DeviceInfo* info) {
    if (info == nullptr) {
        return;
    }
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(handle, &preparsed) || preparsed == nullptr) {
        return;
    }

    HIDP_CAPS caps = {};
    NTSTATUS status = HidP_GetCaps(preparsed, &caps);
    HidD_FreePreparsedData(preparsed);
    if (status != HIDP_STATUS_SUCCESS) {
        return;
    }

    info->usage_page = caps.UsagePage;
    info->usage = caps.Usage;
    info->input_report_len = caps.InputReportByteLength;
    info->output_report_len = caps.OutputReportByteLength;
    info->feature_report_len = caps.FeatureReportByteLength;
}

void QueryAttributes(HANDLE handle, DeviceInfo* info) {
    if (info == nullptr) {
        return;
    }
    HIDD_ATTRIBUTES attr = {};
    attr.Size = sizeof(attr);
    if (!HidD_GetAttributes(handle, &attr)) {
        return;
    }
    info->vendor_id = attr.VendorID;
    info->product_id = attr.ProductID;
}

std::wstring QueryWideString(HANDLE handle, BOOLEAN(__stdcall* query_fn)(HANDLE, PVOID, ULONG)) {
    wchar_t buffer[256] = {};
    if (!query_fn(handle, buffer, static_cast<ULONG>(sizeof(buffer)))) {
        return L"";
    }
    return std::wstring(buffer);
}

} // namespace

BridgeStatusException::BridgeStatusException(std::uint8_t status)
    : BridgeException([status]() {
        std::ostringstream oss;
        const char* narrow = "UNKNOWN";
        switch (status) {
        case 0x00: narrow = "OK"; break;
        case 0x01: narrow = "BAD_MAGIC"; break;
        case 0x02: narrow = "BAD_COMMAND"; break;
        case 0x03: narrow = "BAD_PAYLOAD"; break;
        case 0x04: narrow = "IO_ERROR"; break;
        case 0x05: narrow = "TIMEOUT"; break;
        case 0x06: narrow = "NOT_READY"; break;
        case 0x07: narrow = "BUSY"; break;
        case 0x08: narrow = "UNSUPPORTED"; break;
        default: break;
        }
        oss << "bridge status error: "
            << narrow
            << " (0x" << std::uppercase << std::hex << static_cast<int>(status) << ")";
        return oss.str();
    }()), status_(status) {}

HidBridgeClient::~HidBridgeClient() {
    Disconnect();
}

std::vector<DeviceInfo> HidBridgeClient::Scan(std::uint16_t vid, std::uint16_t pid) const {
    return EnumerateHidDevices(vid, pid, true);
}

void HidBridgeClient::Connect(std::uint16_t vid, std::uint16_t pid, const std::wstring& path, int timeout_ms) {
    Disconnect();

    timeout_ms_ = (std::max)(100, timeout_ms);
    seq_ = 1;

    if (cancelled_.load()) throw BridgeException("Connect cancelled");
    const auto devices = EnumerateHidDevices(vid, pid, true);
    std::vector<DeviceInfo> candidates;
    for (const auto& d : devices) {
        if (d.IsPreferredBridge() && d.HasBridgeReports() && (path.empty() || d.path == path)) candidates.push_back(d);
    }
    if (candidates.size() != 1) throw BridgeException("Select one currently enumerated SGPIO HID interface");
    device_ = candidates.front();
    const auto enumerated_serial = device_.serial_number;
    OpenHandle(device_.path);

    PopulateDeviceMeta();

    if (cancelled_.load() || device_.vendor_id != vid || device_.product_id != pid ||
        !device_.IsPreferredBridge() || !device_.HasBridgeReports() ||
        (!enumerated_serial.empty() && enumerated_serial != device_.serial_number)) {
        Disconnect();
        throw BridgeException("HID identity/report contract mismatch or connection cancelled");
    }
}

void HidBridgeClient::Disconnect() {
    HANDLE h = AsHandle(handle_);
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
        CancelIoEx(h, nullptr);
        CloseHandle(h);
    }
    handle_ = nullptr;
    device_ = {};
}

bool HidBridgeClient::IsConnected() const noexcept {
    HANDLE h = AsHandle(handle_);
    return h != nullptr && h != INVALID_HANDLE_VALUE;
}

void HidBridgeClient::OpenHandle(const std::wstring& path) {
    HANDLE h = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        throw BridgeException(LastErrorMessage("CreateFileW"));
    }

    handle_ = h;
    HidD_SetNumInputBuffers(h, 64);
}

void HidBridgeClient::PopulateDeviceMeta() {
    HANDLE h = AsHandle(handle_);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        return;
    }

    const auto path = device_.path;
    device_ = {};
    device_.path = path;
    QueryAttributes(h, &device_);
    QueryCaps(h, &device_);
    device_.manufacturer = QueryWideString(h, HidD_GetManufacturerString);
    device_.product = QueryWideString(h, HidD_GetProductString);
    device_.serial_number = QueryWideString(h, HidD_GetSerialNumberString);

}

std::vector<std::uint8_t> HidBridgeClient::BuildWriteReport(const std::vector<std::uint8_t>& frame) const {
    if (frame.size() != kReportSize) {
        throw BridgeException("bridge frame size must be 64 bytes");
    }

    std::uint16_t out_len = device_.output_report_len == 0 ? 65 : device_.output_report_len;
    if (out_len < kReportSize) {
        throw BridgeException("output report length too small");
    }

    if (out_len == kReportSize) {
        return frame;
    }

    std::vector<std::uint8_t> report(out_len, 0x00);
    report[0] = 0x00;
    std::memcpy(report.data() + 1, frame.data(), frame.size());
    return report;
}

std::vector<std::uint8_t> HidBridgeClient::ReadFrame(std::uint8_t cmd, std::uint8_t seq, int timeout_ms) {
    HANDLE h = AsHandle(handle_);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        throw BridgeException("device not connected");
    }

    const std::uint16_t in_len = device_.input_report_len == 0 ? 65 : device_.input_report_len;
    auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::milliseconds(std::max(100, timeout_ms));

    for (;;) {
        auto now = std::chrono::steady_clock::now();
        if (now - start >= timeout) {
            throw BridgeException("timeout waiting HID response");
        }

        int remain_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout - (now - start)).count());
        remain_ms = (std::max)(50, remain_ms);

        std::vector<std::uint8_t> read_buf(in_len, 0);
        read_buf.resize(IoWithTimeout(h, read_buf.data(), in_len, remain_ms, cancelled_, false));
        if (read_buf.empty()) {
            continue;
        }

        const std::uint8_t* frame = nullptr;
        size_t frame_size = 0;

        if (read_buf.size() >= kReportSize && read_buf[0] == kMagic) {
            frame = read_buf.data();
            frame_size = read_buf.size();
        } else if (read_buf.size() >= (kReportSize + 1) && read_buf[1] == kMagic) {
            frame = read_buf.data() + 1;
            frame_size = read_buf.size() - 1;
        } else {
            continue;
        }

        if (frame_size < kHeaderSize) {
            continue;
        }
        if (frame[1] != cmd || frame[2] != seq) {
            continue;
        }

        std::uint8_t status = frame[3];
        std::uint16_t payload_len = static_cast<std::uint16_t>(frame[4] | (frame[5] << 8));
        if (payload_len > kMaxPayload) {
            throw BridgeException("invalid response payload length");
        }
        if ((kHeaderSize + payload_len) > frame_size) {
            throw BridgeException("truncated response payload");
        }

        if (status != 0x00) {
            throw BridgeStatusException(status);
        }

        std::vector<std::uint8_t> payload(payload_len, 0);
        if (payload_len > 0) {
            std::memcpy(payload.data(), frame + kHeaderSize, payload_len);
        }
        return payload;
    }
}

std::vector<std::uint8_t> HidBridgeClient::Transact(std::uint8_t cmd, const std::vector<std::uint8_t>& payload) {
    HANDLE h = AsHandle(handle_);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        throw BridgeException("Device is not connected");
    }
    if (payload.size() > kMaxPayload) {
        throw BridgeException("payload too large");
    }

    const std::uint8_t seq = seq_;
    seq_ = static_cast<std::uint8_t>((seq_ + 1) & 0xFF);

    std::vector<std::uint8_t> frame(kReportSize, 0);
    frame[0] = kMagic;
    frame[1] = cmd;
    frame[2] = seq;
    frame[3] = 0;
    frame[4] = static_cast<std::uint8_t>(payload.size() & 0xFF);
    frame[5] = static_cast<std::uint8_t>((payload.size() >> 8) & 0xFF);
    std::copy(payload.begin(), payload.end(), frame.begin() + kHeaderSize);

    std::vector<std::uint8_t> report = BuildWriteReport(frame);
    DWORD written = IoWithTimeout(h, report.data(), static_cast<DWORD>(report.size()), timeout_ms_, cancelled_, true);
    if (written != report.size()) {
        throw BridgeException("failed to write HID report");
    }

    return ReadFrame(cmd, seq, timeout_ms_);
}

std::string HidBridgeClient::LastErrorMessage(const char* prefix) {
    DWORD err = GetLastError();
    std::ostringstream oss;
    oss << prefix << " failed, win32=" << err;
    return oss.str();
}

} // namespace mfc_tool::hid
