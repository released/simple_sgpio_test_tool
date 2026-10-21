/******************************************************************************
 * @file     m261_bridge_sgpio_monitor.c
 * @brief    Timer-triggered PDMA capture and decode for an external SGPIO source.
 *****************************************************************************/

#include <string.h>

#include "NuMicro.h"
#include "m261_bridge_sgpio_monitor.h"
#include "misc_config.h"

extern uint32_t get_tick(void);

#define M261_SGPIO_MONITOR_SDATA_PIN          4u
#define M261_SGPIO_MONITOR_SLOAD_PIN          5u
#define M261_SGPIO_MONITOR_SCLOCK_PIN         6u
#define M261_SGPIO_MONITOR_PIN_MASK           (BIT4 | BIT5 | BIT6)
#define M261_SGPIO_MONITOR_PDMA_CH            1u
#define M261_SGPIO_MONITOR_PDMA_MODULE        PDMA0_MODULE
#define M261_SGPIO_MONITOR_PDMA               PDMA0
#define M261_SGPIO_MONITOR_TIMER              TIMER2
#define M261_SGPIO_MONITOR_TIMER_MODULE       TMR2_MODULE
#define M261_SGPIO_MONITOR_TIMER_CLKSEL       CLK_CLKSEL1_TMR2SEL_PCLK1
#define M261_SGPIO_MONITOR_PDMA_REQ           PDMA_TMR2
#define M261_SGPIO_MONITOR_SAMPLE_MAX         2304u
#define M261_SGPIO_MONITOR_TRIGGER_TIMEOUT_MS 3000u
#define M261_SGPIO_MONITOR_CAPTURE_TIMEOUT_MS 10u
#define M261_SGPIO_MONITOR_PERIOD_MAX_SAMPLES 64u
#define M261_SGPIO_MONITOR_LEVEL_MAX_SAMPLES  40u

typedef struct
{
    uint8_t initialized;
    volatile uint8_t state;
    uint8_t expected_slot_count;
    uint16_t trigger_timeout_ms;
    uint32_t arm_tick_ms;
    uint32_t capture_tick_ms;
    M261_BRIDGE_SGPIO_MONITOR_STATUS_T result;
} M261_BRIDGE_SGPIO_MONITOR_STATE_T;

static M261_BRIDGE_SGPIO_MONITOR_STATE_T g_sgpio_monitor =
{
    0u,
    M261_BRIDGE_SGPIO_MONITOR_STATE_IDLE,
    0u,
    M261_SGPIO_MONITOR_TRIGGER_TIMEOUT_MS,
    0u,
    0u,
    {
        M261_BRIDGE_SGPIO_MONITOR_STATE_IDLE,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        M261_BRIDGE_SGPIO_MONITOR_SAMPLE_RATE_HZ,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u
    }
};

static volatile uint16_t g_sgpio_monitor_samples[M261_SGPIO_MONITOR_SAMPLE_MAX];

static uint8_t BridgeSgpioMonitor_PinLevel(uint16_t sample, uint8_t pin)
{
    return (uint8_t)((sample >> pin) & 0x01u);
}

static uint8_t BridgeSgpioMonitor_ValidSlotCount(uint8_t slot_count)
{
    if ((slot_count == 0u) || (slot_count == 4u) ||
        (slot_count == 8u) || (slot_count == 16u))
    {
        return 1u;
    }
    return 0u;
}

static void BridgeSgpioMonitor_ClearResult(void)
{
    memset(&g_sgpio_monitor.result, 0, sizeof(g_sgpio_monitor.result));
    g_sgpio_monitor.result.state = g_sgpio_monitor.state;
    g_sgpio_monitor.result.expected_slot_count = g_sgpio_monitor.expected_slot_count;
    g_sgpio_monitor.result.sample_rate_hz = M261_BRIDGE_SGPIO_MONITOR_SAMPLE_RATE_HZ;
}

