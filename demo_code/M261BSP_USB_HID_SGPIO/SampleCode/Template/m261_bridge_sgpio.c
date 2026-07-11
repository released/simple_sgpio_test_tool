#include "NuMicro.h"
#include "m261_bridge_sgpio.h"

#define M261_SGPIO_SDATA_IN_PIN        1u
#define M261_SGPIO_SLOAD_PIN           3u
#define M261_SGPIO_SDATA_OUT_PIN       0u
#define M261_SGPIO_SCLOCK_PIN          2u
#define M261_SGPIO_OUT_MASK            (BIT0 | BIT2 | BIT3)
#define M261_SGPIO_IDLE_OUT_MASK       (BIT3)
#define M261_SGPIO_CAPTURE_WORD_MAX    4u
#define M261_SGPIO_WAVEFORM_WORD_MAX   130u
#define M261_SGPIO_PDMA_CH             0u
#define M261_SGPIO_PDMA_MODULE         PDMA0_MODULE
#define M261_SGPIO_PDMA                PDMA0
#define M261_SGPIO_TIMER               TIMER0
#define M261_SGPIO_TIMER_MODULE        TMR0_MODULE
#define M261_SGPIO_TIMER_CLKSEL        CLK_CLKSEL1_TMR0SEL_HIRC
#define M261_SGPIO_PDMA_REQ            PDMA_TMR0

typedef struct
{
    uint8_t initialized;
    uint8_t enabled;
    uint8_t periodic;
    uint8_t first_frame;
    uint8_t frame_active;
    uint8_t slot_count;
    uint8_t sload_raw;
    uint32_t clock_hz;
    uint32_t last_tick_ms;
    uint16_t interval_ms;
    uint16_t act_mask;
    uint16_t locate_mask;
    uint16_t fail_mask;
    uint16_t waveform_count;
    uint32_t saved_pa_datmsk;
} M261_BRIDGE_SGPIO_STATE_T;

extern uint32_t get_tick(void);

static M261_BRIDGE_SGPIO_STATE_T g_sgpio =
{
    0u,
    0u,
    1u,
    1u,
    0u,
    8u,
    0u,
    100000u,
    0u,
    100u,
    0x0001u,
    0x0000u,
    0x0000u,
    0u,
    0u
};

static uint32_t g_sgpio_waveform[M261_SGPIO_WAVEFORM_WORD_MAX];

static uint32_t BridgeSgpio_ClampClock(uint32_t clock_hz)
{
    if (clock_hz < M261_BRIDGE_SGPIO_CLOCK_MIN_HZ)
    {
        return M261_BRIDGE_SGPIO_CLOCK_MIN_HZ;
    }
    if (clock_hz > M261_BRIDGE_SGPIO_CLOCK_MAX_HZ)
    {
        return M261_BRIDGE_SGPIO_CLOCK_MAX_HZ;
    }
    return clock_hz;
}

static uint8_t BridgeSgpio_ClampSlotCount(uint8_t slot_count)
{
    if (slot_count == 0u)
    {
        return 1u;
    }
    if (slot_count > M261_BRIDGE_SGPIO_SLOT_MAX)
    {
        return M261_BRIDGE_SGPIO_SLOT_MAX;
    }
    return slot_count;
}

static uint16_t BridgeSgpio_ClampInterval(uint16_t interval_ms)
{
    if (interval_ms < M261_BRIDGE_SGPIO_INTERVAL_MIN_MS)
    {
        return M261_BRIDGE_SGPIO_INTERVAL_MIN_MS;
    }
    if (interval_ms > M261_BRIDGE_SGPIO_INTERVAL_MAX_MS)
    {
        return M261_BRIDGE_SGPIO_INTERVAL_MAX_MS;
    }
    return interval_ms;
}

static void BridgeSgpio_DriveIdle(void)
{
    PA->DOUT = (PA->DOUT & ~M261_SGPIO_OUT_MASK) | M261_SGPIO_IDLE_OUT_MASK;
}

