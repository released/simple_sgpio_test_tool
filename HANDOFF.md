# HANDOFF

## Verified x64 EXE slimming 2026-10-25

- Canonical Release and isolated Debug builds, owned control/dialog/normal-close
  checks and five alternating performance pairs PASS; no real INI/device/clipboard.
- EXE 2849792 -> 760320 bytes; required files 2849792 -> 760320 bytes.
- SHA-256: 6ED7B65BA32A2980C63C0F38936A91F913B141B9E33D5603E59175613F32704B.
- Actual clean removed 30 compiler files; all 11 protected actual
  files unchanged by SHA. Nested fixtures/help/idempotence PASS.
- Raw sanitized samples/limits: docs/verification/exe-slimming-20261025.json.
  Physical devices, firmware, visual/DPI/clean-PC/Win32 acceptance NOT RUN.
- Ordinary publication commit/remote SHA: shared EXE_SLIMMING_PROGRESS.md.

## Authorized publication preflight (2026-10-21)

- The user now authorizes publishing this repository, superseding the earlier deferred-push note. Scope includes the preserved feature work, layout/clean changes, documentation and reviewed native delivery; no unrelated repository is included.
- Build/tests: Debug and Release x64 builds PASS, 0 warnings/errors. Delivery version 1.0.0.014. Isolated native startup/normal-close PASS; no device connected.
- Clean PASS: actual scripts/clean_mfc.bat; 11 protected output files unchanged by SHA-256. Expanded fixtures retained 17 delivery/runtime/cache sentinels plus source; object removal, help, repeat clean and reparse rejection PASS.
- Privacy/index: 13 tracked runtime INI/log/Keil user-option paths removed from index only; all local bytes/hash preserved. Existing firmware/monitor changes and public screenshot relocation are included; no private debugger settings or new logs.
- Firmware/limits: PASS: isolated M032 and M261 application copies built with Keil/Arm Compiler 6.22, both 0 errors/warnings. Earlier FlexNet failure is historical, not this run; hardware SGPIO acceptance NOT RUN.
- Candidate text/EXE checks found no tested private-key, access-token or personal-profile-path patterns; this is not a full secret/license audit. Only ordinary non-force publication to the verified existing main is authorized; exact final commit/remote SHA is recorded in the workspace migration ledger.


## Workspace layout / clean audit (2026-10-21)

- Scope: Moved main.jpg to `docs/images/main.jpg` with hash/no-overwrite verification and fixed its README link. Existing PC and vendor/firmware paths remain intact.
- Build/tests: PASS: `scripts/build_mfc.ps1 -Configuration Release -CloseRunningTool $false`, 0 warnings/errors, normal generated version 1.0.0.012. Shared isolated EXE startup/captioned-frame/normal-close PASS, exit 0; no SGPIO/device command.
- PASS: shared `verify_clean_preservation.ps1 -ProjectRoot <this-project> -CleanBatch scripts\clean_mfc.bat -CleanActualProject -CheckNestedDelivery -CheckReparseRejection`. Non-destructive help, 17 delivery/runtime sentinels plus source, known object removal, repeat clean, and junction rejection passed; 11 actual protected output files unchanged by SHA-256.
- Additional evidence: The pre-existing private runtime INI hash is unchanged; its values were not read. Existing feature/firmware modifications remain uncommitted and were not overwritten.
- Limits/publication: NOT RUN: Keil firmware rebuild, physical SGPIO validation or exhaustive UI/DPI. User confirmed this batch's push is deferred; preserve all uncommitted feature work and local results.


## Current State

- The workspace has been scaffolded as an SGPIO-only MFC HID GUI.
- The original SGPIO tab is now `SGPIO Output`; `SGPIO Monitor` is a separate tab for validating an independent external source.
- The firmware HID dispatcher exposes core, SGPIO output (`0x74`-`0x77`), and SGPIO monitor (`0x78`-`0x7B`) commands.
- M032 and M261 firmware use Timer0-triggered PDMA writes to PA DOUT to output SGPIO on PA2/PA0/PA3, and report PA1 as optional SDATA in.
- Both monitor implementations wait for a PA5 SLOAD falling edge, then use Timer2 + PDMA channel 1 to sample PA6/PA4/PA5 at 3.2 MS/s and decode 4/8/16-slot ACT/LOCATE/FAIL data.
- Both firmware targets report version `1.1.0` and expose identical output and monitor HID payloads.

## Key Files

- PC entry point and shell:
  - `src\app.*`
  - `src\main.*`
  - `src\main_frame.*`
- SGPIO UI:
  - `src\ui\sgpio_tab.*`
  - `src\ui\sgpio_monitor_tab.*`
- HID command service:
  - `src\core\bridge_service.*`
  - `src\core\bridge_commands.h`
- Firmware SGPIO:
  - `demo_code\M031BSP_USB_HID_SGPIO\SampleCode\Template\m031_bridge_sgpio.*`
  - `demo_code\M031BSP_USB_HID_SGPIO\SampleCode\Template\m031_bridge_sgpio_monitor.*`
  - `demo_code\M031BSP_USB_HID_SGPIO\SampleCode\Template\hid_tool_api.c`
  - `demo_code\M031BSP_USB_HID_SGPIO\SampleCode\Template\bridge_protocol.h`
  - `demo_code\M261BSP_USB_HID_SGPIO\SampleCode\Template\m261_bridge_sgpio.*`
  - `demo_code\M261BSP_USB_HID_SGPIO\SampleCode\Template\m261_bridge_sgpio_monitor.*`

## Hardware Notes

- M032/M261 PA2 -> target SCLK
- M032/M261 PA0 -> target SDATA out
- M032/M261 PA3 -> target SLOAD
- M032/M261 GND -> target GND
- M032/M261 PA1 is optional SDATA in status only; capture words are not implemented.
- External source SCLOCK -> M032/M261 PA6
- External source SDATA -> M032/M261 PA4
- External source SLOAD -> M032/M261 PA5
- External source GND -> M032/M261 GND

## Remaining Hardware Check

After building/flashing firmware, verify on a logic analyzer:

- SCLK frequency roughly follows the GUI clock setting.
- Frame starts with five SLOAD low clocks, then one SLOAD high restart clock.
- Slot bits are ACT, LOCATE, FAIL in LSB slot order.
- SLOAD returns idle high after the payload.
- SGPIO clock should clamp to 100 kHz through 400 kHz.
- Monitor should reach Complete with no error flags at 100 kHz and 400 kHz for 4/8/16 slots.
- Auto slot detection and fixed Expected Slots should produce the same masks.
- Repeat output and monitor checks independently on both M032 and M261 firmware `1.1.0`.
