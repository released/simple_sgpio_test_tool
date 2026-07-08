#include "sgpio_tab.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "../core/fixed_topology.h"
#include "../core/text_utils.h"
#include "layout_utils.h"

namespace fixed_topology = mfc_tool::core::fixed_topology;

namespace {

enum : UINT {
    IDC_SGPIO_GROUP = 12200,
    IDC_SGPIO_FIXED_PIN_TITLE,
    IDC_SGPIO_FIXED_PIN_VALUE,
    IDC_SGPIO_SLOT_COUNT_TITLE,
    IDC_SGPIO_SLOT_COUNT_EDIT,
    IDC_SGPIO_CLOCK_TITLE,
    IDC_SGPIO_CLOCK_EDIT,
    IDC_SGPIO_PERIODIC_CHECK,
    IDC_SGPIO_INTERVAL_TITLE,
    IDC_SGPIO_INTERVAL_EDIT,
    IDC_SGPIO_VENDOR_TITLE,
    IDC_SGPIO_VENDOR_EDIT,
    IDC_SGPIO_ENABLE,
    IDC_SGPIO_DISABLE,
    IDC_SGPIO_APPLY,
    IDC_SGPIO_STATUS,
    IDC_SGPIO_STATUS_TITLE,
    IDC_SGPIO_STATUS_VALUE,
    IDC_SGPIO_SDATAIN_TITLE,
    IDC_SGPIO_SDATAIN_VALUE,
    IDC_SGPIO_SLOT_PRESET_TITLE,
    IDC_SGPIO_SLOT_INDEX_COMBO,
    IDC_SGPIO_IBPI_PRESET_TITLE,
    IDC_SGPIO_IBPI_PRESET_COMBO,
    IDC_SGPIO_PRESET_APPLY,
    IDC_SGPIO_CLEAR_ALL,
    IDC_SGPIO_SLOT_HDR,
    IDC_SGPIO_ACT_HDR,
    IDC_SGPIO_LOCATE_HDR,
    IDC_SGPIO_FAIL_HDR,
    IDC_SGPIO_SLOT_LABEL_BASE,
    IDC_SGPIO_ACT_CHECK_BASE = IDC_SGPIO_SLOT_LABEL_BASE + 32,
    IDC_SGPIO_LOCATE_CHECK_BASE = IDC_SGPIO_ACT_CHECK_BASE + 32,
    IDC_SGPIO_FAIL_CHECK_BASE = IDC_SGPIO_LOCATE_CHECK_BASE + 32,
};

constexpr int kSgpioSdataInPin = 1;
constexpr int kSgpioLoadPin = 3;
constexpr int kSgpioDataPin = 0;
constexpr int kSgpioClockPin = 2;
constexpr int kSgpioSlotMax = 16;
constexpr size_t kSgpioStatusBaseSize = 21u;
constexpr size_t kSgpioStatusCaptureHeaderSize = 24u;

std::uint32_t ReadLe32(const std::vector<std::uint8_t>& data, size_t offset) {
    if (offset + 3u >= data.size()) {
        return 0u;
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3u]) << 24u);
}

std::wstring FormatSdinCapture(const std::vector<std::uint8_t>& data) {
    std::wstringstream ss;
    size_t words_available = 0u;
    size_t i = 0u;
    int word_count = 0;
    int bit_count = 0;

    if (data.size() < kSgpioStatusCaptureHeaderSize || data[21] == 0u) {
        return L"-";
    }

    word_count = static_cast<int>(data[22]);
    bit_count = static_cast<int>(data[23]);
    words_available = (data.size() - kSgpioStatusCaptureHeaderSize) / 4u;
    if (word_count < 0) {
        word_count = 0;
    }
    if (static_cast<size_t>(word_count) > words_available) {
        word_count = static_cast<int>(words_available);
    }
    if (word_count > 4) {
        word_count = 4;
    }

    ss << bit_count << L"b:";
    ss << std::uppercase << std::hex << std::setfill(L'0');
    for (i = 0u; i < static_cast<size_t>(word_count); ++i) {
        if (i > 0u) {
            ss << L",";
        }
        ss << std::setw(4) << (ReadLe32(data, kSgpioStatusCaptureHeaderSize + i * 4u) & 0xFFFFu);
    }
    return ss.str();
}

} // namespace

BEGIN_MESSAGE_MAP(CSgpioTab, CWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_BN_CLICKED(IDC_SGPIO_ENABLE, &CSgpioTab::OnBtnEnable)
    ON_BN_CLICKED(IDC_SGPIO_DISABLE, &CSgpioTab::OnBtnDisable)
    ON_BN_CLICKED(IDC_SGPIO_APPLY, &CSgpioTab::OnBtnApplyNow)
    ON_BN_CLICKED(IDC_SGPIO_STATUS, &CSgpioTab::OnBtnStatus)
    ON_BN_CLICKED(IDC_SGPIO_PRESET_APPLY, &CSgpioTab::OnBtnPresetApply)
    ON_BN_CLICKED(IDC_SGPIO_CLEAR_ALL, &CSgpioTab::OnBtnClearAll)
    ON_EN_CHANGE(IDC_SGPIO_SLOT_COUNT_EDIT, &CSgpioTab::OnSlotCountChanged)
END_MESSAGE_MAP()

BOOL CSgpioTab::Create(CWnd* parent, const RECT& rect, UINT id) {
    CString cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW),
                                      reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), nullptr);
    return CWnd::CreateEx(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, rect, parent, id);
}

void CSgpioTab::Bind(mfc_tool::core::BridgeService* service,
                     std::function<void(const std::wstring&)> logger,
                     mfc_tool::core::PinUsageRegistry* pin_usage) {
    service_ = service;
    log_ = std::move(logger);
    pin_usage_ = pin_usage;
    if (pin_usage_ != nullptr) {
        pin_usage_->SetLabel(fixed_topology::kSgpio, L"SGPIO");
        pin_usage_->SetClaim(fixed_topology::kSgpio, fixed_topology::SgpioPins());
        pin_usage_->SetActive(fixed_topology::kSgpio, false);
    }
}