static void BridgeSgpioMonitor_StopHardware(void)
{
    TIMER_Stop(M261_SGPIO_MONITOR_TIMER);
    TIMER_SetTriggerTarget(M261_SGPIO_MONITOR_TIMER, 0u);
    TIMER_ClearIntFlag(M261_SGPIO_MONITOR_TIMER);
    GPIO_DisableInt(PA, M261_SGPIO_MONITOR_SLOAD_PIN);
    GPIO_CLR_INT_FLAG(PA, BIT5);
    PDMA_PAUSE(M261_SGPIO_MONITOR_PDMA, M261_SGPIO_MONITOR_PDMA_CH);
    PDMA_RESET(M261_SGPIO_MONITOR_PDMA, M261_SGPIO_MONITOR_PDMA_CH);
    PDMA_CLR_TD_FLAG(M261_SGPIO_MONITOR_PDMA, (1ul << M261_SGPIO_MONITOR_PDMA_CH));
}

static void BridgeSgpioMonitor_EnsureReady(void)
{
    if (g_sgpio_monitor.initialized != 0u)
    {
        return;
    }

    SYS_UnlockReg();
    CLK_EnableModuleClock(M261_SGPIO_MONITOR_PDMA_MODULE);
    CLK_EnableModuleClock(M261_SGPIO_MONITOR_TIMER_MODULE);
    CLK_SetModuleClock(M261_SGPIO_MONITOR_TIMER_MODULE, M261_SGPIO_MONITOR_TIMER_CLKSEL, 0u);
    SYS->GPA_MFPL = (SYS->GPA_MFPL &
                    ~(SYS_GPA_MFPL_PA4MFP_Msk |
                      SYS_GPA_MFPL_PA5MFP_Msk |
                      SYS_GPA_MFPL_PA6MFP_Msk)) |
                    (SYS_GPA_MFPL_PA4MFP_GPIO |
                     SYS_GPA_MFPL_PA5MFP_GPIO |
                     SYS_GPA_MFPL_PA6MFP_GPIO);
    SYS_LockReg();

    GPIO_SetMode(PA, M261_SGPIO_MONITOR_PIN_MASK, GPIO_MODE_INPUT);
    GPIO_DisableInt(PA, M261_SGPIO_MONITOR_SLOAD_PIN);
    GPIO_CLR_INT_FLAG(PA, BIT5);
    NVIC_ClearPendingIRQ(GPA_IRQn);
    NVIC_EnableIRQ(GPA_IRQn);

    g_sgpio_monitor.initialized = 1u;
}

static void BridgeSgpioMonitor_PrepareHardware(void)
{
    uint32_t actual_sample_rate;

    BridgeSgpioMonitor_StopHardware();

    PDMA_Open(M261_SGPIO_MONITOR_PDMA, (1ul << M261_SGPIO_MONITOR_PDMA_CH));
    PDMA_SetTransferCnt(M261_SGPIO_MONITOR_PDMA, M261_SGPIO_MONITOR_PDMA_CH,
                        PDMA_WIDTH_16, M261_SGPIO_MONITOR_SAMPLE_MAX);
    PDMA_SetTransferAddr(M261_SGPIO_MONITOR_PDMA, M261_SGPIO_MONITOR_PDMA_CH,
                         (uint32_t)&PA->PIN, PDMA_SAR_FIX,
                         (uint32_t)&g_sgpio_monitor_samples[0], PDMA_DAR_INC);
    PDMA_SetTransferMode(M261_SGPIO_MONITOR_PDMA, M261_SGPIO_MONITOR_PDMA_CH,
                         M261_SGPIO_MONITOR_PDMA_REQ, 0u, 0u);
    PDMA_SetBurstType(M261_SGPIO_MONITOR_PDMA, M261_SGPIO_MONITOR_PDMA_CH,
                      PDMA_REQ_SINGLE, PDMA_BURST_1);

    actual_sample_rate = TIMER_Open(M261_SGPIO_MONITOR_TIMER,
                                    TIMER_PERIODIC_MODE,
                                    M261_BRIDGE_SGPIO_MONITOR_SAMPLE_RATE_HZ);
    TIMER_SetTriggerSource(M261_SGPIO_MONITOR_TIMER, TIMER_TRGSRC_TIMEOUT_EVENT);
    TIMER_SetTriggerTarget(M261_SGPIO_MONITOR_TIMER, TIMER_TRG_TO_PDMA);
    TIMER_ClearIntFlag(M261_SGPIO_MONITOR_TIMER);
    g_sgpio_monitor.result.sample_rate_hz = actual_sample_rate;
}

