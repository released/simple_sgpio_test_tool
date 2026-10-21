#pragma once

#include <set>
#include <string>
#include <vector>

namespace mfc_tool::core::fixed_topology {

inline const std::wstring kSgpio = L"SGPIO";
inline const std::wstring kSgpioMonitor = L"SGPIO_MONITOR";

[[nodiscard]] inline const std::vector<int>& SgpioPins() {
    static const std::vector<int> pins = {0, 1, 2, 3};
    return pins;
}

[[nodiscard]] inline const std::vector<int>& SgpioMonitorPins() {
    static const std::vector<int> pins = {4, 5, 6};
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