int CSgpioTab::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CWnd::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    mfc_tool::ui::CreatePointFontForWindow(ui_font_, *this, 85);
    mfc_tool::ui::CreatePointFontForWindow(slot_font_, *this, 75);
    auto mk_static = [this](CStatic& s, const wchar_t* text, UINT id) {
        s.Create(text, WS_CHILD | WS_VISIBLE, CRect(0, 0, 0, 0), this, id);
        s.SetFont(&ui_font_);
    };
    auto mk_edit = [this](CEdit& e, const wchar_t* text, UINT id) {
        e.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, CRect(0, 0, 0, 0), this, id);
        e.SetFont(&ui_font_);
        e.SetWindowTextW(text);
    };
    auto mk_btn = [this](CButton& b, const wchar_t* text, UINT id, DWORD style = BS_PUSHBUTTON) {
        b.Create(text, WS_CHILD | WS_VISIBLE | style, CRect(0, 0, 0, 0), this, id);
        b.SetFont(&ui_font_);
    };

    mk_btn(group_, L"SGPIO (SFF-8485 / IBPI, M032 GPIO host)", IDC_SGPIO_GROUP, BS_GROUPBOX);
    mk_static(fixed_pin_title_, L"Fixed Pins", IDC_SGPIO_FIXED_PIN_TITLE);
    mk_static(fixed_pin_value_, L"PA2=SCLK  PA0=SDOUT  PA3=SLOAD  PA1=SDIN(optional)", IDC_SGPIO_FIXED_PIN_VALUE);
    fixed_pin_value_.ModifyStyle(SS_TYPEMASK, SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS);
    mk_static(slot_count_title_, L"Slot Count", IDC_SGPIO_SLOT_COUNT_TITLE);
    mk_edit(slot_count_edit_, L"8", IDC_SGPIO_SLOT_COUNT_EDIT);
    mk_static(clock_title_, L"Clock(Hz)", IDC_SGPIO_CLOCK_TITLE);
    mk_edit(clock_edit_, L"100000", IDC_SGPIO_CLOCK_EDIT);
    mk_btn(periodic_check_, L"Periodic Send", IDC_SGPIO_PERIODIC_CHECK, BS_AUTOCHECKBOX);
    periodic_check_.SetCheck(BST_CHECKED);
    mk_static(interval_title_, L"Interval(ms)", IDC_SGPIO_INTERVAL_TITLE);
    mk_edit(interval_edit_, L"100", IDC_SGPIO_INTERVAL_EDIT);
    mk_static(vendor_title_, L"SLOAD L0..L3 Raw", IDC_SGPIO_VENDOR_TITLE);
    mk_edit(vendor_edit_, L"0x0", IDC_SGPIO_VENDOR_EDIT);
    mk_btn(enable_btn_, L"Enable", IDC_SGPIO_ENABLE);
    mk_btn(disable_btn_, L"Disable", IDC_SGPIO_DISABLE);
    mk_btn(apply_btn_, L"Apply Now", IDC_SGPIO_APPLY);
    mk_btn(status_btn_, L"Get Status", IDC_SGPIO_STATUS);
    mk_static(status_title_, L"Status", IDC_SGPIO_STATUS_TITLE);
    mk_static(status_value_, L"Idle", IDC_SGPIO_STATUS_VALUE);
    mk_static(sdata_in_title_, L"SDataIn", IDC_SGPIO_SDATAIN_TITLE);
    mk_static(sdata_in_value_, L"-", IDC_SGPIO_SDATAIN_VALUE);

    mk_static(slot_preset_title_, L"Slot", IDC_SGPIO_SLOT_PRESET_TITLE);
    slot_index_combo_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, CRect(0, 0, 0, 0), this, IDC_SGPIO_SLOT_INDEX_COMBO);
    slot_index_combo_.SetFont(&ui_font_);
    mk_static(ibpi_preset_title_, L"IBPI Preset", IDC_SGPIO_IBPI_PRESET_TITLE);
    ibpi_preset_combo_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, CRect(0, 0, 0, 0), this, IDC_SGPIO_IBPI_PRESET_COMBO);
    ibpi_preset_combo_.SetFont(&ui_font_);
    mk_btn(preset_apply_btn_, L"Apply Preset", IDC_SGPIO_PRESET_APPLY);
    mk_btn(clear_all_btn_, L"Clear All", IDC_SGPIO_CLEAR_ALL);

    mk_static(slot_hdr_, L"Slot", IDC_SGPIO_SLOT_HDR);
    mk_static(act_hdr_, L"ACT", IDC_SGPIO_ACT_HDR);
    mk_static(locate_hdr_, L"LOCATE", IDC_SGPIO_LOCATE_HDR);
    mk_static(fail_hdr_, L"FAIL", IDC_SGPIO_FAIL_HDR);
    act_hdr_.ModifyStyle(0, SS_CENTER);
    locate_hdr_.ModifyStyle(0, SS_CENTER);
    fail_hdr_.ModifyStyle(0, SS_CENTER);
    slot_hdr_.SetFont(&slot_font_);
    act_hdr_.SetFont(&slot_font_);
    locate_hdr_.SetFont(&slot_font_);
    fail_hdr_.SetFont(&slot_font_);

    for (int i = 0; i < kSgpioSlotMax; ++i) {
        wchar_t slot_text[8] = {};
        swprintf_s(slot_text, L"%d", i);
        slot_labels_[i].Create(slot_text, WS_CHILD | WS_VISIBLE, CRect(0, 0, 0, 0), this, IDC_SGPIO_SLOT_LABEL_BASE + i);
        slot_labels_[i].SetFont(&slot_font_);
        act_checks_[i].Create(L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(0, 0, 0, 0), this, IDC_SGPIO_ACT_CHECK_BASE + i);
        locate_checks_[i].Create(L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(0, 0, 0, 0), this, IDC_SGPIO_LOCATE_CHECK_BASE + i);
        fail_checks_[i].Create(L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(0, 0, 0, 0), this, IDC_SGPIO_FAIL_CHECK_BASE + i);
        act_checks_[i].SetFont(&ui_font_);
        locate_checks_[i].SetFont(&ui_font_);
        fail_checks_[i].SetFont(&ui_font_);
    }

    PopulateSlotCombos();
    ibpi_preset_combo_.AddString(L"Off");
    ibpi_preset_combo_.AddString(L"Activity");
    ibpi_preset_combo_.AddString(L"Locate");
    ibpi_preset_combo_.AddString(L"Fail");
    ibpi_preset_combo_.AddString(L"ACT+LOC");
    ibpi_preset_combo_.AddString(L"ACT+FAIL");
    ibpi_preset_combo_.AddString(L"LOC+FAIL");
    ibpi_preset_combo_.AddString(L"ALL");
    ibpi_preset_combo_.SetCurSel(0);

    ApplyMasksToChecks(0x0001, 0x0000, 0x0000);
    UpdateEnableState();
    return 0;
}

