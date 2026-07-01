# AGENTS.md

## Project Scope

This workspace is the M032 EVB SGPIO HID GUI tool.

- PC GUI: MFC HID tool, based on the sibling `simple_pmbus_smbus_tool` structure.
- SGPIO UI behavior: based on the `SGPIO` tab from the HID test tool reference project.
- Firmware: `demo_code\M031BSP_USB_HID_SGPIO`, using M032/M031 USB HID as an SGPIO host bridge.

## Local Rules

- Keep the PC GUI focused on SGPIO only.
- Do not add Pico board references to user-facing UI text.
- Keep M032 SGPIO pin mapping stable:
  - PA2: SCLK
  - PA0: SDATA out
  - PA3: SLOAD
  - PA1: SDATA in, optional
- Firmware C code should remain compatible with the existing Nuvoton sample style and C90-friendly declarations.
- Do not refactor the inherited HID/I2C firmware foundation unless it blocks SGPIO.

## Verification Expectations

- PC GUI changes: build `SgpioHidTool.sln` with `scripts\build_mfc.ps1`.
- Firmware changes: build the Keil project under `demo_code\M031BSP_USB_HID_SGPIO\SampleCode\Template\Keil`.
- Hardware validation requires an M032 EVB and one of the SGPIO target projects listed in `README.md`.
