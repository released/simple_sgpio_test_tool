#ifndef __M261_BRIDGE_SGPIO_H__
#define __M261_BRIDGE_SGPIO_H__

#include <stdint.h>

#define M261_BRIDGE_SGPIO_SLOT_MAX        16u
#define M261_BRIDGE_SGPIO_CLOCK_MIN_HZ    100000u
#define M261_BRIDGE_SGPIO_CLOCK_MAX_HZ    400000u
#define M261_BRIDGE_SGPIO_INTERVAL_MIN_MS 20u
#define M261_BRIDGE_SGPIO_INTERVAL_MAX_MS 5000u

uint8_t M261BridgeSgpio_Config(uint8_t slot_count, uint32_t clock_hz);
uint8_t M261BridgeSgpio_Apply(uint8_t enable, uint8_t periodic, uint16_t interval_ms, uint8_t sload_raw,
                              uint16_t act_mask, uint16_t locate_mask, uint16_t fail_mask);
uint8_t M261BridgeSgpio_Off(void);
uint8_t M261BridgeSgpio_GetStatus(uint8_t *enabled, uint8_t *periodic, uint8_t *slot_count,
                                  uint32_t *clock_hz, uint16_t *interval_ms, uint8_t *sload_raw,
                                  uint16_t *act_mask, uint16_t *locate_mask, uint16_t *fail_mask,
                                  uint8_t *sdata_in_level, uint8_t *sdata_in_valid,
                                  uint8_t *sdata_in_word_count, uint8_t *sdata_in_bit_count,
                                  uint32_t *sdata_in_words);
void M261BridgeSgpio_Process(void);

#endif /* __M261_BRIDGE_SGPIO_H__ */
