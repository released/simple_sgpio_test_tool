# HANDOFF

## Current State

- The workspace has been scaffolded as an SGPIO-only MFC HID GUI.
- The SGPIO tab behavior was ported from `HidTestToolForRasberryPiPico` and relabeled for M032 PA pins.
- The firmware HID dispatcher exposes core commands and SGPIO commands only.
- The M032 firmware uses Timer0-triggered PDMA writes to PA DOUT to output SGPIO on PA2/PA0/PA3, and reports PA1 as optional SDATA in.

## Key Files

- PC entry point and shell:
  - `src\app.*`
  - `src\main.*`
  - `src\main_frame.*`
- SGPIO UI:
  - `src\ui\sgpio_tab.*`
- HID command service:
  - `src\core\bridge_service.*`
  - `src\core\bridge_commands.h`
- Firmware SGPIO:
  - `demo_code\M031BSP_USB_HID_SGPIO\SampleCode\Template\m031_bridge_sgpio.*`
  - `demo_code\M031BSP_USB_HID_SGPIO\SampleCode\Template\hid_tool_api.c`
  - `demo_code\M031BSP_USB_HID_SGPIO\SampleCode\Template\bridge_protocol.h`

## Hardware Notes

- M032 PA2 -> target PA2 SCLK
- M032 PA0 -> target PA0 SDATA out
- M032 PA3 -> target PA3 SLOAD
- M032 GND -> target GND
- M032 PA1 is optional SDATA in status only; capture words are not implemented on M032.

## Remaining Hardware Check

After building/flashing firmware, verify on a logic analyzer:

- SCLK frequency roughly follows the GUI clock setting.
- Frame starts with five SLOAD low clocks, then one SLOAD high restart clock.
- Slot bits are ACT, LOCATE, FAIL in LSB slot order.
- SLOAD returns idle high after the payload.
- SGPIO clock should clamp to 100 kHz through 400 kHz.
