#include "main_frame.h"

#include <afxdlgs.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <sstream>

#include "build_info.generated.h"
#include "core/text_utils.h"
#include "resource.h"
#include "ui/layout_utils.h"

namespace {

constexpr int kUiLogEditLimitChars = 512 * 1024;
constexpr int kUiLogTrimThresholdChars = 160 * 1024;
constexpr int kUiLogTrimTargetChars = 120 * 1024;

std::wstring NowTimeText() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    wchar_t buf[32] = {};
    swprintf_s(buf, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

} // namespace

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_MESSAGE(WM_DPICHANGED, &CMainFrame::OnDpiChanged)
    ON_WM_GETMINMAXINFO()
    ON_BN_CLICKED(ID_TOP_REFRESH_BTN, &CMainFrame::OnBnClickedRefresh)
    ON_BN_CLICKED(ID_TOP_CONNECT_BTN, &CMainFrame::OnBnClickedConnect)
    ON_BN_CLICKED(ID_TOP_DISCONNECT_BTN, &CMainFrame::OnBnClickedDisconnect)
    ON_BN_CLICKED(ID_TOP_GET_INFO_BTN, &CMainFrame::OnBnClickedGetInfo)
    ON_BN_CLICKED(ID_TOP_PING_BTN, &CMainFrame::OnBnClickedPing)
    ON_BN_CLICKED(ID_TOP_RESET_MCU_BTN, &CMainFrame::OnBnClickedResetMcu)
    ON_BN_CLICKED(ID_TOP_RETURN_BOOTLOADER_BTN, &CMainFrame::OnBnClickedReturnBootloader)
    ON_BN_CLICKED(ID_TOP_SAVE_INI_BTN, &CMainFrame::OnBnClickedSaveIni)
    ON_BN_CLICKED(ID_TOP_LOAD_INI_BTN, &CMainFrame::OnBnClickedLoadIni)
    ON_BN_CLICKED(ID_TOP_RESET_INI_BTN, &CMainFrame::OnBnClickedResetIni)
    ON_BN_CLICKED(ID_LOG_SAVE_BTN, &CMainFrame::OnBnClickedSaveLog)
    ON_BN_CLICKED(ID_LOG_SAVE_CHECK, &CMainFrame::OnBnClickedSaveLogCheck)
    ON_BN_CLICKED(ID_LOG_CLEAR_BTN, &CMainFrame::OnBnClickedClearLog)
    ON_WM_CTLCOLOR()
    ON_WM_CLOSE()
END_MESSAGE_MAP()

CMainFrame::CMainFrame()
    : ini_(mfc_tool::config::IniManager::DefaultIniPath(L"sgpio_hid_tool.ini")),
      state_(mfc_tool::core::AppState::Default()) {
    expected_fw_version_ = M032_EXPECTED_FW_VERSION;
    state_.ui.expected_fw_version = expected_fw_version_;
    current_fw_version_ = state_.ui.last_seen_fw_version;
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs) {
    if (!CFrameWnd::PreCreateWindow(cs)) {
        return FALSE;
    }
    cs.style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    HICON app_icon = AfxGetApp()->LoadIconW(IDR_MAINFRAME);
    cs.lpszClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW),
                                       reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), app_icon);
    return TRUE;
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    current_dpi_ = mfc_tool::ui::GetDpiForWnd(*this);
    RecreateUiFont();
    status_chip_brush_.CreateSolidBrush(status_chip_color_);
    HICON app_icon = AfxGetApp()->LoadIconW(IDR_MAINFRAME);
    SetIcon(app_icon, TRUE);
    SetIcon(app_icon, FALSE);

    auto create_or_fail = [this](BOOL ok, const wchar_t* name) -> bool {
        if (!ok) {
            AppendLog(std::wstring(L"Create control failed: ") + name);
            return false;
        }
        return true;
    };

    if (!create_or_fail(vid_label_.Create(L"VID", WS_CHILD | WS_VISIBLE, CRect(), this, ID_TOP_VID_LABEL), L"VID label")) return -1;
    if (!create_or_fail(vid_edit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, CRect(), this, ID_TOP_VID_EDIT), L"VID edit")) return -1;
    if (!create_or_fail(pid_label_.Create(L"PID", WS_CHILD | WS_VISIBLE, CRect(), this, ID_TOP_PID_LABEL), L"PID label")) return -1;
    if (!create_or_fail(pid_edit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, CRect(), this, ID_TOP_PID_EDIT), L"PID edit")) return -1;
    if (!create_or_fail(timeout_label_.Create(L"Timeout(ms)", WS_CHILD | WS_VISIBLE, CRect(), this, ID_TOP_TIMEOUT_LABEL), L"Timeout label")) return -1;
    if (!create_or_fail(timeout_edit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, CRect(), this, ID_TOP_TIMEOUT_EDIT), L"Timeout edit")) return -1;
    if (!create_or_fail(refresh_btn_.Create(L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_REFRESH_BTN), L"Refresh")) return -1;
    if (!create_or_fail(connect_btn_.Create(L"Connect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_CONNECT_BTN), L"Connect")) return -1;
    if (!create_or_fail(disconnect_btn_.Create(L"Disconnect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_DISCONNECT_BTN), L"Disconnect")) return -1;
    if (!create_or_fail(status_title_.Create(L"Status:", WS_CHILD | WS_VISIBLE, CRect(), this, ID_TOP_STATUS_TITLE), L"Status title")) return -1;
    if (!create_or_fail(status_chip_.Create(L"Disconnected", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, CRect(), this, ID_TOP_STATUS_VALUE), L"Status chip")) return -1;
    if (!create_or_fail(save_ini_btn_.Create(L"Save INI", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_SAVE_INI_BTN), L"Save INI")) return -1;
    if (!create_or_fail(load_ini_btn_.Create(L"Load INI", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_LOAD_INI_BTN), L"Load INI")) return -1;
    if (!create_or_fail(reset_ini_btn_.Create(L"Reset INI", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_RESET_INI_BTN), L"Reset INI")) return -1;
    if (!create_or_fail(ini_path_title_.Create(L"INI:", WS_CHILD | WS_VISIBLE, CRect(), this, ID_TOP_INI_PATH_TITLE), L"INI path title")) return -1;
    if (!create_or_fail(ini_path_value_.Create(L"-", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, CRect(), this, ID_TOP_INI_PATH_VALUE), L"INI path value")) return -1;
    if (!create_or_fail(build_info_title_.Create(L"Build:", WS_CHILD | WS_VISIBLE, CRect(), this, ID_TOP_BUILD_INFO_TITLE), L"Build info title")) return -1;
    if (!create_or_fail(build_info_value_.Create(L"-", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, CRect(), this, ID_TOP_BUILD_INFO_VALUE), L"Build info value")) return -1;
    if (!create_or_fail(fw_info_title_.Create(L"FW:", WS_CHILD | WS_VISIBLE, CRect(), this, ID_TOP_FW_INFO_TITLE), L"FW info title")) return -1;
    if (!create_or_fail(fw_info_value_.Create(L"-", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, CRect(), this, ID_TOP_FW_INFO_VALUE), L"FW info value")) return -1;
    if (!create_or_fail(device_label_.Create(L"Device", WS_CHILD | WS_VISIBLE, CRect(), this, ID_TOP_DEVICE_LABEL), L"Device label")) return -1;
    if (!create_or_fail(device_combo_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, CRect(), this, ID_TOP_DEVICE_COMBO), L"Device combo")) return -1;
    if (!create_or_fail(get_info_btn_.Create(L"Get Info", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_GET_INFO_BTN), L"Get Info")) return -1;
    if (!create_or_fail(ping_btn_.Create(L"Ping", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_PING_BTN), L"Ping")) return -1;
    if (!create_or_fail(reset_mcu_btn_.Create(L"Reset MCU", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_RESET_MCU_BTN), L"Reset MCU")) return -1;
    if (!create_or_fail(return_bootloader_btn_.Create(L"Return Bootloader", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_TOP_RETURN_BOOTLOADER_BTN), L"Return Bootloader")) return -1;
    if (!create_or_fail(tab_ctrl_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_FIXEDWIDTH, CRect(), this, ID_TAB_CTRL), L"Tab ctrl")) return -1;
    if (!create_or_fail(save_log_check_.Create(L"Save Log", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(), this, ID_LOG_SAVE_CHECK), L"Save Log checkbox")) return -1;
    if (!create_or_fail(save_log_btn_.Create(L"Save Log", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_LOG_SAVE_BTN), L"Save Log button")) return -1;
    if (!create_or_fail(clear_log_btn_.Create(L"Clear Log", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, ID_LOG_CLEAR_BTN), L"Clear Log button")) return -1;
    if (!create_or_fail(log_edit_.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_BORDER,
                                         CRect(), this, ID_LOG_EDIT), L"Log edit")) return -1;
    log_edit_.LimitText(kUiLogEditLimitChars);

    CRect tab_dummy(0, 0, 100, 100);
    if (!sgpio_tab_.Create(&tab_ctrl_, tab_dummy, ID_TAB_SGPIO)) return -1;
    sgpio_tab_.Bind(&service_, [this](const std::wstring& msg) { AppendLog(msg); }, &pin_usage_);

    ApplyTopControlFont();

    tab_ctrl_.DeleteAllItems();
    tab_ctrl_.InsertItem(0, L"SGPIO");
    {
        const mfc_tool::ui::DpiScaler dpi(current_dpi_);
        tab_ctrl_.SetItemSize(dpi.ScaleSize(96, 24));
    }
    tab_ctrl_.SetCurSel(0);

    vid_edit_.SetWindowTextW(state_.ui.vid.c_str());
    pid_edit_.SetWindowTextW(state_.ui.pid.c_str());
    timeout_edit_.SetWindowTextW(state_.ui.timeout_ms.c_str());
    save_log_check_.SetCheck(state_.ui.save_log_checked ? BST_CHECKED : BST_UNCHECKED);

    LoadIni();
    RefreshDevices();
    SetConnectionUi(false);

    AppendLog(L"M032 SGPIO HID tool initialized.");
    AppendLog(L"M032 EVB SGPIO host pins: PA2=SCLK, PA0=SDOUT, PA3=SLOAD, PA1=SDIN optional.");
    UpdateIniPathUi();
    SetControlText(build_info_value_, std::wstring(L"v") + SGPIO_TOOL_BUILD_VERSION + L" | " + SGPIO_TOOL_BUILD_TIME);
    UpdateFirmwareInfoUi();

    CRect rc;
    GetClientRect(&rc);
    LayoutControls(rc.Width(), rc.Height());
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    return 0;
}

void CMainFrame::OnSize(UINT nType, int cx, int cy) {
    CFrameWnd::OnSize(nType, cx, cy);
    LayoutControls(cx, cy);
}

LRESULT CMainFrame::OnDpiChanged(WPARAM wParam, LPARAM lParam) {
    const UINT dpi_y = HIWORD(wParam);
    const UINT dpi_x = LOWORD(wParam);
    const RECT* suggested_rect = reinterpret_cast<const RECT*>(lParam);

    current_dpi_ = dpi_y != 0u ? dpi_y : (dpi_x != 0u ? dpi_x : mfc_tool::ui::GetDpiForWnd(*this));

    if (suggested_rect != nullptr) {
        SetWindowPos(nullptr,
                     suggested_rect->left,
                     suggested_rect->top,
                     suggested_rect->right - suggested_rect->left,
                     suggested_rect->bottom - suggested_rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RecreateUiFont();
    ApplyTopControlFont();
    {
        const mfc_tool::ui::DpiScaler dpi(current_dpi_);
        tab_ctrl_.SetItemSize(dpi.ScaleSize(96, 24));
    }
    sgpio_tab_.RefreshDpiLayout();

    CRect rc;
    GetClientRect(&rc);
    LayoutControls(rc.Width(), rc.Height());
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    return 0;
}

void CMainFrame::OnGetMinMaxInfo(MINMAXINFO* lpMMI) {
    CFrameWnd::OnGetMinMaxInfo(lpMMI);
    if (lpMMI == nullptr) {
        return;
    }
    const mfc_tool::ui::DpiScaler dpi(current_dpi_ == 0u ? 96u : current_dpi_);
    lpMMI->ptMinTrackSize.x = (std::max)(lpMMI->ptMinTrackSize.x, static_cast<LONG>(dpi.Scale(960)));
    lpMMI->ptMinTrackSize.y = (std::max)(lpMMI->ptMinTrackSize.y, static_cast<LONG>(dpi.Scale(620)));
}

void CMainFrame::LayoutControls(int cx, int cy) {
    if (cx <= 0 || cy <= 0) {
        return;
    }

    const mfc_tool::ui::DpiScaler dpi(current_dpi_ == 0u ? mfc_tool::ui::GetDpiForWnd(*this) : current_dpi_);
    const mfc_tool::ui::LayoutMetrics metrics = mfc_tool::ui::MetricsForWindow(*this);
    const int margin = metrics.margin8;
    const int gap = metrics.gap;
    const int row_h = metrics.row26;
    const int label_y_pad = dpi.Scale(4);
    const int label_pad = dpi.Scale(10);
    const int label_h = metrics.label18;
    const int log_h = (std::max)(dpi.Scale(120), cy / 5);
    int btn_w = dpi.Scale(86);
    int y = margin;
    int x = margin;

    btn_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(refresh_btn_, dpi.Scale(20)));
    btn_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(connect_btn_, dpi.Scale(20)));
    btn_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(save_ini_btn_, dpi.Scale(20)));

    x = mfc_tool::ui::PlaceLabelAndControl(vid_label_, vid_edit_, x, y + label_y_pad, y, dpi.Scale(92), row_h, gap, label_pad, label_h) + gap;
    x = mfc_tool::ui::PlaceLabelAndControl(pid_label_, pid_edit_, x, y + label_y_pad, y, dpi.Scale(92), row_h, gap, label_pad, label_h) + gap;
    x = mfc_tool::ui::PlaceLabelAndControl(timeout_label_, timeout_edit_, x, y + label_y_pad, y, dpi.Scale(80), row_h, gap, label_pad, label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(refresh_btn_, x, y, btn_w, row_h);
    x += btn_w + gap;
    mfc_tool::ui::SafeMoveWindow(connect_btn_, x, y, btn_w, row_h);
    x += btn_w + gap;
    {
        const int disconnect_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(disconnect_btn_, dpi.Scale(20)));
        mfc_tool::ui::SafeMoveWindow(disconnect_btn_, x, y, disconnect_w, row_h);
        x += disconnect_w + dpi.Scale(10);
    }
    x += mfc_tool::ui::PlaceLabel(status_title_, x, y + label_y_pad, label_pad, label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(status_chip_, x, y + dpi.Scale(1), dpi.Scale(112), row_h - dpi.Scale(2));
    x += dpi.Scale(122);
    mfc_tool::ui::SafeMoveWindow(save_ini_btn_, x, y, btn_w, row_h);
    x += btn_w + gap;
    mfc_tool::ui::SafeMoveWindow(load_ini_btn_, x, y, btn_w, row_h);
    x += btn_w + gap;
    mfc_tool::ui::SafeMoveWindow(reset_ini_btn_, x, y, btn_w, row_h);

    y += row_h + gap;
    x = margin;
    x += mfc_tool::ui::PlaceLabel(device_label_, x, y + label_y_pad, label_pad, label_h) + gap;
    {
        const int reset_w = (std::max)(btn_w + dpi.Scale(8), mfc_tool::ui::MeasureButtonMinWidth(reset_mcu_btn_, dpi.Scale(20)));
        const int return_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(return_bootloader_btn_, dpi.Scale(20)));
        const int action_w = btn_w * 2 + reset_w + return_w + gap * 3;
        const int combo_w = (std::max)(dpi.Scale(260), cx - x - action_w - margin - gap);
        mfc_tool::ui::SafeMoveWindow(device_combo_, x, y, combo_w, row_h + metrics.comboDrop);
        x += combo_w + gap;
    }
    mfc_tool::ui::SafeMoveWindow(get_info_btn_, x, y, btn_w, row_h);
    x += btn_w + gap;
    mfc_tool::ui::SafeMoveWindow(ping_btn_, x, y, btn_w, row_h);
    x += btn_w + gap;
    {
        const int reset_w = (std::max)(btn_w + dpi.Scale(8), mfc_tool::ui::MeasureButtonMinWidth(reset_mcu_btn_, dpi.Scale(20)));
        const int return_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(return_bootloader_btn_, dpi.Scale(20)));
        mfc_tool::ui::SafeMoveWindow(reset_mcu_btn_, x, y, reset_w, row_h);
        x += reset_w + gap;
        mfc_tool::ui::SafeMoveWindow(return_bootloader_btn_, x, y, return_w, row_h);
    }

    y += row_h + gap;
    x = margin;
    x += mfc_tool::ui::PlaceLabel(ini_path_title_, x, y + label_y_pad, label_pad, label_h) + gap;
    {
        const int build_block_w = dpi.Scale(360);
        mfc_tool::ui::SafeMoveWindow(ini_path_value_, x, y + label_y_pad, (std::max)(dpi.Scale(120), cx - x - build_block_w - margin), label_h);
        x = (std::max)(margin, cx - build_block_w);
    }
    x += mfc_tool::ui::PlaceLabel(build_info_title_, x, y + label_y_pad, label_pad, label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(build_info_value_, x, y + label_y_pad, cx - x - margin, label_h);

    y += row_h + gap;
    x = margin;
    x += mfc_tool::ui::PlaceLabel(fw_info_title_, x, y + label_y_pad, label_pad, label_h) + gap;
    mfc_tool::ui::SafeMoveWindow(fw_info_value_, x, y + label_y_pad, cx - x - margin, label_h);

    const int top_h = y + row_h + gap;
    const int log_y = cy - log_h - margin;
    const int log_btn_y = log_y - row_h - gap;
    mfc_tool::ui::SafeMoveWindow(save_log_check_,
                                 margin,
                                 log_btn_y + dpi.Scale(3),
                                 (std::max)(dpi.Scale(110), mfc_tool::ui::MeasureButtonMinWidth(save_log_check_, dpi.Scale(20))),
                                 metrics.row24);
    mfc_tool::ui::SafeMoveWindow(save_log_btn_, margin + dpi.Scale(116), log_btn_y, btn_w, row_h);
    mfc_tool::ui::SafeMoveWindow(clear_log_btn_, margin + dpi.Scale(116) + btn_w + gap, log_btn_y, btn_w, row_h);
    mfc_tool::ui::SafeMoveWindow(log_edit_, margin, log_y, cx - margin * 2, cy - log_y - margin);
    mfc_tool::ui::SafeMoveWindow(tab_ctrl_, margin, top_h, cx - margin * 2, (std::max)(dpi.Scale(120), log_btn_y - top_h - gap));

    CRect rc;
    tab_ctrl_.GetClientRect(&rc);
    tab_ctrl_.AdjustRect(FALSE, &rc);
    rc.DeflateRect(dpi.Scale(4), dpi.Scale(4), dpi.Scale(4), dpi.Scale(4));
    mfc_tool::ui::SafeMoveWindow(sgpio_tab_, rc);
    sgpio_tab_.ShowWindow(SW_SHOW);
}

void CMainFrame::RecreateUiFont() {
    mfc_tool::ui::CreatePointFontForWindow(ui_font_, *this, 90);
}

void CMainFrame::ApplyTopControlFont() {
    if (ui_font_.GetSafeHandle() == nullptr) {
        return;
    }

    const std::array<CWnd*, 31> controls = {
        &vid_label_, &vid_edit_, &pid_label_, &pid_edit_, &timeout_label_, &timeout_edit_,
        &refresh_btn_, &connect_btn_, &disconnect_btn_, &status_title_, &status_chip_, &save_ini_btn_,
        &load_ini_btn_, &reset_ini_btn_, &ini_path_title_, &ini_path_value_, &build_info_title_,
        &build_info_value_, &fw_info_title_, &fw_info_value_, &device_label_, &device_combo_,
        &get_info_btn_, &ping_btn_, &reset_mcu_btn_, &return_bootloader_btn_, &tab_ctrl_,
        &save_log_check_, &save_log_btn_, &clear_log_btn_, &log_edit_
    };

    for (CWnd* w : controls) {
        if (w != nullptr && ::IsWindow(w->GetSafeHwnd())) {
            w->SetFont(&ui_font_, FALSE);
        }
    }
}

void CMainFrame::AppendLog(const std::wstring& text) {
    const std::wstring line = L"[" + NowTimeText() + L"] " + text;
    logger_.AddLine(line);

    if (!::IsWindow(log_edit_.GetSafeHwnd())) {
        return;
    }
    int len = log_edit_.GetWindowTextLengthW();
    log_edit_.SetSel(len, len);
    log_edit_.ReplaceSel((line + L"\r\n").c_str());
    TrimVisibleLogIfNeeded();
}

void CMainFrame::TrimVisibleLogIfNeeded() {
    const int len = log_edit_.GetWindowTextLengthW();
    if (len <= kUiLogTrimThresholdChars) {
        return;
    }

    CString visible_log;
    log_edit_.GetWindowTextW(visible_log);
    std::wstring text = visible_log.GetString();
    int trim_chars = len - kUiLogTrimTargetChars;
    if (trim_chars <= 0) {
        return;
    }
    std::wstring::size_type trim_end = text.find(L'\n', static_cast<std::wstring::size_type>(trim_chars));
    if (trim_end == std::wstring::npos) {
        trim_end = static_cast<std::wstring::size_type>(trim_chars);
    } else {
        ++trim_end;
    }
    if (trim_end == 0 || trim_end >= text.size()) {
        return;
    }

    log_edit_.SetRedraw(FALSE);
    log_edit_.SetSel(0, static_cast<int>(trim_end));
    log_edit_.ReplaceSel(L"");
    log_edit_.SetSel(log_edit_.GetWindowTextLengthW(), log_edit_.GetWindowTextLengthW());
    log_edit_.SetRedraw(TRUE);
    log_edit_.Invalidate(FALSE);
}

void CMainFrame::RefreshDevices() {
    try {
        int vid = mfc_tool::core::ParseInt(GetControlText(vid_edit_));
        int pid = mfc_tool::core::ParseInt(GetControlText(pid_edit_));
        scanned_devices_ = service_.ScanDevices(static_cast<std::uint16_t>(vid), static_cast<std::uint16_t>(pid));
        std::sort(scanned_devices_.begin(), scanned_devices_.end(), [](const auto& a, const auto& b) {
            return DeviceLabel(a, 0) < DeviceLabel(b, 0);
        });

        device_combo_.ResetContent();
        device_combo_.AddString(L"Auto Select");
        for (size_t i = 0; i < scanned_devices_.size(); ++i) {
            device_combo_.AddString(DeviceLabel(scanned_devices_[i], static_cast<int>(i)).c_str());
        }
        device_combo_.SetCurSel(scanned_devices_.empty() ? 0 : 1);
        AppendLog(L"Device scan complete: " + std::to_wstring(scanned_devices_.size()) + L" found");
    } catch (const std::exception& e) {
        AppendLog(L"Refresh failed: " + AnsiToWide(e.what()));
        ShowErrorBox(L"Refresh Error", AnsiToWide(e.what()));
    }
}

void CMainFrame::SetConnectionUi(bool connected) {
    if (!::IsWindow(connect_btn_.GetSafeHwnd())) {
        return;
    }
    mfc_tool::ui::SafeEnableWindow(connect_btn_, !connected);
    mfc_tool::ui::SafeEnableWindow(disconnect_btn_, connected);
    mfc_tool::ui::SafeEnableWindow(reset_mcu_btn_, connected);
    mfc_tool::ui::SafeEnableWindow(return_bootloader_btn_, connected);
    mfc_tool::ui::SafeEnableWindow(device_combo_, !connected);
    mfc_tool::ui::SafeEnableWindow(refresh_btn_, !connected);
    mfc_tool::ui::SafeEnableWindow(save_log_btn_, save_log_check_.GetCheck() == BST_CHECKED);
    SetControlText(status_chip_, connected ? L"Connected" : L"Disconnected");
    status_chip_color_ = connected ? RGB(46, 139, 87) : RGB(95, 104, 117);
    UpdateStatusChipColor();
    sgpio_tab_.SetConnected(connected);
}

void CMainFrame::UpdateStatusChipColor() {
    if (status_chip_brush_.GetSafeHandle()) {
        status_chip_brush_.DeleteObject();
    }
    status_chip_brush_.CreateSolidBrush(status_chip_color_);
    status_chip_.Invalidate();
}

void CMainFrame::UpdateIniPathUi() {
    SetControlText(ini_path_value_, ini_.Path());
}

std::wstring CMainFrame::SelectedDevicePath() const {
    int sel = device_combo_.GetCurSel();
    if (sel == CB_ERR || sel <= 0) {
        if (!scanned_devices_.empty()) {
            return scanned_devices_.front().path;
        }
        return L"";
    }
    const size_t idx = static_cast<size_t>(sel - 1);
    return idx < scanned_devices_.size() ? scanned_devices_[idx].path : L"";
}

std::wstring CMainFrame::DeviceLabel(const mfc_tool::hid::DeviceInfo& d, int index) {
    std::wstringstream ss;
    ss << L"#" << index + 1 << L"  VID:PID ";
    ss << std::uppercase << std::hex;
    ss.width(4);
    ss.fill(L'0');
    ss << d.vendor_id << L":";
    ss.width(4);
    ss << d.product_id;
    ss << std::dec << L"  IN:" << d.input_report_len << L" OUT:" << d.output_report_len;
    if (!d.manufacturer.empty() || !d.product.empty()) {
        ss << L"  [" << d.manufacturer;
        if (!d.manufacturer.empty() && !d.product.empty()) {
            ss << L" - ";
        }
        ss << d.product << L"]";
    }
    return ss.str();
}

void CMainFrame::LogBridgeInfoPayload(const std::vector<std::uint8_t>& rx, bool from_connect) {
    std::wstring bridge_name;
    std::uint8_t reset_reason = 0;

    if (!rx.empty()) {
        bridge_name.assign(rx.begin(), rx.end());
        const auto end_pos = std::find(bridge_name.begin(), bridge_name.end(), L'\0');
        bridge_name.erase(end_pos, bridge_name.end());
        if (rx.size() >= 2u) {
            reset_reason = rx.back();
        }
    }

    current_fw_version_ = ExtractFirmwareVersion(bridge_name);
    if (current_fw_version_.empty()) {
        current_fw_version_ = L"-";
    }
    fw_version_match_ = FirmwareVersionMatch(expected_fw_version_, current_fw_version_) || current_fw_version_ == L"-";
    UpdateFirmwareInfoUi();
    AppendLog(std::wstring(from_connect ? L"Connected bridge: " : L"Bridge info: ") + bridge_name);
    AppendLog(L"Reset reason: " + ResetReasonText(reset_reason));
}

std::wstring CMainFrame::ResetReasonText(std::uint8_t reason) {
    switch (reason) {
    case 0: return L"NONE";
    case 1: return L"WATCHDOG";
    case 2: return L"CMD_RESET";
    default: return L"UNKNOWN";
    }
}

void CMainFrame::UpdateFirmwareInfoUi() {
    std::wstring text = L"expected=" + expected_fw_version_ + L" actual=" + current_fw_version_;
    if (!fw_version_match_) {
        text += L"  MISMATCH";
    }
    SetControlText(fw_info_value_, text);
}

std::wstring CMainFrame::ExtractFirmwareVersion(const std::wstring& bridge_name) {
    const auto pos = bridge_name.find(L'/');
    if (pos == std::wstring::npos || pos + 1 >= bridge_name.size()) {
        return L"";
    }
    return bridge_name.substr(pos + 1);
}

bool CMainFrame::FirmwareVersionMatch(const std::wstring& expected, const std::wstring& actual) {
    return !expected.empty() && expected == actual;
}

void CMainFrame::LoadIni() {
    UpdateIniPathUi();
    mfc_tool::config::IniData data;
    std::wstring error;
    if (!ini_.Exists()) {
        SaveIni();
        AppendLog(L"INI created with defaults.");
        return;
    }
    if (!ini_.Load(&data, &error)) {
        AppendLog(L"INI load failed: " + error);
        return;
    }

    state_.ApplyIniData(data);
    vid_edit_.SetWindowTextW(state_.ui.vid.c_str());
    pid_edit_.SetWindowTextW(state_.ui.pid.c_str());
    timeout_edit_.SetWindowTextW(state_.ui.timeout_ms.c_str());
    save_log_check_.SetCheck(state_.ui.save_log_checked ? BST_CHECKED : BST_UNCHECKED);
    sgpio_tab_.LoadState(state_);
    current_fw_version_ = state_.ui.last_seen_fw_version.empty() ? L"-" : state_.ui.last_seen_fw_version;
    fw_version_match_ = FirmwareVersionMatch(expected_fw_version_, current_fw_version_) || current_fw_version_ == L"-";
    UpdateFirmwareInfoUi();
    AppendLog(L"INI loaded.");
}

void CMainFrame::SaveIni() {
    sgpio_tab_.SaveState(&state_);
    state_.ui.vid = GetControlText(vid_edit_);
    state_.ui.pid = GetControlText(pid_edit_);
    state_.ui.timeout_ms = GetControlText(timeout_edit_);
    state_.ui.save_log_checked = (save_log_check_.GetCheck() == BST_CHECKED);
    state_.ui.expected_fw_version = expected_fw_version_;
    state_.ui.last_seen_fw_version = current_fw_version_;

    CString combo_text;
    device_combo_.GetWindowTextW(combo_text);
    state_.ui.device_label = combo_text.GetString();

    std::wstring error;
    auto data = state_.ToIniData(ini_.Path());
    if (!ini_.Save(data, &error)) {
        AppendLog(L"INI save failed: " + error);
        return;
    }
    AppendLog(L"INI saved.");
}

void CMainFrame::SaveIniTo(const std::wstring& path) {
    ini_.SetPath(path);
    UpdateIniPathUi();
    SaveIni();
}

bool CMainFrame::TryLoadIniFrom(const std::wstring& path) {
    ini_.SetPath(path);
    UpdateIniPathUi();
    LoadIni();
    return true;
}

void CMainFrame::ResetIniToDefault() {
    state_ = mfc_tool::core::AppState::Default();
    vid_edit_.SetWindowTextW(state_.ui.vid.c_str());
    pid_edit_.SetWindowTextW(state_.ui.pid.c_str());
    timeout_edit_.SetWindowTextW(state_.ui.timeout_ms.c_str());
    save_log_check_.SetCheck(BST_UNCHECKED);
    sgpio_tab_.LoadState(state_);
    current_fw_version_ = L"-";
    fw_version_match_ = true;
    UpdateFirmwareInfoUi();
    SaveIni();
}

void CMainFrame::SaveLogToFile(const std::wstring& path) {
    std::wstring error;
    if (!logger_.SaveToFile(path, &error)) {
        ShowErrorBox(L"Save Log Error", error);
        return;
    }
    AppendLog(L"Log saved: " + path);
}

std::wstring CMainFrame::GetControlText(const CWnd& w) const {
    CString s;
    const_cast<CWnd&>(w).GetWindowTextW(s);
    return s.GetString();
}

void CMainFrame::SetControlText(CWnd& w, const std::wstring& text) {
    if (::IsWindow(w.GetSafeHwnd())) {
        w.SetWindowTextW(text.c_str());
    }
}

void CMainFrame::ShowErrorBox(const std::wstring& title, const std::wstring& message) {
    ::MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
}

std::wstring CMainFrame::AnsiToWide(const char* text) {
    if (text == nullptr) {
        return L"";
    }
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (n <= 0) {
        return L"";
    }
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, out.data(), n);
    return out;
}

