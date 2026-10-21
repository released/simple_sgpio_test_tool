#include "sgpio_monitor_tab.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#include "../core/fixed_topology.h"
#include "../core/text_utils.h"
#include "layout_utils.h"

namespace fixed_topology = mfc_tool::core::fixed_topology;

namespace {

enum : UINT {
    IDC_MONITOR_GROUP = 12400,
    IDC_MONITOR_FIXED_PIN_TITLE,
    IDC_MONITOR_FIXED_PIN_VALUE,
    IDC_MONITOR_EXPECTED_SLOT_TITLE,
    IDC_MONITOR_EXPECTED_SLOT_COMBO,
    IDC_MONITOR_TIMEOUT_TITLE,
    IDC_MONITOR_TIMEOUT_EDIT,
    IDC_MONITOR_ARM,
    IDC_MONITOR_STOP,
    IDC_MONITOR_STATUS,
    IDC_MONITOR_STATE_TITLE,
    IDC_MONITOR_STATE_VALUE,
    IDC_MONITOR_CLOCK_TITLE,
    IDC_MONITOR_CLOCK_VALUE,
    IDC_MONITOR_TIMING_TITLE,
    IDC_MONITOR_TIMING_VALUE,
    IDC_MONITOR_FRAME_TITLE,
    IDC_MONITOR_FRAME_VALUE,
    IDC_MONITOR_CAPTURE_TITLE,
    IDC_MONITOR_CAPTURE_VALUE,
    IDC_MONITOR_ERRORS_TITLE,
    IDC_MONITOR_ERRORS_VALUE,
    IDC_MONITOR_SLOT_LIST
};

constexpr UINT_PTR kMonitorPollTimerId = 1u;
constexpr UINT kMonitorPollIntervalMs = 100u;
constexpr size_t kMonitorStatusSize = 37u;
constexpr std::uint8_t kMonitorStateIdle = 0u;
constexpr std::uint8_t kMonitorStateArmed = 1u;
constexpr std::uint8_t kMonitorStateCapturing = 2u;
constexpr std::uint8_t kMonitorStateComplete = 3u;
constexpr std::uint8_t kMonitorStateTimeout = 4u;
constexpr std::uint8_t kMonitorStateError = 5u;

std::uint16_t ReadLe16(const std::vector<std::uint8_t>& data, size_t offset) {
    if (offset + 1u >= data.size()) {
        return 0u;
    }
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1u]) << 8u);
}

std::uint32_t ReadLe32(const std::vector<std::uint8_t>& data, size_t offset) {
    if (offset + 3u >= data.size()) {
        return 0u;
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3u]) << 24u);
}

std::wstring HexNibble(std::uint8_t value) {
    wchar_t text[16] = {};
    swprintf_s(text, L"0x%X", static_cast<unsigned int>(value & 0x0Fu));
    return text;
}

} // namespace

BEGIN_MESSAGE_MAP(CSgpioMonitorTab, CWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_BN_CLICKED(IDC_MONITOR_ARM, &CSgpioMonitorTab::OnBtnArm)
    ON_BN_CLICKED(IDC_MONITOR_STOP, &CSgpioMonitorTab::OnBtnStop)
    ON_BN_CLICKED(IDC_MONITOR_STATUS, &CSgpioMonitorTab::OnBtnStatus)
END_MESSAGE_MAP()

BOOL CSgpioMonitorTab::Create(CWnd* parent, const RECT& rect, UINT id) {
    CString cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW),
                                      reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), nullptr);
    return CWnd::CreateEx(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                          rect, parent, id);
}

void CSgpioMonitorTab::Bind(mfc_tool::core::BridgeService* service,
                            std::function<void(const std::wstring&)> logger,
                            mfc_tool::core::PinUsageRegistry* pin_usage) {
    service_ = service;
    log_ = std::move(logger);
    pin_usage_ = pin_usage;
    if (pin_usage_ != nullptr) {
        pin_usage_->SetLabel(fixed_topology::kSgpioMonitor, L"SGPIO Monitor");
        pin_usage_->SetClaim(fixed_topology::kSgpioMonitor, fixed_topology::SgpioMonitorPins());
        pin_usage_->SetActive(fixed_topology::kSgpioMonitor, false);
    }
}

