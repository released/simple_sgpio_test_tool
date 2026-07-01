#pragma once

#include <afxwin.h>

#include <array>
#include <functional>
#include <string>

#include "../core/app_state.h"
#include "../core/bridge_service.h"
#include "../core/pin_usage_registry.h"

class CSgpioTab : public CWnd {
public:
    BOOL Create(CWnd* parent, const RECT& rect, UINT id);

    void Bind(mfc_tool::core::BridgeService* service,
              std::function<void(const std::wstring&)> logger,
              mfc_tool::core::PinUsageRegistry* pin_usage);
    void SetConnected(bool connected);
    void OnDisconnected();

    void LoadState(const mfc_tool::core::AppState& state);
    void SaveState(mfc_tool::core::AppState* state) const;

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnBtnEnable();
    afx_msg void OnBtnDisable();
    afx_msg void OnBtnApplyNow();
    afx_msg void OnBtnStatus();
    afx_msg void OnBtnPresetApply();
    afx_msg void OnBtnClearAll();
    afx_msg void OnSlotCountChanged();
    DECLARE_MESSAGE_MAP()

private:
    void UpdateEnableState();
    void LayoutSlotGrid(int left, int top, int right, int bottom);
    int ParseEditInt(const CEdit& edit) const;
    std::wstring GetEditText(const CEdit& edit) const;
    std::wstring StatusSummary(bool enabled, bool periodic, int slot_count, int clock_hz,
                               int interval_ms, int sload_raw, int act_mask,
                               int locate_mask, int fail_mask, int sdata_in_level) const;
    void SetStatusText(const std::wstring& text);
    void RefreshPinUsage();
    void ApplyFromUi(bool enable);
    void ApplyMasksToChecks(int act_mask, int locate_mask, int fail_mask);
    void BuildMasksFromChecks(int* act_mask, int* locate_mask, int* fail_mask) const;
    void PopulateSlotCombos();
    void ApplySelectedSlotPreset();
    int EffectiveSlotCount() const;
    void SetSdataInText(int level);
    void SetSdataInText(const std::wstring& text);
    static std::wstring AnsiToWide(const char* text);

private:
    mfc_tool::core::BridgeService* service_ = nullptr;
    mfc_tool::core::PinUsageRegistry* pin_usage_ = nullptr;
    std::function<void(const std::wstring&)> log_;
    bool connected_ = false;
    bool enabled_ = false;

    CFont ui_font_;
    CFont slot_font_;
    CButton group_;
    CStatic fixed_pin_title_;
    CStatic fixed_pin_value_;
    CStatic slot_count_title_;
    CEdit slot_count_edit_;
    CStatic clock_title_;
    CEdit clock_edit_;
    CButton periodic_check_;
    CStatic interval_title_;
    CEdit interval_edit_;
    CStatic vendor_title_;
    CEdit vendor_edit_;
    CButton enable_btn_;
    CButton disable_btn_;
    CButton apply_btn_;
    CButton status_btn_;
    CStatic status_title_;
    CStatic status_value_;
    CStatic sdata_in_title_;
    CStatic sdata_in_value_;

    CStatic slot_preset_title_;
    CComboBox slot_index_combo_;
    CStatic ibpi_preset_title_;
    CComboBox ibpi_preset_combo_;
    CButton preset_apply_btn_;
    CButton clear_all_btn_;

    CStatic slot_hdr_;
    CStatic act_hdr_;
    CStatic locate_hdr_;
    CStatic fail_hdr_;
    std::array<CStatic, 16> slot_labels_;
    std::array<CButton, 16> act_checks_;
    std::array<CButton, 16> locate_checks_;
    std::array<CButton, 16> fail_checks_;
};
