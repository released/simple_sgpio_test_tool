#include "overlapped_io.h"
#include "hid_bridge_client.h"
#include <chrono>

namespace mfc_tool::hid {
DWORD IoWithTimeout(HANDLE handle, void* buffer, DWORD size, int timeout_ms,
                    const std::atomic<bool>& cancelled, bool write) {
    if (cancelled.load()) throw BridgeException("Operation cancelled; device outcome may be unknown");
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) throw BridgeException("CreateEvent failed");
    const BOOL ok = write ? WriteFile(handle, buffer, size, nullptr, &ov) : ReadFile(handle, buffer, size, nullptr, &ov);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        throw BridgeException(write ? "WriteFile failed" : "ReadFile failed");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    bool interrupted = false;
    for (;;) {
        const DWORD wait = WaitForSingleObject(ov.hEvent, 20);
        if (wait == WAIT_OBJECT_0) break;
        if (cancelled.load() || wait == WAIT_FAILED || std::chrono::steady_clock::now() >= deadline) {
            interrupted = true;
            CancelIoEx(handle, &ov);
            break;
        }
    }
    // Cancellation is a request. Keep buffer, OVERLAPPED and event alive until completion.
    // This drain runs only on the bridge worker, never on the interactive UI thread.
    DWORD transferred = 0;
    const BOOL completed = GetOverlappedResult(handle, &ov, &transferred, TRUE);
    CloseHandle(ov.hEvent);
    if (interrupted || cancelled.load()) throw BridgeException(cancelled.load() ?
        "Operation cancelled; device outcome may be unknown" : "I/O timeout; device outcome may be unknown");
    if (!completed) throw BridgeException("Overlapped I/O failed; device outcome may be unknown");
    return transferred;
}
} // namespace mfc_tool::hid