void CSgpioTab::OnSize(UINT nType, int cx, int cy) {
    CWnd::OnSize(nType, cx, cy);
    LayoutScrolledContent();
}

void CSgpioTab::RefreshDpiLayout() {
    if (!::IsWindow(GetSafeHwnd())) {
        return;
    }

    mfc_tool::ui::CreatePointFontForWindow(ui_font_, *this, 85);
    mfc_tool::ui::CreatePointFontForWindow(slot_font_, *this, 75);
    mfc_tool::ui::ApplyFontToChildWindows(*this, ui_font_, FALSE);

    if (::IsWindow(slot_hdr_.GetSafeHwnd())) {
        slot_hdr_.SetFont(&slot_font_, FALSE);
        act_hdr_.SetFont(&slot_font_, FALSE);
        locate_hdr_.SetFont(&slot_font_, FALSE);
        fail_hdr_.SetFont(&slot_font_, FALSE);
    }
    for (int i = 0; i < kSgpioSlotMax; ++i) {
        if (::IsWindow(slot_labels_[i].GetSafeHwnd())) {
            slot_labels_[i].SetFont(&slot_font_, FALSE);
        }
    }

    LayoutScrolledContent();
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CSgpioTab::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
    if (pScrollBar != nullptr) {
        CWnd::OnVScroll(nSBCode, nPos, pScrollBar);
        return;
    }

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(SB_VERT, &si);

    int next_offset = scroll_offset_;
    const mfc_tool::ui::LayoutMetrics metrics = mfc_tool::ui::MetricsForWindow(*this);
    switch (nSBCode) {
    case SB_LINEUP:
        next_offset -= metrics.row24;
        break;
    case SB_LINEDOWN:
        next_offset += metrics.row24;
        break;
    case SB_PAGEUP:
        next_offset -= static_cast<int>(si.nPage);
        break;
    case SB_PAGEDOWN:
        next_offset += static_cast<int>(si.nPage);
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        next_offset = static_cast<int>(nPos);
        break;
    case SB_TOP:
        next_offset = 0;
        break;
    case SB_BOTTOM:
        next_offset = si.nMax;
        break;
    default:
        break;
    }
    ScrollToOffset(next_offset);
}

BOOL CSgpioTab::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
    (void)nFlags;
    (void)pt;
    const mfc_tool::ui::LayoutMetrics metrics = mfc_tool::ui::MetricsForWindow(*this);
    const int wheel_delta = zDelta < 0 ? -static_cast<int>(zDelta) : static_cast<int>(zDelta);
    const int lines = (std::max)(1, wheel_delta / WHEEL_DELTA);
    const int delta = metrics.row24 * 3 * lines;
    ScrollToOffset(scroll_offset_ + (zDelta > 0 ? -delta : delta));
    return TRUE;
}

int CSgpioTab::CalculateVirtualContentHeight(const CRect& client) const {
    const mfc_tool::ui::DpiScaler dpi = mfc_tool::ui::DpiScaler::FromWindow(*this);
    const mfc_tool::ui::LayoutMetrics metrics = mfc_tool::ui::MetricsForWindow(*this);
    const int rows_per_col = kSgpioSlotMax / 2;
    const int grid_rows = dpi.Scale(16) + dpi.Scale(3) + dpi.Scale(22) * rows_per_col;
    const int control_rows = metrics.margin8 + dpi.Scale(28) + metrics.row26 * 3 + metrics.gap * 4 + dpi.Scale(4);
    const int desired_height = control_rows + grid_rows + metrics.margin8 + dpi.Scale(18);
    return (std::max)(client.Height(), desired_height);
}

void CSgpioTab::UpdateVerticalScroll(const CRect& client) {
    virtual_content_height_ = CalculateVirtualContentHeight(client);
    const int max_offset = (std::max)(0, virtual_content_height_ - client.Height());
    if (scroll_offset_ > max_offset) {
        scroll_offset_ = max_offset;
    }
    if (scroll_offset_ < 0) {
        scroll_offset_ = 0;
    }

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (std::max)(0, virtual_content_height_ - 1);
    si.nPage = static_cast<UINT>((std::max)(1, client.Height()));
    si.nPos = scroll_offset_;
    SetScrollInfo(SB_VERT, &si, TRUE);
    ShowScrollBar(SB_VERT, max_offset > 0 ? TRUE : FALSE);
}

