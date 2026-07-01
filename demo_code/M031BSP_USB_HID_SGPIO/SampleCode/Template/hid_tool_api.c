/******************************************************************************
 * @file     hid_tool_api.c
 * @brief    HID bridge dispatcher for SGPIO host testing.
 *****************************************************************************/

#include <string.h>

#include "NuMicro.h"
#include "bridge_le.h"
#include "bridge_protocol.h"
#include "bridge_version.h"
#include "hid_transfer.h"
#include "hid_tool_api.h"
#include "m031_bridge_sgpio.h"
#include "misc_config.h"

#define BRIDGE_INFO_NAME             "m032-sgpio-bridge/" M032_BRIDGE_FW_VERSION_STR
#define BRIDGE_RESET_REASON_NONE     0u
#define BRIDGE_RESET_REASON_CMD      2u
#define BRIDGE_RESET_DELAY_LOOPS     20000u

static volatile uint8_t g_u8CmdProcessReady = 0u;
static volatile uint8_t g_u8EP3Ready = 0u;
static volatile uint8_t g_u8ResetRequested = 0u;
static volatile uint32_t g_u32ResetCountdown = 0u;
static uint8_t g_u8LastResetReason = BRIDGE_RESET_REASON_NONE;

static uint8_t hid_buffer_to_pc[EP2_MAX_PKT_SIZE] = {0};
static uint8_t hid_buffer_from_pc[EP3_MAX_PKT_SIZE] = {0};

static void Bridge_BuildResponseHeader(uint8_t *resp, uint8_t cmd, uint8_t seq, uint8_t status, uint16_t payload_len)
{
    reset_buffer(resp, 0x00, EP2_MAX_PKT_SIZE);
    resp[0] = BRIDGE_MAGIC;
    resp[1] = cmd;
    resp[2] = seq;
    resp[3] = status;
    Bridge_WriteU16Le(&resp[4], payload_len);
}

static void Bridge_FinishResponse(uint8_t *resp, uint8_t cmd, uint8_t seq, uint8_t status,
                                  const uint8_t *payload, uint16_t payload_len)
{
    if (payload_len > BRIDGE_MAX_PAYLOAD)
    {
        status = BRIDGE_STATUS_BAD_PAYLOAD;
        payload_len = 0u;
    }

    Bridge_BuildResponseHeader(resp, cmd, seq, status, payload_len);
    if ((payload != 0) && (payload_len > 0u))
    {
        copy_buffer(&resp[BRIDGE_HEADER_SIZE], (void *)payload, payload_len);
    }
}

static uint8_t Bridge_HandleCore(uint8_t cmd, const uint8_t *payload, uint16_t payload_len,
                                 uint8_t *out, uint16_t *out_len, uint8_t *status)
{
    const char *name;
    uint16_t name_len;

    switch (cmd)
    {
        case BRIDGE_CMD_PING:
            if (payload_len > BRIDGE_MAX_PAYLOAD)
            {
                *status = BRIDGE_STATUS_BAD_PAYLOAD;
                *out_len = 0u;
                return 1u;
            }
            if (payload_len > 0u)
            {
                copy_buffer(out, (void *)payload, payload_len);
            }
            *out_len = payload_len;
            *status = BRIDGE_STATUS_OK;
            return 1u;

        case BRIDGE_CMD_GET_INFO:
            if (payload_len != 0u)
            {
                *status = BRIDGE_STATUS_BAD_PAYLOAD;
                *out_len = 0u;
                return 1u;
            }
            name = BRIDGE_INFO_NAME;
            name_len = (uint16_t)strlen(name);
            if (name_len > (BRIDGE_MAX_PAYLOAD - 2u))
            {
                name_len = BRIDGE_MAX_PAYLOAD - 2u;
            }
            copy_buffer(out, (void *)name, name_len);
            out[name_len] = 0u;
            out[name_len + 1u] = g_u8LastResetReason;
            *out_len = (uint16_t)(name_len + 2u);
            *status = BRIDGE_STATUS_OK;
            return 1u;

        case BRIDGE_CMD_RESET_MCU:
            if (payload_len != 0u)
            {
                *status = BRIDGE_STATUS_BAD_PAYLOAD;
                *out_len = 0u;
                return 1u;
            }
            g_u8LastResetReason = BRIDGE_RESET_REASON_CMD;
            g_u8ResetRequested = 1u;
            *out_len = 0u;
            *status = BRIDGE_STATUS_OK;
            return 1u;

        default:
            break;
    }

    return 0u;
}

