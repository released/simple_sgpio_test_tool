#ifndef __M261_BRIDGE_SGPIO_MONITOR_H__
#define __M261_BRIDGE_SGPIO_MONITOR_H__

#include <stdint.h>

#define M261_BRIDGE_SGPIO_MONITOR_STATE_IDLE       0u
#define M261_BRIDGE_SGPIO_MONITOR_STATE_ARMED      1u
#define M261_BRIDGE_SGPIO_MONITOR_STATE_CAPTURING  2u
#define M261_BRIDGE_SGPIO_MONITOR_STATE_COMPLETE   3u
#define M261_BRIDGE_SGPIO_MONITOR_STATE_TIMEOUT    4u
#define M261_BRIDGE_SGPIO_MONITOR_STATE_ERROR      5u

#define M261_BRIDGE_SGPIO_MONITOR_ERROR_NO_TRIGGER      0x0001u
#define M261_BRIDGE_SGPIO_MONITOR_ERROR_NO_CLOCK        0x0002u
#define M261_BRIDGE_SGPIO_MONITOR_ERROR_CLOCK_RANGE     0x0004u
#define M261_BRIDGE_SGPIO_MONITOR_ERROR_SYNC            0x0008u
#define M261_BRIDGE_SGPIO_MONITOR_ERROR_INCOMPLETE      0x0010u
#define M261_BRIDGE_SGPIO_MONITOR_ERROR_CAPTURE_TIMEOUT 0x0020u

#define M261_BRIDGE_SGPIO_MONITOR_SAMPLE_RATE_HZ 3200000u
#define M261_BRIDGE_SGPIO_MONITOR_CLOCK_MIN_HZ   100000u
#define M261_BRIDGE_SGPIO_MONITOR_CLOCK_MAX_HZ   400000u

typedef struct
{
    uint8_t state;
    uint8_t expected_slot_count;
    uint8_t detected_slot_count;
    uint8_t sload_raw;
    uint16_t error_flags;
    uint16_t sample_count;
    uint16_t clock_rising_count;
    uint16_t decoded_bit_count;
    uint32_t sample_rate_hz;
    uint32_t clock_hz;
    uint32_t clock_high_ns;
    uint32_t clock_low_ns;
    uint16_t act_mask;
    uint16_t locate_mask;
    uint16_t fail_mask;
} M261_BRIDGE_SGPIO_MONITOR_STATUS_T;

uint8_t M261BridgeSgpioMonitor_Config(uint8_t expected_slot_count, uint16_t trigger_timeout_ms);
uint8_t M261BridgeSgpioMonitor_Arm(void);
uint8_t M261BridgeSgpioMonitor_Stop(void);
uint8_t M261BridgeSgpioMonitor_GetStatus(M261_BRIDGE_SGPIO_MONITOR_STATUS_T *status);
void M261BridgeSgpioMonitor_Process(void);

#endif /* __M261_BRIDGE_SGPIO_MONITOR_H__ */