static void BridgeSgpio_InitPins(void)
{
    SYS_UnlockReg();
    SYS->GPA_MFPL = (SYS->GPA_MFPL &
                    ~(SYS_GPA_MFPL_PA0MFP_Msk |
                      SYS_GPA_MFPL_PA1MFP_Msk |
                      SYS_GPA_MFPL_PA2MFP_Msk |
                      SYS_GPA_MFPL_PA3MFP_Msk)) |
                    (SYS_GPA_MFPL_PA0MFP_GPIO |
                     SYS_GPA_MFPL_PA1MFP_GPIO |
                     SYS_GPA_MFPL_PA2MFP_GPIO |
                     SYS_GPA_MFPL_PA3MFP_GPIO);
    SYS_LockReg();

    GPIO_SetMode(PA, BIT0 | BIT2 | BIT3, GPIO_MODE_OUTPUT);
    GPIO_SetMode(PA, BIT1, GPIO_MODE_INPUT);
    BridgeSgpio_DriveIdle();
}

static void BridgeSgpio_InitDmaClock(void)
{
    SYS_UnlockReg();
    CLK_EnableModuleClock(M261_SGPIO_PDMA_MODULE);
    CLK_EnableModuleClock(M261_SGPIO_TIMER_MODULE);
    CLK_SetModuleClock(M261_SGPIO_TIMER_MODULE, M261_SGPIO_TIMER_CLKSEL, 0u);
    SYS_LockReg();
}

static void BridgeSgpio_EnsureReady(void)
{
    if (g_sgpio.initialized == 0u)
    {
        BridgeSgpio_InitDmaClock();
        BridgeSgpio_InitPins();
        g_sgpio.saved_pa_datmsk = PA->DATMSK;
        g_sgpio.initialized = 1u;
    }
}

static uint8_t BridgeSgpio_IsFrameDone(void)
{
    if (g_sgpio.frame_active == 0u)
    {
        return 1u;
    }
    if ((PDMA_GET_TD_STS(M261_SGPIO_PDMA) & (1ul << M261_SGPIO_PDMA_CH)) != 0u)
    {
        return 1u;
    }
    if ((M261_SGPIO_PDMA->DSCT[M261_SGPIO_PDMA_CH].CTL & PDMA_DSCT_CTL_OPMODE_Msk) == PDMA_OP_STOP)
    {
        return 1u;
    }
    return 0u;
}

static void BridgeSgpio_StopFrame(void)
{
    TIMER_Stop(M261_SGPIO_TIMER);
    TIMER_SetTriggerTarget(M261_SGPIO_TIMER, 0u);
    TIMER_ClearIntFlag(M261_SGPIO_TIMER);

    PDMA_PAUSE(M261_SGPIO_PDMA, M261_SGPIO_PDMA_CH);
    PDMA_RESET(M261_SGPIO_PDMA, M261_SGPIO_PDMA_CH);
    PDMA_CLR_TD_FLAG(M261_SGPIO_PDMA, (1ul << M261_SGPIO_PDMA_CH));
    g_sgpio.frame_active = 0u;
    BridgeSgpio_DriveIdle();
}

static void BridgeSgpio_FinishFrameIfDone(void)
{
    if (g_sgpio.frame_active == 0u)
    {
        return;
    }
    if (BridgeSgpio_IsFrameDone() == 0u)
    {
        return;
    }

    TIMER_Stop(M261_SGPIO_TIMER);
    TIMER_SetTriggerTarget(M261_SGPIO_TIMER, 0u);
    TIMER_ClearIntFlag(M261_SGPIO_TIMER);
    PDMA_CLR_TD_FLAG(M261_SGPIO_PDMA, (1ul << M261_SGPIO_PDMA_CH));
    g_sgpio.frame_active = 0u;
    g_sgpio.first_frame = 0u;
    BridgeSgpio_DriveIdle();
}