static uint8_t Bridge_HandleSgpio(uint8_t cmd, const uint8_t *payload, uint16_t payload_len,
                                  uint8_t *out, uint16_t *out_len, uint8_t *status)
{
    switch (cmd)
    {
        case BRIDGE_CMD_SGPIO_CONFIG:
        {
            uint8_t slot_count;
            uint32_t clock_hz;

            if (payload_len != 5u)
            {
                *status = BRIDGE_STATUS_BAD_PAYLOAD;
                *out_len = 0u;
                return 1u;
            }
            slot_count = payload[0];
            clock_hz = Bridge_ReadU32Le(&payload[1]);
            if (M031BridgeSgpio_Config(slot_count, clock_hz) == 0u)
            {
                *status = BRIDGE_STATUS_NOT_READY;
                *out_len = 0u;
                return 1u;
            }
            out[0] = slot_count;
            Bridge_WriteU32Le(&out[1], clock_hz);
            *status = BRIDGE_STATUS_OK;
            *out_len = 5u;
            return 1u;
        }

        case BRIDGE_CMD_SGPIO_APPLY:
        {
            uint8_t enable;
            uint8_t periodic;
            uint16_t interval_ms;
            uint8_t sload_raw;
            uint16_t act_mask;
            uint16_t locate_mask;
            uint16_t fail_mask;

            if (payload_len != 11u)
            {
                *status = BRIDGE_STATUS_BAD_PAYLOAD;
                *out_len = 0u;
                return 1u;
            }
            enable = (payload[0] != 0u) ? 1u : 0u;
            periodic = (payload[1] != 0u) ? 1u : 0u;
            interval_ms = Bridge_ReadU16Le(&payload[2]);
            sload_raw = (uint8_t)(payload[4] & 0x0Fu);
            act_mask = Bridge_ReadU16Le(&payload[5]);
            locate_mask = Bridge_ReadU16Le(&payload[7]);
            fail_mask = Bridge_ReadU16Le(&payload[9]);
            if (M031BridgeSgpio_Apply(enable, periodic, interval_ms, sload_raw,
                                      act_mask, locate_mask, fail_mask) == 0u)
            {
                *status = BRIDGE_STATUS_NOT_READY;
                *out_len = 0u;
                return 1u;
            }
            out[0] = enable;
            out[1] = periodic;
            Bridge_WriteU16Le(&out[2], interval_ms);
            out[4] = sload_raw;
            Bridge_WriteU16Le(&out[5], act_mask);
            Bridge_WriteU16Le(&out[7], locate_mask);
            Bridge_WriteU16Le(&out[9], fail_mask);
            *status = BRIDGE_STATUS_OK;
            *out_len = 11u;
            return 1u;
        }

        case BRIDGE_CMD_SGPIO_STATUS:
        {
            uint8_t enabled;
            uint8_t periodic;
            uint8_t slot_count;
            uint32_t clock_hz;
            uint16_t interval_ms;
            uint8_t sload_raw;
            uint16_t act_mask;
            uint16_t locate_mask;
            uint16_t fail_mask;
            uint8_t sdata_in_level;
            uint8_t sdata_in_valid;
            uint8_t sdata_in_word_count;
            uint8_t sdata_in_bit_count;
            uint32_t sdata_in_words[4];
            uint8_t i;
            uint16_t index;

            if (payload_len != 0u)
            {
                *status = BRIDGE_STATUS_BAD_PAYLOAD;
                *out_len = 0u;
                return 1u;
            }

            enabled = 0u;
            periodic = 0u;
            slot_count = 0u;
            clock_hz = 0u;
            interval_ms = 0u;
            sload_raw = 0u;
            act_mask = 0u;
            locate_mask = 0u;
            fail_mask = 0u;
            sdata_in_level = 0u;
            sdata_in_valid = 0u;
            sdata_in_word_count = 0u;
            sdata_in_bit_count = 0u;
            for (i = 0u; i < 4u; ++i)
            {
                sdata_in_words[i] = 0u;
            }

            if (M031BridgeSgpio_GetStatus(&enabled, &periodic, &slot_count, &clock_hz, &interval_ms, &sload_raw,
                                          &act_mask, &locate_mask, &fail_mask, &sdata_in_level,
                                          &sdata_in_valid, &sdata_in_word_count, &sdata_in_bit_count,
                                          sdata_in_words) == 0u)
            {
                *status = BRIDGE_STATUS_NOT_READY;
                *out_len = 0u;
                return 1u;
            }

            out[0] = enabled;
            out[1] = periodic;
            out[2] = slot_count;
            out[3] = sload_raw;
            Bridge_WriteU32Le(&out[4], clock_hz);
            Bridge_WriteU16Le(&out[8], interval_ms);
            Bridge_WriteU16Le(&out[10], act_mask);
            Bridge_WriteU16Le(&out[12], locate_mask);
            Bridge_WriteU16Le(&out[14], fail_mask);
            out[16] = 2u;
            out[17] = 0u;
            out[18] = 3u;
            out[19] = 1u;
            out[20] = sdata_in_level;
            out[21] = sdata_in_valid;
            out[22] = sdata_in_word_count;
            out[23] = sdata_in_bit_count;

            index = 24u;
            if (sdata_in_word_count > 4u)
            {
                sdata_in_word_count = 4u;
            }
            for (i = 0u; i < sdata_in_word_count; ++i)
            {
                Bridge_WriteU32Le(&out[index], sdata_in_words[i]);
                index = (uint16_t)(index + 4u);
            }

            *status = BRIDGE_STATUS_OK;
            *out_len = index;
            return 1u;
        }

        case BRIDGE_CMD_SGPIO_OFF:
            if (payload_len != 0u)
            {
                *status = BRIDGE_STATUS_BAD_PAYLOAD;
                *out_len = 0u;
                return 1u;
            }
            if (M031BridgeSgpio_Off() == 0u)
            {
                *status = BRIDGE_STATUS_NOT_READY;
                *out_len = 0u;
                return 1u;
            }
            out[0] = 0u;
            *status = BRIDGE_STATUS_OK;
            *out_len = 1u;
            return 1u;

        default:
            break;
    }

    return 0u;
}

