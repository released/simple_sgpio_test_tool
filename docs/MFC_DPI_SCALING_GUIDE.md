# MFC DPI Scaling Guide

This GUI is maintained for 100%, 125%, and 150% Windows display scaling.

The goal is not a fully fluid UI for every arbitrary DPI value. The supported
contract is that the SGPIO HID tool remains readable and usable at the common
lab PC scaling settings used for 1080p, 1440p, and notebook displays.

## Supported Scaling

| Windows scale | DPI | Expected status |
| --- | ---: | --- |
| 100% | 96 | Primary development baseline |
| 125% | 120 | Supported |
| 150% | 144 | Supported |

## DPI Helper Layer

Shared helpers live in `src\ui\layout_utils.h`.

- `GetDpiForHwnd()` and `GetDpiForWnd()` read the active window DPI through
  `GetDpiForWindow` when available, with a `GetDeviceCaps(LOGPIXELSX)` fallback.
- `DpiScaler` centralizes `MulDiv(value, dpi, 96)` scaling.
- `MetricsForWindow()` provides common scaled layout metrics such as margins,
  gaps, row heights, label height, and combo drop-down height.
- `CreatePointFontForWindow()` recreates MFC fonts against the current window
  device context so text follows the monitor DPI.
- `ApplyFontToChildWindows()` reapplies recreated fonts to child controls.

Keep new layout code using these helpers instead of hard-coded 96-DPI pixel
values.

## Main Frame DPI Handling

`src\main_frame.cpp` handles `WM_DPICHANGED`.

When Windows moves the window to a monitor with a different DPI, the handler:

1. Stores the new DPI from `WM_DPICHANGED`.
2. Applies the suggested window rectangle from Windows.
3. Recreates the main UI font.
4. Reapplies the font to top-level HID controls, log controls, and the tab.
5. Rescales the tab item size.
6. Calls `CSgpioTab::RefreshDpiLayout()`.
7. Re-runs `LayoutControls()` and invalidates child windows.

`OnGetMinMaxInfo()` also scales the minimum tracking size. This prevents the
100% layout from being resized into an unusable 125% or 150% shape.

## Scaled Metrics

Both the main frame and SGPIO tab use scaled layout values for:

- outer margin
- control gap
- row height
- label height
- tab inset
- combo drop-down height
- SGPIO slot grid row spacing
- SGPIO checkbox size

The SGPIO tab also keeps a responsive fallback for the dense status area. When
the second row does not have enough horizontal space, the `Status` and
`SDataIn` fields move to the next row. The tab's vertical scrollbar handles the
extra height.

## Button Text Measure

Button widths are based on live text measurement through:

- `MeasureControlTextWidth()`
- `MeasureButtonMinWidth()`

Layouts pass scaled padding to these helpers. This keeps labels such as
`Disconnect`, `Reset MCU`, `Apply Preset`, and `Clear All` from clipping when
the font is recreated at 125% or 150%.

When adding a new button, avoid a fixed width unless it is a true icon-only or
fixed-format control. Use `MeasureButtonMinWidth()` and then apply a reasonable
scaled minimum.

## Dense Tab Scrollbar

The SGPIO tab is intentionally dense because the 16-slot ACT/LOCATE/FAIL matrix
must stay visible for lab bring-up.

`CSgpioTab` is created with `WS_VSCROLL` and tracks:

- `scroll_offset_`
- `virtual_content_height_`

Relevant methods:

- `CalculateVirtualContentHeight()`
- `UpdateVerticalScroll()`
- `ScrollToOffset()`
- `LayoutScrolledContent()`
- `LayoutControls()`

`OnSize()`, `OnMouseWheel()`, `OnVScroll()`, and `OnSlotCountChanged()` all route
through the same scroll-aware layout path.

## ScrollWindowEx Ghosting Countermeasure

Dense MFC child controls can leave stale pixels or stacked-control remnants
when scrolling with many child windows.

`CSgpioTab::ScrollToOffset()` uses:

```cpp
ScrollWindowEx(0,
               delta,
               nullptr,
               nullptr,
               nullptr,
               nullptr,
               SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
```

This scrolls child controls and explicitly invalidates/erases the affected
area, which prevents visible ghosting after wheel or scrollbar movement.

## 100/125/150% Verification Matrix

Run this matrix after any layout, font, tab, or top-level toolbar change.

| Area | 100% | 125% | 150% |
| --- | --- | --- | --- |
| Main window opens and maximizes | Pass required | Pass required | Pass required |
| Top HID controls fit without clipping | Pass required | Pass required | Pass required |
| VID/PID/Timeout edits remain usable | Pass required | Pass required | Pass required |
| Connect/Get Info/Ping/Reset buttons fit text | Pass required | Pass required | Pass required |
| INI, build, and firmware text rows do not overlap | Pass required | Pass required | Pass required |
| SGPIO tab title and group box are readable | Pass required | Pass required | Pass required |
| Fixed pin reminder is visible | Pass required | Pass required | Pass required |
| Slot Count/Clock/Periodic/Interval/SLOAD row fits | Pass required | Pass required | Pass required |
| Enable/Disable/Apply/Get Status row fits | Pass required | Pass required | Pass required |
| Preset controls fit or status row wraps cleanly | Pass required | Pass required | Pass required |
| Status and SDataIn fields do not overlap controls | Pass required | Pass required | Pass required |
| 16-slot ACT/LOCATE/FAIL grid is readable | Pass required | Pass required | Pass required |
| Vertical scrollbar appears when content exceeds tab | Pass required | Pass required | Pass required |
| Mouse wheel scroll has no stale control artifacts | Pass required | Pass required | Pass required |
| Scrollbar thumb scroll has no stale control artifacts | Pass required | Pass required | Pass required |
| Log buttons and log edit remain visible | Pass required | Pass required | Pass required |

## Manual Test Steps

1. Set Windows display scaling to 100%.
2. Launch `build\SgpioHidTool.exe`.
3. Confirm the top controls, SGPIO tab, slot grid, and log area are readable.
4. Set `Slot Count=16` and verify all slot rows can be reached.
5. Scroll the SGPIO tab with mouse wheel and scrollbar.
6. Repeat the same check at 125%.
7. Repeat the same check at 150%.
8. If available, move the running window between monitors with different DPI and
   confirm `WM_DPICHANGED` refreshes fonts and layout without stale pixels.

Hardware is not required for the DPI visual pass. HID connect and SGPIO output
validation remain separate hardware checks.