static uint16_t BridgeSgpioMonitor_TransferredCount(void)
{
    uint32_t control;
    uint32_t remaining;

    control = M261_SGPIO_MONITOR_PDMA->DSCT[M261_SGPIO_MONITOR_PDMA_CH].CTL;
    if ((control & PDMA_DSCT_CTL_OPMODE_Msk) == PDMA_OP_STOP)
    {
        return M261_SGPIO_MONITOR_SAMPLE_MAX;
    }

    remaining = ((control & PDMA_DSCT_CTL_TXCNT_Msk) >> PDMA_DSCT_CTL_TXCNT_Pos) + 1u;
    if (remaining >= M261_SGPIO_MONITOR_SAMPLE_MAX)
    {
        return 0u;
    }
    return (uint16_t)(M261_SGPIO_MONITOR_SAMPLE_MAX - remaining);
}

static void BridgeSgpioMonitor_Decode(uint16_t sample_count)
{
    uint16_t i;
    uint16_t rise_count;
    uint16_t sync_rise;
    uint16_t sync_low_count;
    uint16_t zero_run;
    uint16_t previous_rise_index;
    uint16_t period_count;
    uint32_t period_sum;
    uint32_t high_sum;
    uint32_t low_sum;
    uint16_t high_count;
    uint16_t low_count;
    uint16_t transition_index;
    uint8_t previous_clock;
    uint8_t current_clock;
    uint8_t sload;
    uint16_t duration;
    uint16_t data_index;
    uint16_t available_bits;
    uint16_t padding_start;
    uint16_t high_run_start;
    uint16_t high_run;
    uint16_t first_high_after_l3;
    uint8_t detected_slots;
    uint16_t required_bits;
    uint16_t act_mask;
    uint16_t locate_mask;
    uint16_t fail_mask;
    uint8_t sload_raw;
    uint16_t slot;
    uint8_t bit_select;
    uint16_t slot_mask;
    uint64_t value64;

    g_sgpio_monitor.result.sample_count = sample_count;
    g_sgpio_monitor.result.error_flags = 0u;
    g_sgpio_monitor.result.detected_slot_count = 0u;
    g_sgpio_monitor.result.sload_raw = 0u;
    g_sgpio_monitor.result.clock_rising_count = 0u;
    g_sgpio_monitor.result.decoded_bit_count = 0u;
    g_sgpio_monitor.result.clock_hz = 0u;
    g_sgpio_monitor.result.clock_high_ns = 0u;
    g_sgpio_monitor.result.clock_low_ns = 0u;
    g_sgpio_monitor.result.act_mask = 0u;
    g_sgpio_monitor.result.locate_mask = 0u;
    g_sgpio_monitor.result.fail_mask = 0u;

    if (sample_count < 2u)
    {
        g_sgpio_monitor.result.error_flags = M261_BRIDGE_SGPIO_MONITOR_ERROR_NO_CLOCK;
        g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_ERROR;
        g_sgpio_monitor.result.state = g_sgpio_monitor.state;
        return;
    }

    rise_count = 0u;
    sync_rise = 0xFFFFu;
    sync_low_count = 0u;
    zero_run = 0u;
    previous_rise_index = 0u;
    period_count = 0u;
    period_sum = 0u;
    high_sum = 0u;
    low_sum = 0u;
    high_count = 0u;
    low_count = 0u;
    transition_index = 0u;
    previous_clock = BridgeSgpioMonitor_PinLevel(g_sgpio_monitor_samples[0],
                                                  M261_SGPIO_MONITOR_SCLOCK_PIN);

    for (i = 1u; i < sample_count; ++i)
    {
        current_clock = BridgeSgpioMonitor_PinLevel(g_sgpio_monitor_samples[i],
                                                     M261_SGPIO_MONITOR_SCLOCK_PIN);
        if (current_clock == previous_clock)
        {
            continue;
        }

        duration = (uint16_t)(i - transition_index);
        if ((transition_index != 0u) && (duration <= M261_SGPIO_MONITOR_LEVEL_MAX_SAMPLES))
        {
            if (previous_clock != 0u)
            {
                high_sum += duration;
                high_count = (uint16_t)(high_count + 1u);
            }
            else
            {
                low_sum += duration;
                low_count = (uint16_t)(low_count + 1u);
            }
        }
        transition_index = i;

        if ((previous_clock == 0u) && (current_clock != 0u))
        {
            if (rise_count > 0u)
            {
                duration = (uint16_t)(i - previous_rise_index);
                if ((duration >= 4u) && (duration <= M261_SGPIO_MONITOR_PERIOD_MAX_SAMPLES))
                {
                    period_sum += duration;
                    period_count = (uint16_t)(period_count + 1u);
                }
            }
            previous_rise_index = i;
            sload = BridgeSgpioMonitor_PinLevel(g_sgpio_monitor_samples[i],
                                                M261_SGPIO_MONITOR_SLOAD_PIN);
            if (sync_rise == 0xFFFFu)
            {
                if (sload == 0u)
                {
                    zero_run = (uint16_t)(zero_run + 1u);
                }
                else
                {
                    if (zero_run >= 4u)
                    {
                        sync_rise = rise_count;
                        sync_low_count = zero_run;
                    }
                    zero_run = 0u;
                }
            }
            rise_count = (uint16_t)(rise_count + 1u);
        }
        previous_clock = current_clock;
    }

    g_sgpio_monitor.result.clock_rising_count = rise_count;
    if ((rise_count < 6u) || (period_count == 0u) || (period_sum == 0u))
    {
        g_sgpio_monitor.result.error_flags |= M261_BRIDGE_SGPIO_MONITOR_ERROR_NO_CLOCK;
    }
    else
    {
        value64 = (uint64_t)g_sgpio_monitor.result.sample_rate_hz * (uint64_t)period_count;
        g_sgpio_monitor.result.clock_hz = (uint32_t)((value64 + (period_sum / 2u)) / period_sum);
        if ((g_sgpio_monitor.result.clock_hz < M261_BRIDGE_SGPIO_MONITOR_CLOCK_MIN_HZ) ||
            (g_sgpio_monitor.result.clock_hz > M261_BRIDGE_SGPIO_MONITOR_CLOCK_MAX_HZ))
        {
            g_sgpio_monitor.result.error_flags |= M261_BRIDGE_SGPIO_MONITOR_ERROR_CLOCK_RANGE;
        }
    }

    if ((high_count > 0u) && (g_sgpio_monitor.result.sample_rate_hz > 0u))
    {
        value64 = (uint64_t)high_sum * (uint64_t)1000000000u;
        value64 /= (uint64_t)high_count * g_sgpio_monitor.result.sample_rate_hz;
        g_sgpio_monitor.result.clock_high_ns = (uint32_t)value64;
    }
    if ((low_count > 0u) && (g_sgpio_monitor.result.sample_rate_hz > 0u))
    {
        value64 = (uint64_t)low_sum * (uint64_t)1000000000u;
        value64 /= (uint64_t)low_count * g_sgpio_monitor.result.sample_rate_hz;
        g_sgpio_monitor.result.clock_low_ns = (uint32_t)value64;
    }

    if (sync_rise == 0xFFFFu)
    {
        g_sgpio_monitor.result.error_flags |= M261_BRIDGE_SGPIO_MONITOR_ERROR_SYNC;
        g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_ERROR;
        g_sgpio_monitor.result.state = g_sgpio_monitor.state;
        return;
    }
    if (sync_low_count != 5u)
    {
        g_sgpio_monitor.result.error_flags |= M261_BRIDGE_SGPIO_MONITOR_ERROR_SYNC;
    }

    available_bits = (uint16_t)(rise_count - sync_rise - 1u);
    padding_start = 0xFFFFu;
    high_run_start = 0u;
    high_run = 0u;
    first_high_after_l3 = 0xFFFFu;
    data_index = 0u;
    previous_clock = BridgeSgpioMonitor_PinLevel(g_sgpio_monitor_samples[0],
                                                  M261_SGPIO_MONITOR_SCLOCK_PIN);
    rise_count = 0u;
    act_mask = 0u;
    locate_mask = 0u;
    fail_mask = 0u;
    sload_raw = 0u;

    for (i = 1u; i < sample_count; ++i)
    {
        current_clock = BridgeSgpioMonitor_PinLevel(g_sgpio_monitor_samples[i],
                                                     M261_SGPIO_MONITOR_SCLOCK_PIN);
        if ((previous_clock == 0u) && (current_clock != 0u))
        {
            if (rise_count > sync_rise)
            {
                data_index = (uint16_t)(rise_count - sync_rise - 1u);
                sload = BridgeSgpioMonitor_PinLevel(g_sgpio_monitor_samples[i],
                                                    M261_SGPIO_MONITOR_SLOAD_PIN);
                if (data_index < 4u)
                {
                    sload_raw |= (uint8_t)(sload << data_index);
                }
                else if ((sload != 0u) && (first_high_after_l3 == 0xFFFFu))
                {
                    first_high_after_l3 = data_index;
                }

                if (data_index < 48u)
                {
                    slot = (uint16_t)(data_index / 3u);
                    bit_select = (uint8_t)(data_index % 3u);
                    slot_mask = (uint16_t)(1u << slot);
                    if (BridgeSgpioMonitor_PinLevel(g_sgpio_monitor_samples[i],
                                                    M261_SGPIO_MONITOR_SDATA_PIN) != 0u)
                    {
                        if (bit_select == 0u)
                        {
                            act_mask |= slot_mask;
                        }
                        else if (bit_select == 1u)
                        {
                            locate_mask |= slot_mask;
                        }
                        else
                        {
                            fail_mask |= slot_mask;
                        }
                    }
                }

                if (data_index >= 12u)
                {
                    if (sload != 0u)
                    {
                        if (high_run == 0u)
                        {
                            high_run_start = data_index;
                        }
                        high_run = (uint16_t)(high_run + 1u);
                        if ((high_run >= 2u) &&
                            ((high_run_start == 12u) ||
                             (high_run_start == 24u) ||
                             (high_run_start == 48u)) &&
                            (padding_start == 0xFFFFu))
                        {
                            padding_start = high_run_start;
                        }
                    }
                    else
                    {
                        high_run = 0u;
                    }
                }
            }
            rise_count = (uint16_t)(rise_count + 1u);
        }
        previous_clock = current_clock;
    }

    detected_slots = g_sgpio_monitor.expected_slot_count;
    if (detected_slots == 0u)
    {
        if (padding_start != 0xFFFFu)
        {
            detected_slots = (uint8_t)(padding_start / 3u);
        }
        else if (available_bits >= 48u)
        {
            detected_slots = 16u;
            g_sgpio_monitor.result.error_flags |= M261_BRIDGE_SGPIO_MONITOR_ERROR_INCOMPLETE;
        }
    }

    if (BridgeSgpioMonitor_ValidSlotCount(detected_slots) == 0u || detected_slots == 0u)
    {
        g_sgpio_monitor.result.error_flags |= M261_BRIDGE_SGPIO_MONITOR_ERROR_INCOMPLETE;
        g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_ERROR;
        g_sgpio_monitor.result.sload_raw = sload_raw;
        g_sgpio_monitor.result.state = g_sgpio_monitor.state;
        return;
    }

    required_bits = (uint16_t)detected_slots * 3u;
    if (available_bits < required_bits)
    {
        g_sgpio_monitor.result.error_flags |= M261_BRIDGE_SGPIO_MONITOR_ERROR_INCOMPLETE;
    }
    if ((first_high_after_l3 != 0xFFFFu) && (first_high_after_l3 < required_bits))
    {
        g_sgpio_monitor.result.error_flags |= M261_BRIDGE_SGPIO_MONITOR_ERROR_SYNC;
    }

    slot_mask = (detected_slots >= 16u) ? 0xFFFFu : (uint16_t)((1u << detected_slots) - 1u);
    g_sgpio_monitor.result.detected_slot_count = detected_slots;
    g_sgpio_monitor.result.sload_raw = sload_raw;
    g_sgpio_monitor.result.decoded_bit_count = required_bits;
    g_sgpio_monitor.result.act_mask = (uint16_t)(act_mask & slot_mask);
    g_sgpio_monitor.result.locate_mask = (uint16_t)(locate_mask & slot_mask);
    g_sgpio_monitor.result.fail_mask = (uint16_t)(fail_mask & slot_mask);

    if ((g_sgpio_monitor.result.error_flags &
         (M261_BRIDGE_SGPIO_MONITOR_ERROR_NO_CLOCK |
          M261_BRIDGE_SGPIO_MONITOR_ERROR_INCOMPLETE)) != 0u)
    {
        g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_ERROR;
    }
    else
    {
        g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_COMPLETE;
    }
    g_sgpio_monitor.result.state = g_sgpio_monitor.state;
}

