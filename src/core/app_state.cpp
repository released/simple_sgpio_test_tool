#include "app_state.h"

#include <cwctype>

namespace mfc_tool::core {
namespace {

bool IsAbsolutePath(const std::wstring& path) {
    if (path.size() >= 2u && path[1] == L':') {
        return true;
    }
    return path.size() >= 2u &&
           ((path[0] == L'\\' && path[1] == L'\\') ||
            (path[0] == L'/' && path[1] == L'/'));
}

std::wstring LowerPath(std::wstring path) {
    for (wchar_t& ch : path) {
        if (ch == L'/') {
            ch = L'\\';
        } else {
            ch = static_cast<wchar_t>(towlower(ch));
        }
    }
    return path;
}

std::wstring ToExeRelativePath(const std::wstring& path) {
    if (path.empty() || !IsAbsolutePath(path)) {
        return path;
    }

    const std::wstring root = config::IniManager::DefaultIniPath(L"");
    const std::wstring lower_path = LowerPath(path);
    const std::wstring lower_root = LowerPath(root);
    if (lower_root.empty() || lower_path.find(lower_root) != 0u) {
        return path;
    }
    return path.substr(root.size());
}

std::wstring GetValue(
    const config::IniData& data,
    const std::wstring& section,
    const std::wstring& key,
    const std::wstring& fallback
) {
    auto sec_it = data.find(section);
    if (sec_it == data.end()) {
        return fallback;
    }
    auto key_it = sec_it->second.find(key);
    if (key_it == sec_it->second.end()) {
        return fallback;
    }
    return key_it->second;
}

} // namespace

AppState AppState::Default() {
    return AppState{};
}

config::IniData AppState::ToIniData(const std::wstring& ini_path) const {
    config::IniData out;
    out[L"APP"] = {
        {L"ini_path", ToExeRelativePath(ini_path)}
    };

    out[L"UI"] = {
        {L"vid", ui.vid},
        {L"pid", ui.pid},
        {L"timeout_ms", ui.timeout_ms},
        {L"device_label", ui.device_label},
        {L"save_log_checked", ui.save_log_checked ? L"1" : L"0"},
        {L"expected_fw_version", ui.expected_fw_version},
        {L"last_seen_fw_version", ui.last_seen_fw_version}
    };

    out[L"SGPIO"] = {
        {L"slot_count", sgpio.slot_count},
        {L"clock_hz", sgpio.clock_hz},
        {L"periodic", sgpio.periodic},
        {L"interval_ms", sgpio.interval_ms},
        {L"sload_raw", sgpio.sload_raw},
        {L"act_mask", sgpio.act_mask},
        {L"locate_mask", sgpio.locate_mask},
        {L"fail_mask", sgpio.fail_mask}
    };

    return out;
}

void AppState::ApplyIniData(const config::IniData& data) {
    ui.vid = GetValue(data, L"UI", L"vid", ui.vid);
    ui.pid = GetValue(data, L"UI", L"pid", ui.pid);
    ui.timeout_ms = GetValue(data, L"UI", L"timeout_ms", ui.timeout_ms);
    ui.device_label = GetValue(data, L"UI", L"device_label", ui.device_label);
    ui.save_log_checked = GetValue(data, L"UI", L"save_log_checked", ui.save_log_checked ? L"1" : L"0") == L"1";
    ui.last_seen_fw_version = GetValue(data, L"UI", L"last_seen_fw_version", ui.last_seen_fw_version);

    sgpio.slot_count = GetValue(data, L"SGPIO", L"slot_count", sgpio.slot_count);
    sgpio.clock_hz = GetValue(data, L"SGPIO", L"clock_hz", sgpio.clock_hz);
    sgpio.periodic = GetValue(data, L"SGPIO", L"periodic", sgpio.periodic);
    sgpio.interval_ms = GetValue(data, L"SGPIO", L"interval_ms", sgpio.interval_ms);
    sgpio.sload_raw = GetValue(data, L"SGPIO", L"sload_raw", GetValue(data, L"SGPIO", L"vendor_bits", sgpio.sload_raw));
    sgpio.act_mask = GetValue(data, L"SGPIO", L"act_mask", sgpio.act_mask);
    sgpio.locate_mask = GetValue(data, L"SGPIO", L"locate_mask", sgpio.locate_mask);
    sgpio.fail_mask = GetValue(data, L"SGPIO", L"fail_mask", sgpio.fail_mask);
}

} // namespace mfc_tool::core