void CSgpioTab::ScrollToOffset(int next_offset) {
    CRect client;
    GetClientRect(&client);
    const int max_offset = (std::max)(0, virtual_content_height_ - client.Height());
    next_offset = (std::max)(0, (std::min)(next_offset, max_offset));
    if (next_offset == scroll_offset_) {
        return;
    }

    const int delta = scroll_offset_ - next_offset;
    scroll_offset_ = next_offset;
    SetScrollPos(SB_VERT, scroll_offset_, TRUE);
    ScrollWindowEx(0,
                   delta,
                   nullptr,
                   nullptr,
                   nullptr,
                   nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CSgpioTab::LayoutScrolledContent() {
    if (!::IsWindow(GetSafeHwnd())) {
        return;
    }

    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0) {
        return;
    }

    UpdateVerticalScroll(client);

    CRect page(client);
    page.top -= scroll_offset_;
    page.bottom = page.top + virtual_content_height_;

    SetRedraw(FALSE);
    LayoutControls(page);
    SetRedraw(TRUE);
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CSgpioTab::LayoutControls(const CRect& r) {
    const mfc_tool::ui::DpiScaler dpi = mfc_tool::ui::DpiScaler::FromWindow(*this);
    const mfc_tool::ui::LayoutMetrics metrics = mfc_tool::ui::MetricsForWindow(*this);
    const int margin = metrics.margin8;
    const int gap = metrics.gap;
    const int row = metrics.row26;
    const int right = r.right - margin - dpi.Scale(16);
    const int inner_left = r.left + margin + dpi.Scale(12);
    const int group_h = (std::max)(dpi.Scale(250), r.Height() - margin * 2 - dpi.Scale(8));
    CRect group_rc;
    int grid_bottom = 0;
    int y = r.top + margin + dpi.Scale(28);
    int x = inner_left;
    int btn_w = dpi.Scale(92);
    int label_y = y + dpi.Scale(4);
    int row2_y = 0;
    int grid_top_y = 0;
    int status_label_w = 0;
    int sdata_label_w = 0;
    int status_field_w = dpi.Scale(84);
    int sdata_field_w = dpi.Scale(150);

    mfc_tool::ui::SafeMoveWindow(group_, r.left + margin, r.top + margin, r.Width() - margin * 2, group_h);
    group_rc.SetRect(r.left + margin, r.top + margin, r.right - margin, r.top + margin + group_h);
    grid_bottom = group_rc.bottom - dpi.Scale(12);

    {
        const int fixed_pin_w = (std::max)(dpi.Scale(360),
                                           mfc_tool::ui::MeasureControlTextWidth(fixed_pin_value_, dpi.Scale(12)));
        x = mfc_tool::ui::PlaceLabelAndControl(fixed_pin_title_,
                                               fixed_pin_value_,
                                               x,
                                               label_y,
                                               y + dpi.Scale(4),
                                               fixed_pin_w,
                                               metrics.row24,
                                               gap,
                                               dpi.Scale(8),
                                               metrics.label18) + dpi.Scale(14);
    }
    x = mfc_tool::ui::PlaceLabelAndControl(slot_count_title_, slot_count_edit_, x, label_y, y, dpi.Scale(50), row, gap, dpi.Scale(8), metrics.label18) + dpi.Scale(12);
    x = mfc_tool::ui::PlaceLabelAndControl(clock_title_, clock_edit_, x, label_y, y, dpi.Scale(70), row, gap, dpi.Scale(8), metrics.label18) + dpi.Scale(12);
    mfc_tool::ui::SafeMoveWindow(periodic_check_, x, y + dpi.Scale(2), dpi.Scale(110), metrics.row24);
    x += dpi.Scale(110) + gap;
    x = mfc_tool::ui::PlaceLabelAndControl(interval_title_, interval_edit_, x, label_y, y, dpi.Scale(64), row, gap, dpi.Scale(8), metrics.label18) + dpi.Scale(12);
    mfc_tool::ui::PlaceLabelAndControl(vendor_title_, vendor_edit_, x, label_y, y, dpi.Scale(52), row, gap, dpi.Scale(8), metrics.label18);

    btn_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(enable_btn_, dpi.Scale(20)));
    btn_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(disable_btn_, dpi.Scale(20)));
    btn_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(apply_btn_, dpi.Scale(20)));
    btn_w = (std::max)(btn_w, mfc_tool::ui::MeasureButtonMinWidth(status_btn_, dpi.Scale(20)));
    row2_y = y + row + gap;
    x = inner_left;
    mfc_tool::ui::SafeMoveWindow(enable_btn_, x, row2_y, btn_w, row);
    x += btn_w + gap;
    mfc_tool::ui::SafeMoveWindow(disable_btn_, x, row2_y, btn_w, row);
    x += btn_w + gap;
    mfc_tool::ui::SafeMoveWindow(apply_btn_, x, row2_y, btn_w, row);
    x += btn_w + gap;
    mfc_tool::ui::SafeMoveWindow(status_btn_, x, row2_y, btn_w, row);
    x += btn_w + dpi.Scale(18);
    x = mfc_tool::ui::PlaceLabelAndControl(slot_preset_title_, slot_index_combo_, x, row2_y + dpi.Scale(4), row2_y, dpi.Scale(58), dpi.Scale(200), gap, dpi.Scale(8), metrics.label18) + dpi.Scale(12);
    x = mfc_tool::ui::PlaceLabelAndControl(ibpi_preset_title_, ibpi_preset_combo_, x, row2_y + dpi.Scale(4), row2_y, dpi.Scale(112), dpi.Scale(240), gap, dpi.Scale(8), metrics.label18) + dpi.Scale(12);
    {
        const int preset_w = (std::max)(dpi.Scale(96), mfc_tool::ui::MeasureButtonMinWidth(preset_apply_btn_, dpi.Scale(20)));
        const int clear_w = (std::max)(dpi.Scale(88), mfc_tool::ui::MeasureButtonMinWidth(clear_all_btn_, dpi.Scale(20)));
        mfc_tool::ui::SafeMoveWindow(preset_apply_btn_, x, row2_y, preset_w, row);
        x += preset_w + gap;
        mfc_tool::ui::SafeMoveWindow(clear_all_btn_, x, row2_y, clear_w, row);
        x += clear_w;
    }
    status_label_w = mfc_tool::ui::MeasureControlTextWidth(status_title_, dpi.Scale(8));
    sdata_label_w = mfc_tool::ui::MeasureControlTextWidth(sdata_in_title_, dpi.Scale(8));
    grid_top_y = row2_y + row + gap + dpi.Scale(4);
    {
        const int sdata_x = right - (sdata_label_w + gap + sdata_field_w);
        const int status_x = sdata_x - gap - status_field_w - gap - status_label_w;
        if (status_x >= x + dpi.Scale(12)) {
            mfc_tool::ui::SafeMoveWindow(status_title_, status_x, row2_y + dpi.Scale(4), status_label_w, metrics.label18);
            mfc_tool::ui::SafeMoveWindow(status_value_, status_x + status_label_w + gap, row2_y + dpi.Scale(4), status_field_w, metrics.row24);
            mfc_tool::ui::SafeMoveWindow(sdata_in_title_, sdata_x, row2_y + dpi.Scale(4), sdata_label_w, metrics.label18);
            mfc_tool::ui::SafeMoveWindow(sdata_in_value_, sdata_x + sdata_label_w + gap, row2_y + dpi.Scale(4), sdata_field_w, metrics.row24);
        } else {
            const int row3_y = row2_y + row + gap;
            int sx = inner_left;
            int status_value_w = right - sx - status_label_w - gap - sdata_label_w - gap - sdata_field_w - gap;
            status_value_w = (std::max)(dpi.Scale(220), status_value_w);
            mfc_tool::ui::SafeMoveWindow(status_title_, sx, row3_y + dpi.Scale(4), status_label_w, metrics.label18);
            sx += status_label_w + gap;
            mfc_tool::ui::SafeMoveWindow(status_value_, sx, row3_y + dpi.Scale(4), status_value_w, metrics.row24);
            sx += status_value_w + gap;
            mfc_tool::ui::SafeMoveWindow(sdata_in_title_, sx, row3_y + dpi.Scale(4), sdata_label_w, metrics.label18);
            sx += sdata_label_w + gap;
            mfc_tool::ui::SafeMoveWindow(sdata_in_value_, sx, row3_y + dpi.Scale(4), sdata_field_w, metrics.row24);
            grid_top_y = row3_y + row + gap + dpi.Scale(4);
        }
    }

    LayoutSlotGrid(inner_left, grid_top_y, right, grid_bottom);
}