static uint32_t BridgeSgpio_MakeDout(uint8_t sload, uint8_t sdata, uint8_t sclk)
{
    uint32_t value;

    value = PA->DOUT & ~M261_SGPIO_OUT_MASK;
    if (sdata != 0u)
    {
        value |= BIT0;
    }
    if (sclk != 0u)
    {
        value |= BIT2;
    }
    if (sload != 0u)
    {
        value |= BIT3;
    }
    return value;
}

static void BridgeSgpio_AppendLevel(uint32_t dout_value)
{
    if (g_sgpio.waveform_count >= M261_SGPIO_WAVEFORM_WORD_MAX)
    {
        return;
    }
    g_sgpio_waveform[g_sgpio.waveform_count] = dout_value;
    g_sgpio.waveform_count = (uint16_t)(g_sgpio.waveform_count + 1u);
}

static void BridgeSgpio_AppendClock(uint8_t sload, uint8_t sdata, uint16_t *pair_count)
{
    BridgeSgpio_AppendLevel(BridgeSgpio_MakeDout(sload, sdata, 0u));
    BridgeSgpio_AppendLevel(BridgeSgpio_MakeDout(sload, sdata, 1u));

    if (pair_count != 0)
    {
        *pair_count = (uint16_t)(*pair_count + 1u);
    }
}

static uint8_t BridgeSgpio_SlotStreamBit(uint8_t slots_to_emit, uint16_t bit_index)
{
    uint8_t slot;
    uint8_t bit_sel;
    uint8_t value;

    if (slots_to_emit == 0u)
    {
        return 0u;
    }

    slot = (uint8_t)(bit_index / 3u);
    bit_sel = (uint8_t)(bit_index % 3u);
    value = 0u;

    if (slot >= slots_to_emit)
    {
        return 0u;
    }
    if (slot < g_sgpio.slot_count)
    {
        if (bit_sel == 0u)
        {
            value = (uint8_t)((g_sgpio.act_mask >> slot) & 0x01u);
        }
        else if (bit_sel == 1u)
        {
            value = (uint8_t)((g_sgpio.locate_mask >> slot) & 0x01u);
        }
        else
        {
            value = (uint8_t)((g_sgpio.fail_mask >> slot) & 0x01u);
        }
    }

    return value;
}

static void BridgeSgpio_BuildWaveform(void)
{
    uint8_t sync_bit;
    uint8_t slots_to_emit;
    uint8_t sload_raw;
    uint8_t sload_bit;
    uint8_t sdata_bit;
    uint16_t slot_bit;
    uint16_t slot_bit_count;
    uint16_t pair_count;

    g_sgpio.waveform_count = 0u;
    slots_to_emit = g_sgpio.slot_count;
    if (slots_to_emit < 4u)
    {
        slots_to_emit = 4u;
    }

    sload_raw = (uint8_t)(g_sgpio.sload_raw & 0x0Fu);
    if (g_sgpio.first_frame != 0u)
    {
        sload_raw = (uint8_t)(sload_raw & 0x0Eu);
    }

    pair_count = 0u;
    for (sync_bit = 0u; sync_bit < 5u; ++sync_bit)
    {
        BridgeSgpio_AppendClock(0u, 0u, &pair_count);
    }

    /* SFF-8485: SLOAD high restarts the stream; slot data follows on the next clock. */
    BridgeSgpio_AppendClock(1u, 0u, &pair_count);

    slot_bit_count = (uint16_t)slots_to_emit * 3u;
    for (slot_bit = 0u; slot_bit < slot_bit_count; ++slot_bit)
    {
        sload_bit = 0u;
        if (slot_bit < 4u)
        {
            sload_bit = (uint8_t)((sload_raw >> slot_bit) & 0x01u);
        }
        sdata_bit = BridgeSgpio_SlotStreamBit(slots_to_emit, slot_bit);
        BridgeSgpio_AppendClock(sload_bit, sdata_bit, &pair_count);
    }

    while ((pair_count & 0x0Fu) != 0u)
    {
        BridgeSgpio_AppendClock(1u, 0u, &pair_count);
    }

    BridgeSgpio_AppendLevel(BridgeSgpio_MakeDout(1u, 0u, 0u));
}

