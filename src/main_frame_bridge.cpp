#include "main_frame.h"
#include "core/text_utils.h"
#include "core/fixed_topology.h"
#include "ui/layout_utils.h"
#include <algorithm>
#include <memory>
#include <array>

namespace {
constexpr UINT_PTR kBridgeJobTimer = 0x5301;
}

bool CMainFrame::StartBridgeJob(mfc_tool::core::BridgeJob::Work work, mfc_tool::core::BridgeJob::Completion done) {
    if (IsBusy() || closing_ || cancel_pending_) return false;
    service_.ResetCancel();
    try {
    if (!bridge_job_.Start(std::move(work), [this, done = std::move(done)](std::exception_ptr error) {
        completing_ = true;
        if (cancel_pending_ || closing_) {
            // Open/transaction has drained; close late-successful handles before releasing ownership.
            service_.Disconnect();
            connected_ = false;
            model_ = {};
            cancel_pending_ = false;
            sgpio_tab_.OnDisconnected();
            sgpio_monitor_tab_.OnDisconnected();
            AppendLog(L"Disconnected. Remote SGPIO state is unknown; no stop command was replayed.");
        } else {
            connected_ = service_.IsConnected();
            model_ = service_.Model();
            try { if (done) done(error); }
            catch (const std::exception& e) { AppendLog(L"Operation result error: " + AnsiToWide(e.what())); }
        }
        completing_ = false;
        SyncBridgeUi();
    })) return false;
    } catch (const std::exception& e) {
        SyncBridgeUi();
        ShowErrorBox(L"Bridge Worker", AnsiToWide(e.what()));
        return false;
    }
    SyncBridgeUi();
    return true;
}

void CMainFrame::SyncBridgeUi() { SetConnectionUi(connected_); }

void CMainFrame::SetConnectionUi(bool connected) {
    if (!::IsWindow(connect_btn_.GetSafeHwnd())) return;
    const bool busy = IsBusy() || closing_;
    mfc_tool::ui::SafeEnableWindow(connect_btn_, !busy && !connected);
    mfc_tool::ui::SafeEnableWindow(disconnect_btn_, !cancel_pending_ && (busy || connected));
    mfc_tool::ui::SafeEnableWindow(reset_mcu_btn_, !busy && connected);
    mfc_tool::ui::SafeEnableWindow(return_bootloader_btn_, !busy && connected);
    mfc_tool::ui::SafeEnableWindow(get_info_btn_, !busy && connected);
    mfc_tool::ui::SafeEnableWindow(ping_btn_, !busy && connected);
    for (CWnd* control : std::array<CWnd*,5>{&vid_edit_, &pid_edit_, &timeout_edit_, &device_combo_, &refresh_btn_}) {
        mfc_tool::ui::SafeEnableWindow(*control, !busy && !connected);
    }
    for (CWnd* control : std::array<CWnd*,3>{&save_ini_btn_, &load_ini_btn_, &reset_ini_btn_}) {
        mfc_tool::ui::SafeEnableWindow(*control, !busy);
    }
    SetControlText(status_chip_, cancel_pending_ || closing_ ? L"Cancelling" : busy ? L"Busy" : connected ? L"Connected" : L"Disconnected");
    status_chip_color_ = connected ? RGB(46, 139, 87) : RGB(95, 104, 117);
    UpdateStatusChipColor();
    // Publish both owners before either tab derives its enabled controls.
    pin_usage_.SetActive(mfc_tool::core::fixed_topology::kSgpio, model_.output != mfc_tool::core::PinState::Idle);
    pin_usage_.SetActive(mfc_tool::core::fixed_topology::kSgpioMonitor, model_.monitor != mfc_tool::core::PinState::Idle);
    sgpio_tab_.SetConnected(connected);
    sgpio_monitor_tab_.SetConnected(connected);
    sgpio_tab_.SetRuntime(busy, model_);
    sgpio_monitor_tab_.SetRuntime(busy, model_);
}

void CMainFrame::CancelBridgeJob() {
    if (cancel_pending_) return;
    cancel_pending_ = true;
    service_.RequestCancel();
    if (!bridge_job_.Busy()) {
        service_.Disconnect();
        model_ = {};
        connected_ = false;
        cancel_pending_ = false;
        sgpio_tab_.OnDisconnected();
        sgpio_monitor_tab_.OnDisconnected();
        AppendLog(L"Disconnected. Remote SGPIO state is unknown.");
    }
    SyncBridgeUi();
}

void CMainFrame::OnTimer(UINT_PTR timer_id) {
    if (timer_id != kBridgeJobTimer) { CFrameWnd::OnTimer(timer_id); return; }
    bridge_job_.Consume();
    if (closing_ && !IsBusy()) {
        KillTimer(kBridgeJobTimer);
        service_.Disconnect();
        CFrameWnd::OnClose();
    }
}