void CSgpioTab::LayoutSlotGrid(int left, int top, int right, int bottom) {
    const mfc_tool::ui::DpiScaler dpi = mfc_tool::ui::DpiScaler::FromWindow(*this);
    const mfc_tool::ui::LayoutMetrics metrics = mfc_tool::ui::MetricsForWindow(*this);
    const int gap = metrics.gap;
    const int min_row = dpi.Scale(18);
    const int rows_per_col = kSgpioSlotMax / 2;
    const int header_h = dpi.Scale(16);
    const int check_size = dpi.Scale(16);
    const int col1_w = dpi.Scale(44);
    const int col_w = dpi.Scale(64);
    const int block_w = col1_w + gap + col_w + gap + col_w + gap + col_w + dpi.Scale(12);
    const int avail_w = (std::max)(0, right - left);
    const int remain_w = (std::max)(0, avail_w - block_w * 2);
    int gap_each = dpi.Scale(18);
    int row_step = 0;
    int row_base_y = 0;
    int first_col_x = 0;
    int second_col_x = 0;
    int i = 0;

    if (block_w * 2 + gap_each > avail_w) {
        gap_each = (std::max)(gap, remain_w);
    }
    first_col_x = left;
    second_col_x = first_col_x + block_w + gap_each;
    if (second_col_x + block_w > right) {
        second_col_x = right - block_w;
        first_col_x = (std::max)(left, second_col_x - gap_each - block_w);
    }

    row_base_y = top + header_h + dpi.Scale(3);
    row_step = (bottom - row_base_y - dpi.Scale(4)) / rows_per_col;
    row_step = (std::max)(min_row, row_step);
    row_step = (std::min)(dpi.Scale(24), row_step);

    mfc_tool::ui::SafeMoveWindow(slot_hdr_, first_col_x, top, col1_w, header_h);
    mfc_tool::ui::SafeMoveWindow(act_hdr_, first_col_x + col1_w + gap, top, col_w, header_h);
    mfc_tool::ui::SafeMoveWindow(locate_hdr_, first_col_x + col1_w + gap + col_w + gap, top, col_w, header_h);
    mfc_tool::ui::SafeMoveWindow(fail_hdr_, first_col_x + col1_w + gap + col_w + gap + col_w + gap, top, col_w, header_h);
    for (i = 0; i < kSgpioSlotMax; ++i) {
        const bool left_block = i < 8;
        const int local_index = left_block ? i : (i - 8);
        const int base_x = left_block ? first_col_x : second_col_x;
        const int row_y = row_base_y + local_index * row_step;
        const int act_x = base_x + col1_w + gap;
        const int locate_x = act_x + col_w + gap;
        const int fail_x = locate_x + col_w + gap;
        const bool active_slot = i < EffectiveSlotCount();
        mfc_tool::ui::SafeMoveWindow(slot_labels_[i], base_x, row_y + dpi.Scale(1), col1_w, metrics.label18);
        mfc_tool::ui::SafeMoveWindow(act_checks_[i], act_x + (col_w - check_size) / 2, row_y, check_size, check_size);
        mfc_tool::ui::SafeMoveWindow(locate_checks_[i], locate_x + (col_w - check_size) / 2, row_y, check_size, check_size);
        mfc_tool::ui::SafeMoveWindow(fail_checks_[i], fail_x + (col_w - check_size) / 2, row_y, check_size, check_size);
        mfc_tool::ui::SafeEnableWindow(slot_labels_[i], active_slot ? TRUE : FALSE);
        mfc_tool::ui::SafeEnableWindow(act_checks_[i], (connected_ && active_slot) ? TRUE : FALSE);
        mfc_tool::ui::SafeEnableWindow(locate_checks_[i], (connected_ && active_slot) ? TRUE : FALSE);
        mfc_tool::ui::SafeEnableWindow(fail_checks_[i], (connected_ && active_slot) ? TRUE : FALSE);
    }
}

