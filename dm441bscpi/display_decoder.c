#include "dm441bscpi.h"
#include "display_decoder.h"
uint16_t dm_decode_leds(uint16_t raw) {
    /* IC3 H..A arrive in bits 15..8, then IC4 H..A in bits 7..0.
     * Active-low inputs are IC3 H..B and IC4 G (negative sign). */
    return (raw ^ 0xfe40u) & 0xff7fu;
}
bool dm_decode(dm_decoder_t *s, uint16_t raw, uint64_t now, uint64_t *display) {
    static const uint16_t select[] = {0x0001, 0x8000, 0x4000, 0x2000, 0x0100};
    unsigned pos;
    for (pos = 0; pos < 5; ++pos) if ((raw & 0xe101u) == select[pos]) break;
    /* IC1 B=BCD_1 (LSB), C=BCD_2, D=BCD_3, E=BCD_4. */
    uint8_t bcd = (raw >> 9) & 15u;
    if (pos == 5 || bcd > 9) { s->seen = 0; s->flags = 0; return false; }
    if (now-s->last_us > 100000 || (s->seen & (1u << pos))) {
        s->seen = 0;
        s->flags = 0;
    }
    s->last_us = now;
    s->digits[pos] = bcd;
    s->seen |= 1u << pos;
    /* Live meter captures show DP_1..DP_4 are active low. Normalize
     * just those pins. UNDER/OVER keep their active-high polarity. */
    s->flags |= (raw ^ 0x001eu) & 0x007eu;
    if (s->seen != 31) return false;
    uint64_t magnitude = 0;
    /* DIGIT_1 is the rightmost (units) digit, DIGIT_5 the leftmost. */
    for (unsigned i = 5; i > 0; --i) magnitude = magnitude * 10 + s->digits[i-1];
    uint64_t next = magnitude << 16;
    if (s->flags & (1u << 6)) next |= DM_UNDER;
    if (s->flags & (1u << 5)) next |= DM_OVER;
    unsigned dp = (s->flags >> 1) & 15u;
    /* Two bits encode exactly four decimal positions. Reject contradictory
     * multi-point scans; all-off defaults to position zero. */
    if (dp && (dp & (dp-1))) { s->seen = 0; s->flags = 0; return false; }
    /* DP_n places n fractional digits to its right. Preserve the SCPI
     * field's left-to-right positions (DP_1 -> 3, DP_4 -> 0). */
    for (unsigned i = 0; i < 4; ++i)
        if (dp & (1u << (3-i))) next |= (uint64_t)(3-i) << 61;
    *display = next;
    s->seen = 0;
    s->flags = 0;
    return true;
}
