#pragma once

#include <string>

#include "../config/ini_manager.h"
#include "fw_version.generated.h"

namespace mfc_tool::core {

struct UiState {
    std::wstring vid = L"0x0416";
    std::wstring pid = L"0x5020";
    std::wstring timeout_ms = L"2000";
    std::wstring device_label = L"Auto Select";
    bool save_log_checked = false;
    std::wstring expected_fw_version = M032_EXPECTED_FW_VERSION;
    std::wstring last_seen_fw_version = L"-";
};

struct SgpioState {
    std::wstring slot_count = L"8";
    std::wstring clock_hz = L"100000";
    std::wstring periodic = L"1";
    std::wstring interval_ms = L"100";
    std::wstring sload_raw = L"0x0";
    std::wstring act_mask = L"0x0001";
    std::wstring locate_mask = L"0x0000";
    std::wstring fail_mask = L"0x0000";
};

struct AppState {
    UiState ui;
    SgpioState sgpio;

    static AppState Default();

    config::IniData ToIniData(const std::wstring& ini_path) const;
    void ApplyIniData(const config::IniData& data);
};

} // namespace mfc_tool::core
