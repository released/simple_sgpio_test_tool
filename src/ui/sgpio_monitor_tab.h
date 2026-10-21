#pragma once

#include <afxcmn.h>
#include <afxwin.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/app_state.h"
#include "../core/bridge_service.h"
#include "../core/pin_usage_registry.h"

class CSgpioMonitorTab : public CWnd {
public:
    BOOL Create(CWnd* parent, const RECT& rect, UINT id);

    void Bind(mfc_tool::core::BridgeService* service,
              std::function<void(const std::wstring&)> logger,
              mfc_tool::core::PinUsageRegistry* pin_usage);
    void SetConnected(bool connected);
    void OnDisconnected();
    void RefreshDpiLayout();

    void LoadState(const mfc_tool::core::AppState& state);
    void SaveState(mfc_tool::core::AppState* state) const;

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR timer_id);
    afx_msg void OnBtnArm();
    afx_msg void OnBtnStop();
    afx_msg void OnBtnStatus();
    DECLARE_MESSAGE_MAP()

private:
    void LayoutControls(const CRect& client);
    void UpdateEnableState();
    void RefreshPinUsage();
    void StartPolling();
    void StopPolling();
    void ReadAndDisplayStatus(bool from_poll);
    void ApplyStatus(const std::vector<std::uint8_t>& data);
    void UpdateSlotRows(int slot_count, std::uint16_t act_mask,
                        std::uint16_t locate_mask, std::uint16_t fail_mask);
    int ExpectedSlotCount() const;
    void SetExpectedSlotCount(int slot_count);
    std::wstring GetTimeoutText() const;
    static std::wstring StateText(std::uint8_t state);
    static std::wstring ErrorText(std::uint16_t flags);
    static std::wstring AnsiToWide(const char* text);

private:
    mfc_tool::core::BridgeService* service_ = nullptr;
    mfc_tool::core::PinUsageRegistry* pin_usage_ = nullptr;
    std::function<void(const std::wstring&)> log_;
    bool connected_ = false;
    bool armed_ = false;
    bool polling_ = false;
    std::uint8_t current_state_ = 0u;

    CFont ui_font_;
    CFont slot_font_;
    CButton group_;
    CStatic fixed_pin_title_;
    CStatic fixed_pin_value_;
    CStatic expected_slot_title_;
    CComboBox expected_slot_combo_;
    CStatic timeout_title_;
    CEdit timeout_edit_;
    CButton arm_btn_;
    CButton stop_btn_;
    CButton status_btn_;
    CStatic state_title_;
    CStatic state_value_;
    CStatic clock_title_;
    CStatic clock_value_;
    CStatic timing_title_;
    CStatic timing_value_;
    CStatic frame_title_;
    CStatic frame_value_;
    CStatic capture_title_;
    CStatic capture_value_;
    CStatic errors_title_;
    CStatic errors_value_;
    CListCtrl slot_list_;
};