static uint8_t BridgeSgpio_StartFrame(void)
{
    uint32_t half_rate_hz;
    uint32_t actual_rate_hz;

    BridgeSgpio_EnsureReady();
    BridgeSgpio_FinishFrameIfDone();
    if (g_sgpio.frame_active != 0u)
    {
        return 0u;
    }

    BridgeSgpio_BuildWaveform();
    if (g_sgpio.waveform_count < 2u)
    {
        return 0u;
    }

    PA->DATMSK = (g_sgpio.saved_pa_datmsk | (~M261_SGPIO_OUT_MASK & 0xFFFFu)) & ~M261_SGPIO_OUT_MASK;
    BridgeSgpio_DriveIdle();

    TIMER_Stop(M261_SGPIO_TIMER);
    TIMER_SetTriggerTarget(M261_SGPIO_TIMER, 0u);
    TIMER_ClearIntFlag(M261_SGPIO_TIMER);

    PDMA_RESET(M261_SGPIO_PDMA, M261_SGPIO_PDMA_CH);
    PDMA_Open(M261_SGPIO_PDMA, (1ul << M261_SGPIO_PDMA_CH));
    PDMA_CLR_TD_FLAG(M261_SGPIO_PDMA, (1ul << M261_SGPIO_PDMA_CH));
    PDMA_SetTransferCnt(M261_SGPIO_PDMA, M261_SGPIO_PDMA_CH, PDMA_WIDTH_32, g_sgpio.waveform_count);
    PDMA_SetTransferAddr(M261_SGPIO_PDMA, M261_SGPIO_PDMA_CH,
                         (uint32_t)&g_sgpio_waveform[0], PDMA_SAR_INC,
                         (uint32_t)&PA->DOUT, PDMA_DAR_FIX);
    PDMA_SetTransferMode(M261_SGPIO_PDMA, M261_SGPIO_PDMA_CH, M261_SGPIO_PDMA_REQ, 0u, 0u);
    PDMA_SetBurstType(M261_SGPIO_PDMA, M261_SGPIO_PDMA_CH, PDMA_REQ_SINGLE, PDMA_BURST_1);

    half_rate_hz = g_sgpio.clock_hz * 2u;
    actual_rate_hz = TIMER_Open(M261_SGPIO_TIMER, TIMER_PERIODIC_MODE, half_rate_hz);
    (void)actual_rate_hz;
    TIMER_SetTriggerSource(M261_SGPIO_TIMER, TIMER_TRGSRC_TIMEOUT_EVENT);
    TIMER_SetTriggerTarget(M261_SGPIO_TIMER, TIMER_TRG_TO_PDMA);
    TIMER_ClearIntFlag(M261_SGPIO_TIMER);

    g_sgpio.frame_active = 1u;
    TIMER_Start(M261_SGPIO_TIMER);
    return 1u;
}

uint8_t M261BridgeSgpio_Config(uint8_t slot_count, uint32_t clock_hz)
{
    g_sgpio.slot_count = BridgeSgpio_ClampSlotCount(slot_count);
    g_sgpio.clock_hz = BridgeSgpio_ClampClock(clock_hz);
    BridgeSgpio_EnsureReady();
    return 1u;
}

