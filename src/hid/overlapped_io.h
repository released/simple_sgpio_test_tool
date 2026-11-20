#pragma once

#include <windows.h>
#include <atomic>

namespace mfc_tool::hid {
DWORD IoWithTimeout(HANDLE handle, void* buffer, DWORD size, int timeout_ms,
                    const std::atomic<bool>& cancelled, bool write);
}