int CSgpioMonitorTab::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CWnd::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    mfc_tool::ui::CreatePointFontForWindow(ui_font_, *this, 85);
    mfc_tool::ui::CreatePointFontForWindow(slot_font_, *this, 80);
    auto mk_static = [this](CStatic& control, const wchar_t* text, UINT id) {
        control.Create(text, WS_CHILD | WS_VISIBLE, CRect(), this, id);
        control.SetFont(&ui_font_);
    };
    auto mk_button = [this](CButton& control, const wchar_t* text, UINT id, DWORD style = BS_PUSHBUTTON) {
        control.Create(text, WS_CHILD | WS_VISIBLE | style, CRect(), this, id);
        control.SetFont(&ui_font_);
    };

    mk_button(group_, L"SGPIO Monitor (external source input)", IDC_MONITOR_GROUP, BS_GROUPBOX);
    mk_static(fixed_pin_title_, L"Fixed Input Pins", IDC_MONITOR_FIXED_PIN_TITLE);
    mk_static(fixed_pin_value_, L"PA6=SCLOCK  PA4=SDATA  PA5=SLOAD  (3.3 V logic, common GND)",
              IDC_MONITOR_FIXED_PIN_VALUE);
    fixed_pin_value_.ModifyStyle(SS_TYPEMASK, SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS);

    mk_static(expected_slot_title_, L"Expected Slots", IDC_MONITOR_EXPECTED_SLOT_TITLE);
    expected_slot_combo_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                CRect(), this, IDC_MONITOR_EXPECTED_SLOT_COMBO);
    expected_slot_combo_.SetFont(&ui_font_);
    expected_slot_combo_.AddString(L"Auto Detect");
    expected_slot_combo_.AddString(L"4");
    expected_slot_combo_.AddString(L"8");
    expected_slot_combo_.AddString(L"16");
    expected_slot_combo_.SetCurSel(0);

    mk_static(timeout_title_, L"Trigger Timeout(ms)", IDC_MONITOR_TIMEOUT_TITLE);
    timeout_edit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                         CRect(), this, IDC_MONITOR_TIMEOUT_EDIT);
    timeout_edit_.SetFont(&ui_font_);
    timeout_edit_.SetWindowTextW(L"3000");
    mk_button(arm_btn_, L"Arm", IDC_MONITOR_ARM);
    mk_button(stop_btn_, L"Stop", IDC_MONITOR_STOP);
    mk_button(status_btn_, L"Get Status", IDC_MONITOR_STATUS);

    mk_static(state_title_, L"State", IDC_MONITOR_STATE_TITLE);
    mk_static(state_value_, L"Idle", IDC_MONITOR_STATE_VALUE);
    mk_static(clock_title_, L"Clock", IDC_MONITOR_CLOCK_TITLE);
    mk_static(clock_value_, L"-", IDC_MONITOR_CLOCK_VALUE);
    mk_static(timing_title_, L"High / Low", IDC_MONITOR_TIMING_TITLE);
    mk_static(timing_value_, L"-", IDC_MONITOR_TIMING_VALUE);
    mk_static(frame_title_, L"Frame", IDC_MONITOR_FRAME_TITLE);
    mk_static(frame_value_, L"-", IDC_MONITOR_FRAME_VALUE);
    mk_static(capture_title_, L"Capture", IDC_MONITOR_CAPTURE_TITLE);
    mk_static(capture_value_, L"-", IDC_MONITOR_CAPTURE_VALUE);
    mk_static(errors_title_, L"Errors", IDC_MONITOR_ERRORS_TITLE);
    mk_static(errors_value_, L"None", IDC_MONITOR_ERRORS_VALUE);

    slot_list_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER,
                      CRect(), this, IDC_MONITOR_SLOT_LIST);
    slot_list_.SetFont(&slot_font_);
    slot_list_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    slot_list_.InsertColumn(0, L"Slot", LVCFMT_LEFT, 80);
    slot_list_.InsertColumn(1, L"ACT", LVCFMT_CENTER, 100);
    slot_list_.InsertColumn(2, L"LOCATE", LVCFMT_CENTER, 120);
    slot_list_.InsertColumn(3, L"FAIL", LVCFMT_CENTER, 100);
    for (int i = 0; i < 16; ++i) {
        const std::wstring slot = std::to_wstring(i);
        slot_list_.InsertItem(i, slot.c_str());
        slot_list_.SetItemText(i, 1, L"-");
        slot_list_.SetItemText(i, 2, L"-");
        slot_list_.SetItemText(i, 3, L"-");
    }

    UpdateEnableState();
    return 0;
}

