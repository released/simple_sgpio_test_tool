# SGPIO Bridge Notes

## M032/M261 Output Pin Map

| Pin | Direction | SGPIO signal |
| --- | --- | --- |
| PA2 | Output | SCLK |
| PA0 | Output | SDATA out |
| PA3 | Output | SLOAD |
| PA1 | Input | SDATA in, optional status level |

## M032/M261 Monitor Pin Map

| Pin | Direction | SGPIO signal |
| --- | --- | --- |
| PA6 | Input | External SCLOCK |
| PA4 | Input | External SDATA |
| PA5 | Input | External SLOAD trigger/frame |

The monitor pins are separate from the output pins. Connect the external source ground to the bridge ground and use 3.3 V-compatible logic. The GUI allows only one of `SGPIO Output` or `SGPIO Monitor` to be active at a time.

## HID Command Payloads

All HID bridge reports use the shared header:

```text
byte 0      magic 0xA5
byte 1      command
byte 2      sequence
byte 3      status in response
byte 4..5   payload length, little-endian
byte 6..63  payload
```

### `0x74` SGPIO_CONFIG

Request and response payload:

```text
byte 0      slot_count
byte 1..4   clock_hz, little-endian uint32
```

### `0x75` SGPIO_APPLY

Request and response payload:

```text
byte 0      enable
byte 1      periodic
byte 2..3   interval_ms, little-endian uint16
byte 4      SLOAD raw L0..L3
byte 5..6   ACT mask, little-endian uint16
byte 7..8   LOCATE mask, little-endian uint16
byte 9..10  FAIL mask, little-endian uint16
```

### `0x76` SGPIO_STATUS

Request payload is empty. Response payload:

```text
byte 0      enabled
byte 1      periodic
byte 2      slot_count
byte 3      SLOAD raw L0..L3
byte 4..7   clock_hz, little-endian uint32
byte 8..9   interval_ms, little-endian uint16
byte 10..11 ACT mask, little-endian uint16
byte 12..13 LOCATE mask, little-endian uint16
byte 14..15 FAIL mask, little-endian uint16
byte 16     SCLK pin id, PA2
byte 17     SDATA out pin id, PA0
byte 18     SLOAD pin id, PA3
byte 19     SDATA in pin id, PA1
byte 20     SDATA in level
byte 21     SDATA in capture valid, currently 0
byte 22     SDATA in capture word count, currently 0
byte 23     SDATA in capture bit count, currently 0
```

### `0x77` SGPIO_OFF

Request payload is empty. Response payload:

```text
byte 0      0
```

### `0x78` SGPIO_MONITOR_CONFIG

Request and response payload:

```text
byte 0      expected slot count: 0=Auto, 4, 8, or 16
byte 1..2   SLOAD trigger timeout in ms, little-endian uint16
```

Firmware clamps trigger timeout to 100 through 30000 ms. Config returns `BUSY` while the monitor is already Armed or Capturing.

### `0x79` SGPIO_MONITOR_ARM

Request payload is empty. Response payload:

```text
byte 0      monitor state, 1=Armed
byte 1      SCLOCK pin id, PA6
byte 2      SDATA pin id, PA4
byte 3      SLOAD pin id, PA5
```

Arm preconfigures Timer2 and PDMA channel 1, then enables the PA5 falling-edge interrupt. A second Arm while capture is active returns `BUSY`.

### `0x7A` SGPIO_MONITOR_STATUS

Request payload is empty. Response payload:

```text
byte 0      state: 0=Idle, 1=Armed, 2=Capturing,
                      3=Complete, 4=Timeout, 5=Error
byte 1      expected slot count: 0=Auto, 4, 8, or 16
byte 2      detected slot count: 0, 4, 8, or 16
byte 3      captured SLOAD L0..L3 raw value
byte 4..5   error flags, little-endian uint16
byte 6..7   captured sample count, little-endian uint16
byte 8..9   SCLOCK rising-edge count, little-endian uint16
byte 10..11 decoded slot-data bit count, little-endian uint16
byte 12..15 actual sample rate in samples/s, little-endian uint32
byte 16..19 measured SCLOCK frequency in Hz, little-endian uint32
byte 20..23 average SCLOCK high time in ns, little-endian uint32
byte 24..27 average SCLOCK low time in ns, little-endian uint32
byte 28..29 ACT mask, little-endian uint16
byte 30..31 LOCATE mask, little-endian uint16
byte 32..33 FAIL mask, little-endian uint16
byte 34     SCLOCK pin id, PA6
byte 35     SDATA pin id, PA4
byte 36     SLOAD pin id, PA5
```

Error flags:

| Bit | Value | Meaning |
| --- | --- | --- |
| 0 | `0x0001` | No SLOAD falling-edge trigger before timeout |
| 1 | `0x0002` | No valid SCLOCK measurements |
| 2 | `0x0004` | Measured SCLOCK is outside 100-400 kHz |
| 3 | `0x0008` | SLOAD sync pattern is unexpected |
| 4 | `0x0010` | Slot data is incomplete or slot count cannot be detected |
| 5 | `0x0020` | PDMA capture did not complete before capture timeout |

### `0x7B` SGPIO_MONITOR_STOP

Request payload is empty. Response payload:

```text
byte 0      monitor state, 0=Idle
```

## Frame Format

The M032 and M261 firmware targets emit SGPIO with Timer0-triggered PDMA writes to `PA->DOUT`, with one PDMA word per half SCLK period.

```text
5 clocks    SLOAD=0, SDATA=0 sync
1 clock     SLOAD=1, SDATA=0 restart marker
N clocks    ACT, LOCATE, FAIL bits per slot, LSB slot order
pad clocks  SLOAD=1, SDATA=0 until the frame reaches a 16-clock boundary
```

For slot counts below 4, the bridge emits 4 slots so the target receives a stable minimum payload.

Supported SGPIO clock range is 100 kHz to 400 kHz. Timer0 runs at twice the selected SGPIO clock because each SGPIO clock uses one low half-period and one high half-period table entry.

## Monitor Capture And Decode

The independent monitor waits for the external SLOAD falling edge, then starts Timer2 at 3.2 MHz. M261 uses its 48 MHz PCLK1 as the Timer2 source for an exact divide-by-15 sample interval. Each Timer2 timeout requests one 16-bit PDMA transfer from `PA->PIN`, so SCLOCK, SDATA, and SLOAD are sampled at the same instant. The fixed capture buffer contains 2304 samples, or 720 us at the configured sample rate.

The decoder locates the five SLOAD-low sync clocks and the following SLOAD-high restart clock, samples SDATA on SCLOCK rising edges, and maps each three-bit group to ACT/LOCATE/FAIL for one slot. Auto slot detection identifies the SLOAD-high padding boundary at data bit 12, 24, or 48. Firmware returns the decoded summary over HID; raw waveform samples remain local and are not transported to the PC.

Monitor protocol support is present in both M032 and M261 bridge firmware version 1.1.0. Both targets use the same pins, HID commands, capture rate, and decoded response format.
