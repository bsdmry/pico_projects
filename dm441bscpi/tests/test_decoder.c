#include <assert.h>
#include <stdio.h>
#include "dm441bscpi.h"
#include "display_decoder.h"
static uint16_t sample(unsigned pos, unsigned digit, uint16_t flags) {
    static const uint16_t select[] = {0x8000, 1, 2, 4, 0x80};
    uint16_t raw = select[pos] | flags;
    for (unsigned i = 0; i < 4; ++i) raw |= ((digit >> i) & 1u) << (6-i);
    return raw;
}
static void frame(unsigned magnitude, unsigned dp, uint16_t flags) {
    dm_decoder_t s = {0};
    uint64_t out = 0;
    unsigned divisor = 10000;
    for (unsigned i = 0; i < 5; ++i) {
        bool ready = dm_decode(&s, sample(i, magnitude/divisor%10,
            flags | (1u << (11+dp))), 1000+i*100, &out);
        assert(ready == (i == 4));
        divisor /= 10;
    }
    assert(((out >> 16) & UINT64_C(0x7ffffffffff)) == magnitude);
    assert(((out >> 61) & 3) == dp);
    assert(!!(out & DM_UNDER) == !!(flags & 0x200));
    assert(!!(out & DM_OVER) == !!(flags & 0x400));
}
int main(void) {
    for (unsigned dp = 0; dp < 4; ++dp) {
        frame(0, dp, 0); frame(12345, dp, 0); frame(19999, dp, 0x200);
        frame(99999, dp, 0x400);
    }
    assert(dm_decode_leds(0x027f) == 0);
    assert(dm_decode_leds((uint16_t)~0x027f) == 0xfeff);
    for (unsigned bit = 0; bit < 16; ++bit)
        assert(dm_decode_leds(0x027f ^ (1u << bit)) == (bit == 8 ? 0 : (1u << bit)));
    dm_decoder_t s = {0};
    uint64_t out = 42;
    assert(!dm_decode(&s, sample(0, 1, 0), 1, &out));
    assert(!dm_decode(&s, sample(1, 2, 0), 2, &out));
    assert(!dm_decode(&s, sample(1, 3, 0), 3, &out)); /* repeated position resets */
    assert(s.seen == 2);
    assert(!dm_decode(&s, sample(2, 4, 0), 200000, &out)); /* scan timeout */
    assert(s.seen == 4);
    assert(!dm_decode(&s, sample(3, 15, 0), 200001, &out));
    assert(s.seen == 0 && out == 42);
    assert(!dm_decode(&s, 0x8001, 200002, &out)); /* multiple selected digits */
    for (unsigned i = 0; i < 5; ++i)
        assert(!dm_decode(&s, sample(i, i, 0x1800), 200003+i, &out));
    assert(out == 42); /* conflicting decimal points rejected */
    puts("decoder tests passed");
}