uint8_t M261BridgeSgpio_Apply(uint8_t enable, uint8_t periodic, uint16_t interval_ms, uint8_t sload_raw,
                              uint16_t act_mask, uint16_t locate_mask, uint16_t fail_mask)
{
    BridgeSgpio_EnsureReady();
    BridgeSgpio_FinishFrameIfDone();

    g_sgpio.periodic = (periodic != 0u) ? 1u : 0u;
    g_sgpio.interval_ms = BridgeSgpio_ClampInterval(interval_ms);
    g_sgpio.sload_raw = (uint8_t)(sload_raw & 0x0Fu);
    g_sgpio.act_mask = act_mask;
    g_sgpio.locate_mask = locate_mask;
    g_sgpio.fail_mask = fail_mask;
    g_sgpio.last_tick_ms = get_tick();

    if (enable == 0u)
    {
        return M261BridgeSgpio_Off();
    }

    if (g_sgpio.frame_active != 0u)
    {
        BridgeSgpio_StopFrame();
    }

    g_sgpio.enabled = 1u;
    if (BridgeSgpio_StartFrame() == 0u)
    {
        g_sgpio.enabled = 0u;
        return 0u;
    }
    g_sgpio.last_tick_ms = get_tick();
    return 1u;
}

uint8_t M261BridgeSgpio_Off(void)
{
    BridgeSgpio_EnsureReady();
    BridgeSgpio_StopFrame();
    g_sgpio.enabled = 0u;
    g_sgpio.periodic = 0u;
    g_sgpio.first_frame = 1u;
    PA->DATMSK = g_sgpio.saved_pa_datmsk;
    return 1u;
}

uint8_t M261BridgeSgpio_GetStatus(uint8_t *enabled, uint8_t *periodic, uint8_t *slot_count,
                                  uint32_t *clock_hz, uint16_t *interval_ms, uint8_t *sload_raw,
                                  uint16_t *act_mask, uint16_t *locate_mask, uint16_t *fail_mask,
                                  uint8_t *sdata_in_level, uint8_t *sdata_in_valid,
                                  uint8_t *sdata_in_word_count, uint8_t *sdata_in_bit_count,
                                  uint32_t *sdata_in_words)
{
    uint8_t i;

    BridgeSgpio_EnsureReady();
    BridgeSgpio_FinishFrameIfDone();

    if (enabled != 0)
    {
        *enabled = g_sgpio.enabled;
    }
    if (periodic != 0)
    {
        *periodic = g_sgpio.periodic;
    }
    if (slot_count != 0)
    {
        *slot_count = g_sgpio.slot_count;
    }
    if (clock_hz != 0)
    {
        *clock_hz = g_sgpio.clock_hz;
    }
    if (interval_ms != 0)
    {
        *interval_ms = g_sgpio.interval_ms;
    }
    if (sload_raw != 0)
    {
        *sload_raw = g_sgpio.sload_raw;
    }
    if (act_mask != 0)
    {
        *act_mask = g_sgpio.act_mask;
    }
    if (locate_mask != 0)
    {
        *locate_mask = g_sgpio.locate_mask;
    }
    if (fail_mask != 0)
    {
        *fail_mask = g_sgpio.fail_mask;
    }
    if (sdata_in_level != 0)
    {
        *sdata_in_level = (PA1 != 0u) ? 1u : 0u;
    }
    if (sdata_in_valid != 0)
    {
        *sdata_in_valid = 0u;
    }
    if (sdata_in_word_count != 0)
    {
        *sdata_in_word_count = 0u;
    }
    if (sdata_in_bit_count != 0)
    {
        *sdata_in_bit_count = 0u;
    }
    if (sdata_in_words != 0)
    {
        for (i = 0u; i < M261_SGPIO_CAPTURE_WORD_MAX; ++i)
        {
            sdata_in_words[i] = 0u;
        }
    }

    return 1u;
}

void M261BridgeSgpio_Process(void)
{
    uint32_t now;

    BridgeSgpio_FinishFrameIfDone();

    if (g_sgpio.enabled == 0u)
    {
        return;
    }
    if (g_sgpio.periodic == 0u)
    {
        return;
    }
    if (g_sgpio.frame_active != 0u)
    {
        return;
    }

    now = get_tick();
    if ((uint32_t)(now - g_sgpio.last_tick_ms) < (uint32_t)g_sgpio.interval_ms)
    {
        return;
    }

    if (BridgeSgpio_StartFrame() != 0u)
    {
        g_sgpio.last_tick_ms = now;
    }
}
