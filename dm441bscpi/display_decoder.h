#ifndef DISPLAY_DECODER_H
#define DISPLAY_DECODER_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint8_t digits[5], seen;
    uint16_t flags;
    uint64_t last_us;
} dm_decoder_t;
bool dm_decode(dm_decoder_t *state, uint16_t raw, uint64_t now_us, uint64_t *display);
uint16_t dm_decode_leds(uint16_t raw);
#endif