void CSgpioTab::SetConnected(bool connected) {
    connected_ = connected;
    UpdateEnableState();
}

void CSgpioTab::OnDisconnected() {
    enabled_ = false;
    SetStatusText(L"Disconnected");
    SetSdataInText(-1);
    RefreshPinUsage();
    UpdateEnableState();
}

void CSgpioTab::LoadState(const mfc_tool::core::AppState& state) {
    slot_count_edit_.SetWindowTextW(state.sgpio.slot_count.c_str());
    clock_edit_.SetWindowTextW(state.sgpio.clock_hz.c_str());
    periodic_check_.SetCheck(state.sgpio.periodic == L"1" ? BST_CHECKED : BST_UNCHECKED);
    interval_edit_.SetWindowTextW(state.sgpio.interval_ms.c_str());
    vendor_edit_.SetWindowTextW(state.sgpio.sload_raw.c_str());
    ApplyMasksToChecks(mfc_tool::core::ParseInt(state.sgpio.act_mask),
                       mfc_tool::core::ParseInt(state.sgpio.locate_mask),
                       mfc_tool::core::ParseInt(state.sgpio.fail_mask));
    enabled_ = false;
    SetStatusText(L"Idle");
    SetSdataInText(-1);
    RefreshPinUsage();
    UpdateEnableState();
}

void CSgpioTab::SaveState(mfc_tool::core::AppState* state) const {
    int act_mask = 0;
    int locate_mask = 0;
    int fail_mask = 0;
    if (state == nullptr) {
        return;
    }
    BuildMasksFromChecks(&act_mask, &locate_mask, &fail_mask);
    state->sgpio.slot_count = GetEditText(slot_count_edit_);
    state->sgpio.clock_hz = GetEditText(clock_edit_);
    state->sgpio.periodic = periodic_check_.GetCheck() == BST_CHECKED ? L"1" : L"0";
    state->sgpio.interval_ms = GetEditText(interval_edit_);
    state->sgpio.sload_raw = GetEditText(vendor_edit_);
    {
        wchar_t buf[32] = {};
        swprintf_s(buf, L"0x%04X", act_mask & 0xFFFF);
        state->sgpio.act_mask = buf;
        swprintf_s(buf, L"0x%04X", locate_mask & 0xFFFF);
        state->sgpio.locate_mask = buf;
        swprintf_s(buf, L"0x%04X", fail_mask & 0xFFFF);
        state->sgpio.fail_mask = buf;
    }
}

void CSgpioTab::UpdateEnableState() {
    if (!::IsWindow(enable_btn_.GetSafeHwnd())) {
        return;
    }

    const BOOL connected = connected_ ? TRUE : FALSE;
    const BOOL editable = (connected_ && !enabled_) ? TRUE : FALSE;
    const BOOL any_connected = connected;
    BOOL shared_blocked = FALSE;
    const int slots = EffectiveSlotCount();
    int i = 0;

    if (pin_usage_ != nullptr) {
        shared_blocked = pin_usage_->AnyActiveExcept(fixed_topology::SharedFixedTopologyOwners(),
                                                     fixed_topology::SgpioOwnerSet()) ? TRUE : FALSE;
    }

    mfc_tool::ui::SafeEnableWindow(enable_btn_, (connected_ && !enabled_ && !shared_blocked) ? TRUE : FALSE);
    mfc_tool::ui::SafeEnableWindow(disable_btn_, (connected_ && enabled_) ? TRUE : FALSE);
    mfc_tool::ui::SafeEnableWindow(apply_btn_, (connected_ && enabled_) ? TRUE : FALSE);
    mfc_tool::ui::SafeEnableWindow(status_btn_, connected);
    mfc_tool::ui::SafeEnableWindow(slot_count_edit_, editable);
    mfc_tool::ui::SafeEnableWindow(clock_edit_, editable);
    mfc_tool::ui::SafeEnableWindow(periodic_check_, any_connected);
    mfc_tool::ui::SafeEnableWindow(interval_edit_, any_connected);
    mfc_tool::ui::SafeEnableWindow(vendor_edit_, any_connected);
    mfc_tool::ui::SafeEnableWindow(slot_index_combo_, any_connected);
    mfc_tool::ui::SafeEnableWindow(ibpi_preset_combo_, any_connected);
    mfc_tool::ui::SafeEnableWindow(preset_apply_btn_, any_connected);
    mfc_tool::ui::SafeEnableWindow(clear_all_btn_, any_connected);
    for (i = 0; i < kSgpioSlotMax; ++i) {
        const BOOL slot_enabled = (connected_ && i < slots) ? TRUE : FALSE;
        mfc_tool::ui::SafeEnableWindow(act_checks_[i], slot_enabled);
        mfc_tool::ui::SafeEnableWindow(locate_checks_[i], slot_enabled);
        mfc_tool::ui::SafeEnableWindow(fail_checks_[i], slot_enabled);
    }
}

int CSgpioTab::ParseEditInt(const CEdit& edit) const {
    CString s;
    const_cast<CEdit&>(edit).GetWindowTextW(s);
    return mfc_tool::core::ParseInt(s.GetString());
}