static void BridgeSgpioMonitor_StartCapture(void)
{
    if (g_sgpio_monitor.state != M261_BRIDGE_SGPIO_MONITOR_STATE_ARMED)
    {
        return;
    }

    GPIO_DisableInt(PA, M261_SGPIO_MONITOR_SLOAD_PIN);
    g_sgpio_monitor.capture_tick_ms = get_tick();
    g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_CAPTURING;
    g_sgpio_monitor.result.state = g_sgpio_monitor.state;
    TIMER_Start(M261_SGPIO_MONITOR_TIMER);
}

void GPA_IRQHandler(void)
{
    uint32_t source;

    source = PA->INTSRC;
    if ((source & BIT5) != 0u)
    {
        GPIO_CLR_INT_FLAG(PA, BIT5);
        BridgeSgpioMonitor_StartCapture();
        source &= ~BIT5;
    }
    if (source != 0u)
    {
        PA->INTSRC = source;
    }
}

uint8_t M261BridgeSgpioMonitor_Config(uint8_t expected_slot_count, uint16_t trigger_timeout_ms)
{
    if (BridgeSgpioMonitor_ValidSlotCount(expected_slot_count) == 0u)
    {
        return 0u;
    }
    if (trigger_timeout_ms < 100u)
    {
        trigger_timeout_ms = 100u;
    }
    if (trigger_timeout_ms > 30000u)
    {
        trigger_timeout_ms = 30000u;
    }

    BridgeSgpioMonitor_EnsureReady();
    if ((g_sgpio_monitor.state == M261_BRIDGE_SGPIO_MONITOR_STATE_ARMED) ||
        (g_sgpio_monitor.state == M261_BRIDGE_SGPIO_MONITOR_STATE_CAPTURING))
    {
        return 0u;
    }

    g_sgpio_monitor.expected_slot_count = expected_slot_count;
    g_sgpio_monitor.trigger_timeout_ms = trigger_timeout_ms;
    g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_IDLE;
    BridgeSgpioMonitor_ClearResult();
    return 1u;
}