void CMainFrame::OnBnClickedRefresh() {
    RefreshDevices();
}

void CMainFrame::OnBnClickedConnect() {
    try {
        int vid = mfc_tool::core::ParseInt(GetControlText(vid_edit_));
        int pid = mfc_tool::core::ParseInt(GetControlText(pid_edit_));
        int timeout = mfc_tool::core::ParseInt(GetControlText(timeout_edit_));
        std::wstring path = SelectedDevicePath();

        service_.Disconnect();
        service_.Connect(static_cast<std::uint16_t>(vid), static_cast<std::uint16_t>(pid), path, timeout);
        SetConnectionUi(true);
        AppendLog(L"Connected VID:PID");
        try {
            auto rx = service_.GetInfo();
            LogBridgeInfoPayload(rx, true);
        } catch (const std::exception& info_e) {
            AppendLog(L"Auto Get Info failed: " + AnsiToWide(info_e.what()));
        }
    } catch (const std::exception& e) {
        ShowErrorBox(L"Connect Error", AnsiToWide(e.what()));
        AppendLog(L"Connect failed");
        SetConnectionUi(false);
    }
}

void CMainFrame::OnBnClickedDisconnect() {
    sgpio_tab_.OnDisconnected();
    service_.Disconnect();
    SetConnectionUi(false);
    AppendLog(L"Disconnected");
}