static void Bridge_ProcessCommand(const uint8_t *req, uint8_t *resp)
{
    uint8_t cmd;
    uint8_t seq;
    uint16_t payload_len;
    const uint8_t *payload;
    uint8_t temp[BRIDGE_MAX_PAYLOAD];
    uint16_t out_len;
    uint8_t status;
    uint8_t handled;

    cmd = req[1];
    seq = req[2];
    payload_len = Bridge_ReadU16Le(&req[4]);

    if (req[0] != BRIDGE_MAGIC)
    {
        Bridge_FinishResponse(resp, cmd, seq, BRIDGE_STATUS_BAD_MAGIC, 0, 0u);
        return;
    }
    if (payload_len > BRIDGE_MAX_PAYLOAD)
    {
        Bridge_FinishResponse(resp, cmd, seq, BRIDGE_STATUS_BAD_PAYLOAD, 0, 0u);
        return;
    }

    payload = &req[BRIDGE_HEADER_SIZE];
    reset_buffer(temp, 0x00, sizeof(temp));
    out_len = 0u;
    status = BRIDGE_STATUS_OK;
    handled = 0u;

    if (handled == 0u)
    {
        handled = Bridge_HandleCore(cmd, payload, payload_len, temp, &out_len, &status);
    }
    if (handled == 0u)
    {
        handled = Bridge_HandleSgpio(cmd, payload, payload_len, temp, &out_len, &status);
    }

    if (handled == 0u)
    {
        status = BRIDGE_STATUS_BAD_COMMAND;
        out_len = 0u;
    }

    Bridge_FinishResponse(resp, cmd, seq, status, temp, out_len);
}

void HidTool_ResetState(void)
{
    g_u8CmdProcessReady = 0u;
    g_u8EP3Ready = 0u;
    g_u8ResetRequested = 0u;
    g_u32ResetCountdown = 0u;
    reset_buffer(hid_buffer_to_pc, 0x00, sizeof(hid_buffer_to_pc));
    reset_buffer(hid_buffer_from_pc, 0x00, sizeof(hid_buffer_from_pc));
}

void HidTool_OnOutReady(void)
{
    g_u8EP3Ready = 1u;
}

void HidTool_GetOutReport(uint8_t *pu8EpBuf, uint32_t u32Size)
{
    uint32_t copy_len;

    copy_len = u32Size;
    if (copy_len > EP3_MAX_PKT_SIZE)
    {
        copy_len = EP3_MAX_PKT_SIZE;
    }

    reset_buffer(hid_buffer_from_pc, 0x00, sizeof(hid_buffer_from_pc));
    USBD_MemCopy((uint8_t *)&hid_buffer_from_pc, pu8EpBuf, copy_len);

    reset_buffer(hid_buffer_to_pc, 0x00, sizeof(hid_buffer_to_pc));
    Bridge_ProcessCommand(hid_buffer_from_pc, hid_buffer_to_pc);
    g_u8CmdProcessReady = 1u;
}

void HidTool_SetInReport(void)
{
    if (g_u8CmdProcessReady == 0u)
    {
        return;
    }

    USBD_MemCopy((uint8_t *)(USBD_BUF_BASE + USBD_GET_EP_BUF_ADDR(EP2)),
                 (void *)&hid_buffer_to_pc,
                 EP2_MAX_PKT_SIZE);
    USBD_SET_PAYLOAD_LEN(EP2, EP2_MAX_PKT_SIZE);

    g_u8CmdProcessReady = 0u;
    reset_buffer(hid_buffer_to_pc, 0x00, sizeof(hid_buffer_to_pc));

    if (g_u8ResetRequested != 0u)
    {
        g_u32ResetCountdown = BRIDGE_RESET_DELAY_LOOPS;
    }
}

void HidTool_Process(void)
{
    uint8_t *ptr;

    if (g_u8EP3Ready != 0u)
    {
        g_u8EP3Ready = 0u;
        g_u8CmdProcessReady = 0u;

        ptr = (uint8_t *)(USBD_BUF_BASE + USBD_GET_EP_BUF_ADDR(EP3));
        HidTool_GetOutReport(ptr, USBD_GET_PAYLOAD_LEN(EP3));
        USBD_SET_PAYLOAD_LEN(EP3, EP3_MAX_PKT_SIZE);
    }

    HidTool_SetInReport();

    if (g_u32ResetCountdown > 0u)
    {
        --g_u32ResetCountdown;
        if (g_u32ResetCountdown == 0u)
        {
            SYS_UnlockReg();
            SYS_ResetChip();
        }
    }
}