uint8_t M261BridgeSgpioMonitor_Arm(void)
{
    BridgeSgpioMonitor_EnsureReady();
    if ((g_sgpio_monitor.state == M261_BRIDGE_SGPIO_MONITOR_STATE_ARMED) ||
        (g_sgpio_monitor.state == M261_BRIDGE_SGPIO_MONITOR_STATE_CAPTURING))
    {
        return 0u;
    }

    g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_ARMED;
    BridgeSgpioMonitor_ClearResult();
    BridgeSgpioMonitor_PrepareHardware();
    g_sgpio_monitor.arm_tick_ms = get_tick();
    GPIO_CLR_INT_FLAG(PA, BIT5);
    NVIC_ClearPendingIRQ(GPA_IRQn);
    GPIO_EnableInt(PA, M261_SGPIO_MONITOR_SLOAD_PIN, GPIO_INT_FALLING);
    return 1u;
}

uint8_t M261BridgeSgpioMonitor_Stop(void)
{
    BridgeSgpioMonitor_EnsureReady();
    BridgeSgpioMonitor_StopHardware();
    g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_IDLE;
    BridgeSgpioMonitor_ClearResult();
    return 1u;
}

uint8_t M261BridgeSgpioMonitor_GetStatus(M261_BRIDGE_SGPIO_MONITOR_STATUS_T *status)
{
    if (status == 0)
    {
        return 0u;
    }
    BridgeSgpioMonitor_EnsureReady();
    M261BridgeSgpioMonitor_Process();
    memcpy(status, &g_sgpio_monitor.result, sizeof(*status));
    status->state = g_sgpio_monitor.state;
    status->expected_slot_count = g_sgpio_monitor.expected_slot_count;
    return 1u;
}