std::wstring CSgpioTab::GetEditText(const CEdit& edit) const {
    CString s;
    const_cast<CEdit&>(edit).GetWindowTextW(s);
    return s.GetString();
}

std::wstring CSgpioTab::StatusSummary(bool enabled, bool periodic, int slot_count, int clock_hz,
                                      int interval_ms, int sload_raw, int act_mask,
                                      int locate_mask, int fail_mask, int sdata_in_level) const {
    wchar_t buf[320] = {};
    wchar_t sdin_buf[8] = {};
    if (sdata_in_level < 0) {
        wcscpy_s(sdin_buf, L"-");
    } else {
        swprintf_s(sdin_buf, L"%d", sdata_in_level ? 1 : 0);
    }
    swprintf_s(buf, L"enabled=%d periodic=%d slots=%d clk=%dHz interval=%dms SLOAD L0..L3=0x%X SDataOut ACT=0x%04X LOCATE=0x%04X FAIL=0x%04X SDIN=%ls",
               enabled ? 1 : 0, periodic ? 1 : 0, slot_count, clock_hz, interval_ms,
               sload_raw & 0x0F, act_mask & 0xFFFF, locate_mask & 0xFFFF, fail_mask & 0xFFFF, sdin_buf);
    return buf;
}

void CSgpioTab::SetStatusText(const std::wstring& text) {
    status_value_.SetWindowTextW(text.c_str());
}

void CSgpioTab::SetSdataInText(int level) {
    if (level < 0) {
        sdata_in_value_.SetWindowTextW(L"-");
    } else {
        sdata_in_value_.SetWindowTextW(level ? L"1" : L"0");
    }
}

void CSgpioTab::SetSdataInText(const std::wstring& text) {
    sdata_in_value_.SetWindowTextW(text.c_str());
}

void CSgpioTab::RefreshPinUsage() {
    if (pin_usage_ == nullptr) {
        return;
    }
    pin_usage_->SetLabel(fixed_topology::kSgpio, L"SGPIO");
    pin_usage_->SetClaim(fixed_topology::kSgpio, fixed_topology::SgpioPins());
    pin_usage_->SetActive(fixed_topology::kSgpio, enabled_);
}

void CSgpioTab::ApplyMasksToChecks(int act_mask, int locate_mask, int fail_mask) {
    int i = 0;
    for (i = 0; i < kSgpioSlotMax; ++i) {
        act_checks_[i].SetCheck((act_mask & (1 << i)) ? BST_CHECKED : BST_UNCHECKED);
        locate_checks_[i].SetCheck((locate_mask & (1 << i)) ? BST_CHECKED : BST_UNCHECKED);
        fail_checks_[i].SetCheck((fail_mask & (1 << i)) ? BST_CHECKED : BST_UNCHECKED);
    }
}

void CSgpioTab::BuildMasksFromChecks(int* act_mask, int* locate_mask, int* fail_mask) const {
    int act = 0;
    int locate = 0;
    int fail = 0;
    int i = 0;
    const int slots = EffectiveSlotCount();
    for (i = 0; i < slots; ++i) {
        if (act_checks_[i].GetCheck() == BST_CHECKED) {
            act |= (1 << i);
        }
        if (locate_checks_[i].GetCheck() == BST_CHECKED) {
            locate |= (1 << i);
        }
        if (fail_checks_[i].GetCheck() == BST_CHECKED) {
            fail |= (1 << i);
        }
    }
    if (act_mask != nullptr) {
        *act_mask = act;
    }
    if (locate_mask != nullptr) {
        *locate_mask = locate;
    }
    if (fail_mask != nullptr) {
        *fail_mask = fail;
    }
}

void CSgpioTab::PopulateSlotCombos() {
    int i = 0;
    slot_index_combo_.ResetContent();
    for (i = 0; i < kSgpioSlotMax; ++i) {
        CString s;
        s.Format(L"%d", i);
        slot_index_combo_.AddString(s);
    }
    slot_index_combo_.SetCurSel(0);
}

void CSgpioTab::ApplySelectedSlotPreset() {
    const int slot = slot_index_combo_.GetCurSel();
    const int preset = ibpi_preset_combo_.GetCurSel();
    if (slot < 0 || slot >= kSgpioSlotMax || preset < 0) {
        return;
    }

    act_checks_[slot].SetCheck((preset == 1 || preset == 4 || preset == 5 || preset == 7) ? BST_CHECKED : BST_UNCHECKED);
    locate_checks_[slot].SetCheck((preset == 2 || preset == 4 || preset == 6 || preset == 7) ? BST_CHECKED : BST_UNCHECKED);
    fail_checks_[slot].SetCheck((preset == 3 || preset == 5 || preset == 6 || preset == 7) ? BST_CHECKED : BST_UNCHECKED);
}

int CSgpioTab::EffectiveSlotCount() const {
    int slots = 0;
    try {
        slots = ParseEditInt(slot_count_edit_);
    } catch (...) {
        slots = 8;
    }
    slots = (std::max)(1, slots);
    slots = (std::min)(kSgpioSlotMax, slots);
    return slots;
}