void CSgpioMonitorTab::OnSize(UINT nType, int cx, int cy) {
    CWnd::OnSize(nType, cx, cy);
    if (cx > 0 && cy > 0) {
        LayoutControls(CRect(0, 0, cx, cy));
    }
}

void CSgpioMonitorTab::RefreshDpiLayout() {
    if (!::IsWindow(GetSafeHwnd())) {
        return;
    }
    mfc_tool::ui::CreatePointFontForWindow(ui_font_, *this, 85);
    mfc_tool::ui::CreatePointFontForWindow(slot_font_, *this, 80);
    mfc_tool::ui::ApplyFontToChildWindows(*this, ui_font_, FALSE);
    slot_list_.SetFont(&slot_font_, FALSE);
    CRect client;
    GetClientRect(&client);
    LayoutControls(client);
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CSgpioMonitorTab::LayoutControls(const CRect& client) {
    if (!::IsWindow(group_.GetSafeHwnd())) {
        return;
    }

    const mfc_tool::ui::DpiScaler dpi = mfc_tool::ui::DpiScaler::FromWindow(*this);
    const mfc_tool::ui::LayoutMetrics metrics = mfc_tool::ui::MetricsForWindow(*this);
    const int margin = metrics.margin8;
    const int gap = metrics.gap;
    const int row = metrics.row26;
    const int label_h = metrics.label18;
    const int left = client.left + margin + dpi.Scale(12);
    const int right = client.right - margin - dpi.Scale(12);
    const int bottom = client.bottom - margin - dpi.Scale(12);
    int y = client.top + margin + dpi.Scale(28);
    int x = left;

    mfc_tool::ui::SafeMoveWindow(group_, client.left + margin, client.top + margin,
                                 client.Width() - margin * 2, client.Height() - margin * 2);

    x += mfc_tool::ui::PlaceLabel(fixed_pin_title_, x, y + dpi.Scale(3), dpi.Scale(8), label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(fixed_pin_value_, x, y + dpi.Scale(3),
                                 (std::max)(dpi.Scale(220), right - x), label_h);

    y += row + gap;
    x = left;
    x = mfc_tool::ui::PlaceLabelAndControl(expected_slot_title_, expected_slot_combo_, x,
                                           y + dpi.Scale(4), y, dpi.Scale(116),
                                           row + metrics.comboDrop, gap, dpi.Scale(8), label_h) + dpi.Scale(12);
    x = mfc_tool::ui::PlaceLabelAndControl(timeout_title_, timeout_edit_, x,
                                           y + dpi.Scale(4), y, dpi.Scale(78), row,
                                           gap, dpi.Scale(8), label_h) + dpi.Scale(12);
    {
        const int arm_w = (std::max)(dpi.Scale(76), mfc_tool::ui::MeasureButtonMinWidth(arm_btn_, dpi.Scale(20)));
        const int stop_w = (std::max)(dpi.Scale(76), mfc_tool::ui::MeasureButtonMinWidth(stop_btn_, dpi.Scale(20)));
        const int status_w = (std::max)(dpi.Scale(92), mfc_tool::ui::MeasureButtonMinWidth(status_btn_, dpi.Scale(20)));
        mfc_tool::ui::SafeMoveWindow(arm_btn_, x, y, arm_w, row);
        x += arm_w + gap;
        mfc_tool::ui::SafeMoveWindow(stop_btn_, x, y, stop_w, row);
        x += stop_w + gap;
        mfc_tool::ui::SafeMoveWindow(status_btn_, x, y, status_w, row);
        x += status_w + dpi.Scale(14);
    }
    x += mfc_tool::ui::PlaceLabel(state_title_, x, y + dpi.Scale(4), dpi.Scale(8), label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(state_value_, x, y + dpi.Scale(4),
                                 (std::max)(dpi.Scale(90), right - x), label_h);

    y += row + gap;
    x = left;
    x += mfc_tool::ui::PlaceLabel(clock_title_, x, y + dpi.Scale(3), dpi.Scale(8), label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(clock_value_, x, y + dpi.Scale(3), dpi.Scale(170), label_h);
    x += dpi.Scale(170) + dpi.Scale(14);
    x += mfc_tool::ui::PlaceLabel(timing_title_, x, y + dpi.Scale(3), dpi.Scale(8), label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(timing_value_, x, y + dpi.Scale(3), dpi.Scale(210), label_h);
    x += dpi.Scale(210) + dpi.Scale(14);
    x += mfc_tool::ui::PlaceLabel(frame_title_, x, y + dpi.Scale(3), dpi.Scale(8), label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(frame_value_, x, y + dpi.Scale(3),
                                 (std::max)(dpi.Scale(180), right - x), label_h);

    y += row;
    x = left;
    x += mfc_tool::ui::PlaceLabel(capture_title_, x, y + dpi.Scale(3), dpi.Scale(8), label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(capture_value_, x, y + dpi.Scale(3), dpi.Scale(350), label_h);
    x += dpi.Scale(350) + dpi.Scale(14);
    x += mfc_tool::ui::PlaceLabel(errors_title_, x, y + dpi.Scale(3), dpi.Scale(8), label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(errors_value_, x, y + dpi.Scale(3),
                                 (std::max)(dpi.Scale(200), right - x), label_h);

    y += row + gap;
    mfc_tool::ui::SafeMoveWindow(slot_list_, left, y, right - left,
                                 (std::max)(dpi.Scale(64), bottom - y));
    {
        const int available = (std::max)(dpi.Scale(360), right - left - dpi.Scale(4));
        const int slot_w = dpi.Scale(76);
        const int value_w = (available - slot_w) / 3;
        slot_list_.SetColumnWidth(0, slot_w);
        slot_list_.SetColumnWidth(1, value_w);
        slot_list_.SetColumnWidth(2, value_w);
        slot_list_.SetColumnWidth(3, available - slot_w - value_w * 2);
    }
}

void CSgpioMonitorTab::SetConnected(bool connected) {
    connected_ = connected;
    if (!connected_) {
        OnDisconnected();
        return;
    }
    UpdateEnableState();
}

void CSgpioMonitorTab::OnDisconnected() {
    connected_ = false;
    armed_ = false;
    current_state_ = kMonitorStateIdle;
    StopPolling();
    if (::IsWindow(state_value_.GetSafeHwnd())) {
        state_value_.SetWindowTextW(L"Disconnected");
    }
    RefreshPinUsage();
    UpdateEnableState();
}

void CSgpioMonitorTab::LoadState(const mfc_tool::core::AppState& state) {
    int expected = 0;
    try {
        expected = mfc_tool::core::ParseInt(state.sgpio_monitor.expected_slot_count);
    } catch (...) {
        expected = 0;
    }
    SetExpectedSlotCount(expected);
    timeout_edit_.SetWindowTextW(state.sgpio_monitor.trigger_timeout_ms.c_str());
    armed_ = false;
    current_state_ = kMonitorStateIdle;
    StopPolling();
    state_value_.SetWindowTextW(L"Idle");
    clock_value_.SetWindowTextW(L"-");
    timing_value_.SetWindowTextW(L"-");
    frame_value_.SetWindowTextW(L"-");
    capture_value_.SetWindowTextW(L"-");
    errors_value_.SetWindowTextW(L"None");
    UpdateSlotRows(0, 0u, 0u, 0u);
    RefreshPinUsage();
    UpdateEnableState();
}

void CSgpioMonitorTab::SaveState(mfc_tool::core::AppState* state) const {
    if (state == nullptr) {
        return;
    }
    state->sgpio_monitor.expected_slot_count = std::to_wstring(ExpectedSlotCount());
    state->sgpio_monitor.trigger_timeout_ms = GetTimeoutText();
}

int CSgpioMonitorTab::ExpectedSlotCount() const {
    switch (expected_slot_combo_.GetCurSel()) {
    case 1: return 4;
    case 2: return 8;
    case 3: return 16;
    default: return 0;
    }
}

void CSgpioMonitorTab::SetExpectedSlotCount(int slot_count) {
    int selection = 0;
    if (slot_count == 4) {
        selection = 1;
    } else if (slot_count == 8) {
        selection = 2;
    } else if (slot_count == 16) {
        selection = 3;
    }
    expected_slot_combo_.SetCurSel(selection);
}

std::wstring CSgpioMonitorTab::GetTimeoutText() const {
    CString text;
    const_cast<CEdit&>(timeout_edit_).GetWindowTextW(text);
    return text.GetString();
}

void CSgpioMonitorTab::UpdateEnableState() {
    if (!::IsWindow(arm_btn_.GetSafeHwnd())) {
        return;
    }
    BOOL shared_blocked = FALSE;
    if (pin_usage_ != nullptr) {
        shared_blocked = pin_usage_->AnyActiveExcept(fixed_topology::SharedFixedTopologyOwners(),
                                                     fixed_topology::SgpioMonitorOwnerSet()) ? TRUE : FALSE;
    }
    mfc_tool::ui::SafeEnableWindow(arm_btn_, connected_ && !armed_ && !shared_blocked ? TRUE : FALSE);
    mfc_tool::ui::SafeEnableWindow(stop_btn_, connected_ && armed_ ? TRUE : FALSE);
    mfc_tool::ui::SafeEnableWindow(status_btn_, connected_ ? TRUE : FALSE);
    mfc_tool::ui::SafeEnableWindow(expected_slot_combo_, connected_ && !armed_ ? TRUE : FALSE);
    mfc_tool::ui::SafeEnableWindow(timeout_edit_, connected_ && !armed_ ? TRUE : FALSE);
}

void CSgpioMonitorTab::RefreshPinUsage() {
    if (pin_usage_ == nullptr) {
        return;
    }
    pin_usage_->SetLabel(fixed_topology::kSgpioMonitor, L"SGPIO Monitor");
    pin_usage_->SetClaim(fixed_topology::kSgpioMonitor, fixed_topology::SgpioMonitorPins());
    pin_usage_->SetActive(fixed_topology::kSgpioMonitor, armed_);
}

void CSgpioMonitorTab::StartPolling() {
    StopPolling();
    polling_ = SetTimer(kMonitorPollTimerId, kMonitorPollIntervalMs, nullptr) != 0u;
}

void CSgpioMonitorTab::StopPolling() {
    if (polling_ && ::IsWindow(GetSafeHwnd())) {
        KillTimer(kMonitorPollTimerId);
    }
    polling_ = false;
}

void CSgpioMonitorTab::OnTimer(UINT_PTR timer_id) {
    if (timer_id == kMonitorPollTimerId) {
        ReadAndDisplayStatus(true);
        return;
    }
    CWnd::OnTimer(timer_id);
}

void CSgpioMonitorTab::OnBtnArm() {
    try {
        if (!connected_ || service_ == nullptr) {
            throw std::runtime_error("bridge is not connected");
        }
        const int timeout_ms = mfc_tool::core::ParseInt(GetTimeoutText());
        if (timeout_ms < 100 || timeout_ms > 30000) {
            throw std::runtime_error("trigger timeout must be 100..30000 ms");
        }
        if (pin_usage_ != nullptr &&
            pin_usage_->AnyActiveExcept(fixed_topology::SharedFixedTopologyOwners(),
                                        fixed_topology::SgpioMonitorOwnerSet())) {
            throw std::runtime_error("stop SGPIO Output before arming SGPIO Monitor");
        }
        if (pin_usage_ != nullptr &&
            pin_usage_->AnyPinOccupied(fixed_topology::SgpioMonitorPins(),
                                       fixed_topology::SgpioMonitorOwnerSet())) {
            throw std::runtime_error("one or more monitor pins are already occupied");
        }

        service_->SgpioMonitorConfig(ExpectedSlotCount(), timeout_ms);
        const auto response = service_->SgpioMonitorArm();
        if (response.empty() || response[0] != kMonitorStateArmed) {
            throw std::runtime_error("firmware did not enter Armed state");
        }

        armed_ = true;
        current_state_ = kMonitorStateArmed;
        state_value_.SetWindowTextW(L"Armed - waiting for SLOAD falling edge");
        errors_value_.SetWindowTextW(L"None");
        RefreshPinUsage();
        UpdateEnableState();
        StartPolling();
        if (log_) {
            log_(L"SGPIO Monitor armed: PA6=SCLOCK, PA4=SDATA, PA5=SLOAD.");
        }
    } catch (const std::exception& e) {
        const std::wstring message = AnsiToWide(e.what());
        if (log_) {
            log_(L"SGPIO Monitor arm failed: " + message);
        }
        MessageBoxW(message.c_str(), L"SGPIO Monitor", MB_OK | MB_ICONERROR);
    }
}

void CSgpioMonitorTab::OnBtnStop() {
    try {
        if (service_ != nullptr && connected_) {
            service_->SgpioMonitorStop();
        }
        StopPolling();
        armed_ = false;
        current_state_ = kMonitorStateIdle;
        state_value_.SetWindowTextW(L"Idle");
        RefreshPinUsage();
        UpdateEnableState();
        if (log_) {
            log_(L"SGPIO Monitor stopped.");
        }
    } catch (const std::exception& e) {
        const std::wstring message = AnsiToWide(e.what());
        if (log_) {
            log_(L"SGPIO Monitor stop failed: " + message);
        }
        MessageBoxW(message.c_str(), L"SGPIO Monitor", MB_OK | MB_ICONERROR);
    }
}

void CSgpioMonitorTab::OnBtnStatus() {
    ReadAndDisplayStatus(false);
}

void CSgpioMonitorTab::ReadAndDisplayStatus(bool from_poll) {
    try {
        if (!connected_ || service_ == nullptr) {
            throw std::runtime_error("bridge is not connected");
        }
        ApplyStatus(service_->SgpioMonitorStatus());
    } catch (const std::exception& e) {
        const std::wstring message = AnsiToWide(e.what());
        if (log_) {
            log_(L"SGPIO Monitor status failed: " + message);
        }
        if (from_poll) {
            StopPolling();
            armed_ = false;
            RefreshPinUsage();
            UpdateEnableState();
        } else {
            MessageBoxW(message.c_str(), L"SGPIO Monitor", MB_OK | MB_ICONERROR);
        }
    }
}

void CSgpioMonitorTab::ApplyStatus(const std::vector<std::uint8_t>& data) {
    if (data.size() < kMonitorStatusSize) {
        throw std::runtime_error("monitor status response is shorter than 37 bytes");
    }

    const bool was_armed = armed_;
    const std::uint8_t state = data[0];
    const int expected_slots = static_cast<int>(data[1]);
    const int detected_slots = static_cast<int>(data[2]);
    const std::uint8_t sload_raw = data[3];
    const std::uint16_t errors = ReadLe16(data, 4u);
    const std::uint16_t samples = ReadLe16(data, 6u);
    const std::uint16_t rising_edges = ReadLe16(data, 8u);
    const std::uint16_t decoded_bits = ReadLe16(data, 10u);
    const std::uint32_t sample_rate = ReadLe32(data, 12u);
    const std::uint32_t clock_hz = ReadLe32(data, 16u);
    const std::uint32_t high_ns = ReadLe32(data, 20u);
    const std::uint32_t low_ns = ReadLe32(data, 24u);
    const std::uint16_t act_mask = ReadLe16(data, 28u);
    const std::uint16_t locate_mask = ReadLe16(data, 30u);
    const std::uint16_t fail_mask = ReadLe16(data, 32u);
    std::wstringstream text;

    current_state_ = state;
    armed_ = state == kMonitorStateArmed || state == kMonitorStateCapturing;
    state_value_.SetWindowTextW(StateText(state).c_str());

    if (clock_hz == 0u) {
        clock_value_.SetWindowTextW(L"-");
    } else {
        text << clock_hz << L" Hz";
        clock_value_.SetWindowTextW(text.str().c_str());
    }

    text.str(L"");
    text.clear();
    if (high_ns == 0u && low_ns == 0u) {
        timing_value_.SetWindowTextW(L"-");
    } else {
        text << high_ns << L" ns / " << low_ns << L" ns";
        timing_value_.SetWindowTextW(text.str().c_str());
    }

    text.str(L"");
    text.clear();
    text << L"Expected=" << (expected_slots == 0 ? L"Auto" : std::to_wstring(expected_slots))
         << L"  Detected=" << detected_slots
         << L"  SLOAD=" << HexNibble(sload_raw)
         << L"  Bits=" << decoded_bits;
    frame_value_.SetWindowTextW(text.str().c_str());

    text.str(L"");
    text.clear();
    text << L"Samples=" << samples << L"  Rising=" << rising_edges
         << L"  Rate=" << sample_rate << L" Sa/s";
    capture_value_.SetWindowTextW(text.str().c_str());
    errors_value_.SetWindowTextW(ErrorText(errors).c_str());
    UpdateSlotRows(detected_slots > 0 ? detected_slots : expected_slots,
                   act_mask, locate_mask, fail_mask);

    if (!armed_) {
        StopPolling();
    }
    RefreshPinUsage();
    UpdateEnableState();

    if (was_armed && !armed_ && log_) {
        text.str(L"");
        text.clear();
        text << L"SGPIO Monitor " << StateText(state)
             << L": clock=" << clock_hz << L" Hz, slots=" << detected_slots
             << L", errors=" << ErrorText(errors) << L".";
        log_(text.str());
    }
}

void CSgpioMonitorTab::UpdateSlotRows(int slot_count, std::uint16_t act_mask,
                                      std::uint16_t locate_mask, std::uint16_t fail_mask) {
    if (!::IsWindow(slot_list_.GetSafeHwnd())) {
        return;
    }
    if (slot_count != 4 && slot_count != 8 && slot_count != 16) {
        slot_count = 0;
    }
    for (int i = 0; i < 16; ++i) {
        const bool in_frame = i < slot_count;
        slot_list_.SetItemText(i, 1, in_frame ? (((act_mask >> i) & 1u) != 0u ? L"1" : L"0") : L"-");
        slot_list_.SetItemText(i, 2, in_frame ? (((locate_mask >> i) & 1u) != 0u ? L"1" : L"0") : L"-");
        slot_list_.SetItemText(i, 3, in_frame ? (((fail_mask >> i) & 1u) != 0u ? L"1" : L"0") : L"-");
    }
}

std::wstring CSgpioMonitorTab::StateText(std::uint8_t state) {
    switch (state) {
    case kMonitorStateIdle: return L"Idle";
    case kMonitorStateArmed: return L"Armed - waiting for SLOAD falling edge";
    case kMonitorStateCapturing: return L"Capturing";
    case kMonitorStateComplete: return L"Complete";
    case kMonitorStateTimeout: return L"Timeout";
    case kMonitorStateError: return L"Error";
    default: return L"Unknown";
    }
}

std::wstring CSgpioMonitorTab::ErrorText(std::uint16_t flags) {
    if (flags == 0u) {
        return L"None";
    }
    std::wstring result;
    auto append = [&result](const wchar_t* text) {
        if (!result.empty()) {
            result += L", ";
        }
        result += text;
    };
    if ((flags & 0x0001u) != 0u) append(L"No trigger");
    if ((flags & 0x0002u) != 0u) append(L"No clock");
    if ((flags & 0x0004u) != 0u) append(L"Clock out of range");
    if ((flags & 0x0008u) != 0u) append(L"SLOAD sync");
    if ((flags & 0x0010u) != 0u) append(L"Incomplete frame");
    if ((flags & 0x0020u) != 0u) append(L"Capture timeout");
    if ((flags & 0xFFC0u) != 0u) {
        wchar_t unknown[32] = {};
        swprintf_s(unknown, L"Unknown 0x%04X", static_cast<unsigned int>(flags & 0xFFC0u));
        append(unknown);
    }
    return result;
}

std::wstring CSgpioMonitorTab::AnsiToWide(const char* text) {
    if (text == nullptr) {
        return L"";
    }
    const int count = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (count <= 1) {
        return L"";
    }
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), count);
    result.resize(static_cast<size_t>(count - 1));
    return result;
}
