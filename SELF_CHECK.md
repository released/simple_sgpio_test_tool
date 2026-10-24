# SELF_CHECK

## Verified x64 EXE slimming 2026-10-25

- PASS fresh comparable Release baseline 2849792 bytes (original delivery
  2849792), formal 760320. Required delivery 2849792 -> 760320 bytes:
  GUI EXE; optional/generated user settings are not bundled by this check.
- Baseline SHA: F98A67F9611A7D1941A0E0E7FAE48689E7471635F587F46EC26A21A416FF7854.
- Formal SHA: 6ED7B65BA32A2980C63C0F38936A91F913B141B9E33D5603E59175613F32704B.
- PASS canonical build_mfc.ps1 -Configuration Release -Platform x64
  -CloseRunningTool $false; isolated MSBuild Debug with version prebuild disabled.
  Zero reported compiler/linker warnings/errors; SDK informational defaults retained.
- PASS authorized isolated actual-EXE tests: hash-matched fresh fixtures, identical
  child-control class counts, owned common-dialog open/cancel, assertion-free Debug
  startup/close and formal exit 0. No hardware, flash or clipboard actions.
- Impact: unused x64 MFC registration only; /O2/static runtime/security PE 0x8160,
  Win32 definitions, protocol/algorithm/firmware sources retained. Measure startup/control/UI paths, not bus throughput.
- PASS paired gate: baseline warmups/calibration, five alternating AB/BA pairs,
  no concurrent compilation. Before-candidate delta max(floor, 10% baseline median,
  3*MAD); memory 5%; floors startup 25ms/UI 2ms/CPU 16ms/memory 1MiB.
  All medians remain within baseline-derived observed variability:
  - StartupMs: 458.7140 -> 471.2129, limit 503.7140.
  - PeakBytes: 28094464.0000 -> 27656192.0000, limit 29499802.0000.
  - CpuMs: 390.6250 -> 375.0000, limit 484.6250.
  - WorkflowMs: 15.2653 -> 15.8244, limit 26.2653.
- Raw samples/limits: docs/verification/exe-slimming-20261025.json.
- PASS actual clean_mfc.bat: 30 compiler files removed, 11 protected actual
  files SHA unchanged; 17 nested fixture files/source, help/idempotence PASS.
  Unchanged earlier junction-rejection evidence is reused, not rerun. Personal
  root INI hashes match the pre-build manifest without inspecting private values.
- NOT RUN: physical devices/protocol, unrelated firmware, visual/DPI, clean-PC,
  Win32 builds. No new driver/runtime, feature removal or global /O1 preset.

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


## Static Checks

- Confirm no user-facing Pico board text remains in the SGPIO GUI.
- Confirm M032 Output pin labels are PA2/PA0/PA3/PA1 and Monitor pin labels are PA6/PA4/PA5.
- Confirm firmware `Get Info` strings are `m032-sgpio-bridge/1.1.0` and `m261-sgpio-bridge/1.1.0`.
- Confirm both Keil projects include their output and monitor bridge sources.
- Confirm Keil project includes `pdma.c`.
- Confirm Keil project includes `fmc.c` for the Return Bootloader CRC erase path.
- Confirm Keil project does not include the removed I2C bridge source or `i2c.c`.
- Confirm SGPIO clock is clamped to 100 kHz through 400 kHz.
- Confirm GitHub publish scan includes the user-facing exe/ini/test_log artifacts and has no local absolute paths in publishable files.
- Confirm GUI DPI support covers helper APIs, `WM_DPICHANGED`, scaled metrics, measured button text, SGPIO tab scrollbar, and `ScrollWindowEx` redraw cleanup.
- Confirm SGPIO `Fixed Pins` value uses single-line text with measured width so it does not wrap under the next control row.
- Confirm Monitor uses Timer2 + PDMA channel 1 at 3.2 MS/s and accepts Auto/4/8/16 slots.
- Confirm Output and Monitor pin owners cannot be active simultaneously in the GUI.

## Build Checks

- PC GUI build: PASS, `scripts\build_mfc.ps1 -Configuration Release -Platform x64`, output `build\SgpioHidTool.exe`, version `1.0.0.011`, 0 warnings, 0 errors.
- Release EXE sensitive-path scan: PASS; no local drive path, workspace name, or user marker remains in ASCII/UTF-16 strings after disabling Release debug information.
- Clean script check: PASS, `scripts\clean_mfc.bat` removed PC intermediates and preserved `build\SgpioHidTool.exe`, `build\sgpio_hid_tool.ini`, and `build\test_log`.
- Publish scan: PASS, checked common Windows absolute-path and user-name patterns with `.gitignore` applied; no publishable-file matches outside intentionally ignored local artifacts.
- DPI visual matrix: 125% PASS for Output/Monitor tab switching and monitor control layout; 100% and 150% NOT RUN. See `docs\MFC_DPI_SCALING_GUIDE.md`.
- M032 firmware syntax check: PASS with `arm-none-eabi-gcc -mcpu=cortex-m0 -mthumb -std=gnu99 -fsyntax-only` for `m031_bridge_sgpio_monitor.c`, `hid_tool_api.c`, and `main.c`, 0 diagnostics.
- M261 firmware syntax check: PASS with `arm-none-eabi-gcc -mcpu=cortex-m23 -mthumb -std=gnu90 -Werror=declaration-after-statement -fsyntax-only` for `m261_bridge_sgpio_monitor.c`, `hid_tool_api.c`, and `main.c`. The changed monitor and HID files produced 0 diagnostics; `main.c` retained only pre-existing printf format warnings.
- Firmware GCC full link: PASS against the M032 128 KB Flash / 16 KB SRAM linker script with all Keil-project application and driver sources. Size: text 42932, data 2800, bss 5824 bytes; data+bss uses 8624 of 16384 SRAM bytes before the configured heap/stack reserve.
- M032 firmware Keil build: BLOCKED by local FlexNet configuration. UV4 reached Arm Compiler 6.22, but the configured license server was unavailable (`FlexNet -96,7`); target was not created.
- M261 firmware Keil build: PASS with Arm Compiler 6.22, including link and CRC binary post-build. Result: 0 errors, 0 warnings; Code 23152, RO-data 1412, RW-data 284, ZI-data 7848 bytes. Boot/app merge was skipped because the M261 bootloader hex was not present.

## Hardware Checks

- Use GUI `Get Info`, `Enable`, `Apply Now`, `Get Status`, and `Disable`.
- Use a logic analyzer on PA2/PA0/PA3 to verify SGPIO frame timing.
- Use an independent SGPIO source on PA6/PA4/PA5 to verify Monitor trigger, clock measurement, and mask decode.
- Test against:
  - `M031BSP_Software_SGPIO`
  - `M031BSP_SPI_SGPIO`

## Known Limits

- M032 and M261 firmware currently report PA1 level as SDATA in status but do not capture SDATA-in frame words.
- SGPIO is generated by Timer0-triggered PDMA writes to PA DOUT; final timing should still be checked with a logic analyzer on each EVB.
- Monitor returns decoded protocol results, not raw waveform samples.
- Monitor firmware and GUI require hardware validation at 100 kHz and 400 kHz for 4/8/16-slot external sources.