void CSgpioTab::ApplyFromUi(bool enable) {
    const bool was_enabled = enabled_;
    const int slot_count = ParseEditInt(slot_count_edit_);
    const int clock_hz = ParseEditInt(clock_edit_);
    const bool periodic = periodic_check_.GetCheck() == BST_CHECKED;
    const int interval_ms = ParseEditInt(interval_edit_);
    const int sload_raw = ParseEditInt(vendor_edit_);
    int act_mask = 0;
    int locate_mask = 0;
    int fail_mask = 0;

    BuildMasksFromChecks(&act_mask, &locate_mask, &fail_mask);

    if (pin_usage_ != nullptr && enable) {
        if (pin_usage_->AnyActiveExcept(fixed_topology::SharedFixedTopologyOwners(),
                                        fixed_topology::SgpioOwnerSet())) {
            throw std::runtime_error("Another fixed-topology function is active. Disable it first before enabling SGPIO.");
        }
        if (pin_usage_->AnyPinOccupied(fixed_topology::SgpioPins(), fixed_topology::SgpioOwnerSet())) {
            throw std::runtime_error("SGPIO requires M032 PA2/PA0/PA3/PA1. Disable the conflicting active function first.");
        }
    }

    service_->SgpioConfig(slot_count, clock_hz);
    service_->SgpioApply(enable, periodic, interval_ms, sload_raw, act_mask, locate_mask, fail_mask);
    enabled_ = enable;
    RefreshPinUsage();

    {
        const std::wstring summary = StatusSummary(enabled_, periodic, slot_count, clock_hz, interval_ms,
                                                   sload_raw, act_mask, locate_mask, fail_mask, -1);
        SetStatusText(summary);
        if (log_) {
            log_(std::wstring(L"SGPIO ") + (enable ? (was_enabled ? L"applied: " : L"enabled: ") : L"disabled: ") + summary);
        }
    }
}

std::wstring CSgpioTab::AnsiToWide(const char* text) {
    if (text == nullptr) {
        return L"";
    }
    const int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (n <= 0) {
        return L"";
    }
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, out.data(), n);
    return out;
}

void CSgpioTab::OnBtnEnable() {
    if (!connected_ || service_ == nullptr) {
        return;
    }
    try {
        ApplyFromUi(true);
    } catch (const std::exception& e) {
        ::MessageBoxW(m_hWnd, AnsiToWide(e.what()).c_str(), L"SGPIO Error", MB_ICONERROR | MB_OK);
    }
    UpdateEnableState();
}

void CSgpioTab::OnBtnDisable() {
    if (!connected_ || service_ == nullptr) {
        return;
    }
    try {
        service_->SgpioOff();
        enabled_ = false;
        RefreshPinUsage();
        SetStatusText(L"SGPIO disabled");
        SetSdataInText(-1);
        if (log_) {
            log_(L"SGPIO disabled on PA2/PA0/PA3/PA1");
        }
    } catch (const std::exception& e) {
        ::MessageBoxW(m_hWnd, AnsiToWide(e.what()).c_str(), L"SGPIO Error", MB_ICONERROR | MB_OK);
    }
    UpdateEnableState();
}

void CSgpioTab::OnBtnApplyNow() {
    if (!connected_ || !enabled_ || service_ == nullptr) {
        return;
    }
    try {
        ApplyFromUi(true);
    } catch (const std::exception& e) {
        ::MessageBoxW(m_hWnd, AnsiToWide(e.what()).c_str(), L"SGPIO Error", MB_ICONERROR | MB_OK);
    }
    UpdateEnableState();
}

void CSgpioTab::OnBtnStatus() {
    if (!connected_ || service_ == nullptr) {
        return;
    }
    try {
        const auto rx = service_->SgpioStatus();
        if (rx.size() < kSgpioStatusBaseSize) {
            throw std::runtime_error("invalid SGPIO status payload");
        }

        enabled_ = rx[0] != 0u;
        periodic_check_.SetCheck(rx[1] ? BST_CHECKED : BST_UNCHECKED);
        slot_count_edit_.SetWindowTextW(std::to_wstring(static_cast<int>(rx[2])).c_str());
        {
            const int sload_raw = static_cast<int>(rx[3] & 0x0F);
            const int clock_hz = static_cast<int>(rx[4] | (rx[5] << 8) | (rx[6] << 16) | (rx[7] << 24));
            const int interval_ms = static_cast<int>(rx[8] | (rx[9] << 8));
            const int act_mask = static_cast<int>(rx[10] | (rx[11] << 8));
            const int locate_mask = static_cast<int>(rx[12] | (rx[13] << 8));
            const int fail_mask = static_cast<int>(rx[14] | (rx[15] << 8));
            const int sdata_in_level = static_cast<int>(rx[20] & 0x01);
            const std::wstring sdin_capture = FormatSdinCapture(rx);
            wchar_t buf[32] = {};
            std::wstring summary;

            clock_edit_.SetWindowTextW(std::to_wstring(clock_hz).c_str());
            interval_edit_.SetWindowTextW(std::to_wstring(interval_ms).c_str());
            swprintf_s(buf, L"0x%X", sload_raw & 0x0F);
            vendor_edit_.SetWindowTextW(buf);
            ApplyMasksToChecks(act_mask, locate_mask, fail_mask);
            SetSdataInText(sdin_capture == L"-" ? std::to_wstring(sdata_in_level) : sdin_capture);
            summary = StatusSummary(enabled_, rx[1] != 0u, static_cast<int>(rx[2]), clock_hz,
                                    interval_ms, sload_raw, act_mask, locate_mask, fail_mask, sdata_in_level);
            if (sdin_capture != L"-") {
                summary += L" SDIN_RAW=" + sdin_capture;
            }
            SetStatusText(summary);
        }
        RefreshPinUsage();
        if (log_) {
            log_(L"SGPIO status: " + GetEditText(slot_count_edit_) + L" slots, PA2/PA0/PA3/PA1, SDIN=" +
                 std::wstring(L"") + FormatSdinCapture(rx));
        }
    } catch (const std::exception& e) {
        ::MessageBoxW(m_hWnd, AnsiToWide(e.what()).c_str(), L"SGPIO Error", MB_ICONERROR | MB_OK);
    }
    UpdateEnableState();
}

void CSgpioTab::OnBtnPresetApply() {
    ApplySelectedSlotPreset();
}

void CSgpioTab::OnBtnClearAll() {
    ApplyMasksToChecks(0, 0, 0);
}

void CSgpioTab::OnSlotCountChanged() {
    UpdateEnableState();
    if (::IsWindow(GetSafeHwnd())) {
        LayoutScrolledContent();
    }
}