void CMainFrame::OnBnClickedGetInfo() {
    try {
        auto rx = service_.GetInfo();
        LogBridgeInfoPayload(rx, false);
    } catch (const std::exception& e) {
        ShowErrorBox(L"Get Info Error", AnsiToWide(e.what()));
    }
}

void CMainFrame::OnBnClickedPing() {
    try {
        std::vector<std::uint8_t> tx = {0xA1, 0xB2, 0xC3, 0xD4};
        auto rx = service_.Ping(tx);
        AppendLog(L"PING RX: " + mfc_tool::core::HexDump(rx));
    } catch (const std::exception& e) {
        ShowErrorBox(L"Ping Error", AnsiToWide(e.what()));
    }
}

void CMainFrame::OnBnClickedResetMcu() {
    try {
        service_.ResetMcu();
        AppendLog(L"MCU reset command sent.");
        sgpio_tab_.OnDisconnected();
        service_.Disconnect();
        SetConnectionUi(false);
        RefreshDevices();
    } catch (const std::exception& e) {
        ShowErrorBox(L"Reset MCU Error", AnsiToWide(e.what()));
    }
}

void CMainFrame::OnBnClickedReturnBootloader() {
    const int answer = MessageBoxW(
        L"Return the bridge board to USB HID bootloader mode?\r\n\r\n"
        L"This erases the application CRC word. If programming is canceled, the board will stay in bootloader mode until a valid application image is programmed.",
        L"Return Bootloader",
        MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    if (answer != IDYES) {
        return;
    }

    try {
        service_.EnterIap();
        AppendLog(L"Return Bootloader command sent; application CRC invalidated.");
        AppendLog(L"Use simple_programming_tool after bootloader VID:PID 0416:3F00 enumerates.");
        sgpio_tab_.OnDisconnected();
        service_.Disconnect();
        SetConnectionUi(false);
    } catch (const std::exception& e) {
        ShowErrorBox(L"Return Bootloader Error", AnsiToWide(e.what()));
    }
}

void CMainFrame::OnBnClickedSaveIni() {
    CFileDialog dlg(FALSE, L"ini", L"sgpio_hid_tool.ini",
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"INI Files (*.ini)|*.ini|All Files (*.*)|*.*||", this);
    if (dlg.DoModal() == IDOK) {
        SaveIniTo(dlg.GetPathName().GetString());
    }
}

void CMainFrame::OnBnClickedLoadIni() {
    if (service_.IsConnected()) {
        ShowErrorBox(L"Load INI", L"Disconnect first before loading INI settings.");
        AppendLog(L"Load INI blocked while connected.");
        return;
    }

    CFileDialog dlg(TRUE, L"ini", nullptr,
                    OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"INI Files (*.ini)|*.ini|All Files (*.*)|*.*||", this);
    if (dlg.DoModal() == IDOK) {
        TryLoadIniFrom(dlg.GetPathName().GetString());
        RefreshDevices();
    }
}

void CMainFrame::OnBnClickedResetIni() {
    if (service_.IsConnected()) {
        ShowErrorBox(L"Reset INI", L"Disconnect first before resetting INI settings.");
        AppendLog(L"Reset INI blocked while connected.");
        return;
    }

    if (MessageBoxW(L"Reset INI settings to defaults?", L"Reset INI", MB_ICONQUESTION | MB_YESNO) != IDYES) {
        return;
    }
    ResetIniToDefault();
    AppendLog(L"INI reset to defaults.");
}

void CMainFrame::OnBnClickedSaveLog() {
    if (save_log_check_.GetCheck() != BST_CHECKED) {
        return;
    }

    CFileDialog dlg(FALSE, L"log", L"sgpio_hid_tool.log",
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"Log Files (*.log)|*.log|Text Files (*.txt)|*.txt|All Files (*.*)|*.*||", this);
    if (dlg.DoModal() == IDOK) {
        SaveLogToFile(dlg.GetPathName().GetString());
    }
}

void CMainFrame::OnBnClickedSaveLogCheck() {
    const bool checked = (save_log_check_.GetCheck() == BST_CHECKED);
    mfc_tool::ui::SafeEnableWindow(save_log_btn_, checked);
}

void CMainFrame::OnBnClickedClearLog() {
    logger_.Clear();
    log_edit_.SetWindowTextW(L"");
}

HBRUSH CMainFrame::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
    HBRUSH hbr = CFrameWnd::OnCtlColor(pDC, pWnd, nCtlColor);
    if (pWnd != nullptr && pWnd->GetSafeHwnd() == status_chip_.GetSafeHwnd()) {
        pDC->SetBkColor(status_chip_color_);
        pDC->SetTextColor(status_chip_text_color_);
        return status_chip_brush_;
    }
    return hbr;
}

void CMainFrame::OnClose() {
    SaveIni();
    sgpio_tab_.OnDisconnected();
    service_.Disconnect();
    CFrameWnd::OnClose();
}