void M261BridgeSgpioMonitor_Process(void)
{
    uint32_t elapsed;
    uint16_t sample_count;

    if (g_sgpio_monitor.initialized == 0u)
    {
        return;
    }

    if (g_sgpio_monitor.state == M261_BRIDGE_SGPIO_MONITOR_STATE_ARMED)
    {
        elapsed = get_tick() - g_sgpio_monitor.arm_tick_ms;
        if (elapsed >= g_sgpio_monitor.trigger_timeout_ms)
        {
            GPIO_DisableInt(PA, M261_SGPIO_MONITOR_SLOAD_PIN);
            g_sgpio_monitor.result.error_flags = M261_BRIDGE_SGPIO_MONITOR_ERROR_NO_TRIGGER;
            g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_TIMEOUT;
            g_sgpio_monitor.result.state = g_sgpio_monitor.state;
        }
        return;
    }

    if (g_sgpio_monitor.state != M261_BRIDGE_SGPIO_MONITOR_STATE_CAPTURING)
    {
        return;
    }

    if ((PDMA_GET_TD_STS(M261_SGPIO_MONITOR_PDMA) & (1ul << M261_SGPIO_MONITOR_PDMA_CH)) != 0u)
    {
        TIMER_Stop(M261_SGPIO_MONITOR_TIMER);
        TIMER_SetTriggerTarget(M261_SGPIO_MONITOR_TIMER, 0u);
        PDMA_CLR_TD_FLAG(M261_SGPIO_MONITOR_PDMA, (1ul << M261_SGPIO_MONITOR_PDMA_CH));
        BridgeSgpioMonitor_Decode(M261_SGPIO_MONITOR_SAMPLE_MAX);
        return;
    }

    elapsed = get_tick() - g_sgpio_monitor.capture_tick_ms;
    if (elapsed >= M261_SGPIO_MONITOR_CAPTURE_TIMEOUT_MS)
    {
        TIMER_Stop(M261_SGPIO_MONITOR_TIMER);
        TIMER_SetTriggerTarget(M261_SGPIO_MONITOR_TIMER, 0u);
        sample_count = BridgeSgpioMonitor_TransferredCount();
        BridgeSgpioMonitor_Decode(sample_count);
        g_sgpio_monitor.result.error_flags |= M261_BRIDGE_SGPIO_MONITOR_ERROR_CAPTURE_TIMEOUT;
        g_sgpio_monitor.state = M261_BRIDGE_SGPIO_MONITOR_STATE_ERROR;
        g_sgpio_monitor.result.state = g_sgpio_monitor.state;
    }
}
