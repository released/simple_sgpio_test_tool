#pragma once

#include <set>
#include <string>
#include <vector>

namespace mfc_tool::core::fixed_topology {

inline const std::wstring kSgpio = L"SGPIO";
inline const std::wstring kSgpioMonitor = L"SGPIO_MONITOR";
inline constexpr int kOutputClock = 2;
inline constexpr int kOutputData = 0;
inline constexpr int kOutputLoad = 3;
inline constexpr int kInputData = 1;
inline constexpr int kMonitorClock = 6;
inline constexpr int kMonitorData = 4;
inline constexpr int kMonitorLoad = 5;

[[nodiscard]] inline const std::vector<int>& SgpioPins() {
    static const std::vector<int> pins = {kOutputData, kInputData, kOutputClock, kOutputLoad};
    return pins;
}

[[nodiscard]] inline const std::vector<int>& SgpioMonitorPins() {
    static const std::vector<int> pins = {kMonitorData, kMonitorLoad, kMonitorClock};
    return pins;
}

[[nodiscard]] inline const std::vector<std::wstring>& SharedFixedTopologyOwners() {
    static const std::vector<std::wstring> owners = {
        kSgpio,
        kSgpioMonitor
    };
    return owners;
}

[[nodiscard]] inline const std::set<std::wstring>& SgpioMonitorOwnerSet() {
    static const std::set<std::wstring> owners = {kSgpioMonitor};
    return owners;
}

[[nodiscard]] inline const std::set<std::wstring>& SgpioOwnerSet() {
    static const std::set<std::wstring> owners = {kSgpio};
    return owners;
}

} // namespace mfc_tool::core::fixed_topology