void CMainFrame::RefreshDevices() {
    if (IsBusy() || connected_) return;
    try {
        const int vid = mfc_tool::core::ParseInt(GetControlText(vid_edit_));
        const int pid = mfc_tool::core::ParseInt(GetControlText(pid_edit_));
        mfc_tool::core::SgpioModel::Require(vid >= 0 && vid <= 65535 && pid >= 0 && pid <= 65535, "VID/PID must be 16-bit values");
        auto devices = std::make_shared<std::vector<mfc_tool::hid::DeviceInfo>>();
        auto* service = &service_;
        StartBridgeJob([service, devices, vid, pid] { *devices = service->ScanDevices(static_cast<std::uint16_t>(vid), static_cast<std::uint16_t>(pid)); },
            [this, devices](std::exception_ptr error) {
                if (error) { try { std::rethrow_exception(error); } catch (const std::exception& e) {
                    ShowErrorBox(L"Refresh Error", AnsiToWide(e.what())); } return; }
                scanned_devices_ = *devices;
                std::sort(scanned_devices_.begin(), scanned_devices_.end(), [](const auto& a, const auto& b) { return DeviceLabel(a,0) < DeviceLabel(b,0); });
                device_combo_.ResetContent();
                device_combo_.AddString(L"Auto Select");
                for (size_t i = 0; i < scanned_devices_.size(); ++i) device_combo_.AddString(DeviceLabel(scanned_devices_[i], static_cast<int>(i)).c_str());
                device_combo_.SetCurSel(scanned_devices_.size() == 1 ? 1 : 0);
                AppendLog(L"Device scan complete: " + std::to_wstring(scanned_devices_.size()) + L" found");
            });
    } catch (const std::exception& e) { ShowErrorBox(L"Refresh Error", AnsiToWide(e.what())); }
}

void CMainFrame::OnBnClickedRefresh() { RefreshDevices(); }
void CMainFrame::OnBnClickedDisconnect() { CancelBridgeJob(); }

void CMainFrame::OnBnClickedConnect() {
    if (IsBusy() || connected_) return;
    try {
        const int vid = mfc_tool::core::ParseInt(GetControlText(vid_edit_));
        const int pid = mfc_tool::core::ParseInt(GetControlText(pid_edit_));
        const int timeout = mfc_tool::core::ParseInt(GetControlText(timeout_edit_));
        mfc_tool::core::SgpioModel::Require(vid >= 0 && vid <= 65535 && pid >= 0 && pid <= 65535 && timeout >= 100 && timeout <= 60000,
                                           "Invalid VID/PID or timeout (100..60000 ms)");
        const auto path = SelectedDevicePath();
        auto info = std::make_shared<std::vector<std::uint8_t>>();
        auto* service = &service_;
        StartBridgeJob([service, vid, pid, path, timeout, info] {
            service->Connect(static_cast<std::uint16_t>(vid), static_cast<std::uint16_t>(pid), path, timeout);
            *info = service->GetInfo();
        }, [this, info](std::exception_ptr error) {
            if (error) { try { std::rethrow_exception(error); } catch (const std::exception& e) {
                AppendLog(L"Connect failed: " + AnsiToWide(e.what()));
                ShowErrorBox(L"Connect Error", AnsiToWide(e.what()) + L"\r\nCheck the selected SGPIO board, firmware and USB connection, then Refresh and retry.");
            } return; }
            LogBridgeInfoPayload(*info, true);
        });
    } catch (const std::exception& e) { ShowErrorBox(L"Connect Error", AnsiToWide(e.what())); }
}

void CMainFrame::OnBnClickedGetInfo() {
    if (IsBusy() || !connected_) return;
    auto data = std::make_shared<std::vector<std::uint8_t>>();
    auto* service = &service_;
    StartBridgeJob([service, data] { *data = service->GetInfo(); }, [this, data](std::exception_ptr error) {
        if (error) { try { std::rethrow_exception(error); } catch (const std::exception& e) {
            ShowErrorBox(L"Get Info Error", AnsiToWide(e.what())); } }
        else LogBridgeInfoPayload(*data, false);
    });
}

void CMainFrame::OnBnClickedPing() {
    if (IsBusy() || !connected_) return;
    auto data = std::make_shared<std::vector<std::uint8_t>>();
    auto* service = &service_;
    StartBridgeJob([service, data] { *data = service->Ping({0xA1,0xB2,0xC3,0xD4}); }, [this, data](std::exception_ptr error) {
        if (error) { try { std::rethrow_exception(error); } catch (const std::exception& e) {
            ShowErrorBox(L"Ping Error", AnsiToWide(e.what())); } }
        else AppendLog(L"PING RX: " + mfc_tool::core::HexDump(*data));
    });
}

void CMainFrame::OnBnClickedResetMcu() {
    if (IsBusy() || !connected_) return;
    auto* service = &service_;
    StartBridgeJob([service] { service->ResetMcu(); }, [this](std::exception_ptr error) {
        if (error) { try { std::rethrow_exception(error); }
        catch (const mfc_tool::hid::BridgeStatusException& e) { ShowErrorBox(L"Reset MCU Rejected", AnsiToWide(e.what())); }
        catch (const std::exception& e) {
            ShowErrorBox(L"Reset MCU Error", AnsiToWide(e.what()) + L"\r\nOutcome unknown. Reconnect and read status before retrying."); } }
        else AppendLog(L"MCU reset acknowledged.");
    });
}

void CMainFrame::OnBnClickedReturnBootloader() {
    if (IsBusy() || !connected_) return;
    completing_ = true;
    SyncBridgeUi();
    const int answer = MessageBoxW(L"Return the bridge board to USB HID bootloader mode?\r\n\r\n"
        L"This erases the application CRC word. If programming is canceled, the board will stay in bootloader mode until a valid application image is programmed.",
        L"Return Bootloader", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    completing_ = false;
    SyncBridgeUi();
    if (answer != IDYES || IsBusy() || !connected_ || closing_) return;
    auto* service = &service_;
    StartBridgeJob([service] { service->EnterIap(); }, [this](std::exception_ptr error) {
        if (error) { try { std::rethrow_exception(error); }
        catch (const mfc_tool::hid::BridgeStatusException& e) { ShowErrorBox(L"Return Bootloader Rejected", AnsiToWide(e.what())); }
        catch (const std::exception& e) {
            ShowErrorBox(L"Return Bootloader Error", AnsiToWide(e.what()) + L"\r\nOutcome unknown. Check enumeration before retrying."); } }
        else AppendLog(L"Return Bootloader acknowledged; application CRC invalidated.");
    });
}
