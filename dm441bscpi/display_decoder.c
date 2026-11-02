#include "dm441bscpi.h"
#include "display_decoder.h"
uint16_t dm_decode_leds(uint16_t raw) {
    return (raw ^ 0x027fu) & 0xfeffu;
}
bool dm_decode(dm_decoder_t *s, uint16_t raw, uint64_t now, uint64_t *display) {
    static const uint16_t select[] = {0x8000, 1, 2, 4, 0x80};
    unsigned pos;
    for (pos = 0; pos < 5; ++pos) if ((raw & 0x8087u) == select[pos]) break;
    uint8_t bcd = 0;
    for (unsigned i = 0; i < 4; ++i) bcd |= ((raw >> (6-i)) & 1u) << i;
    if (pos == 5 || bcd > 9) { s->seen = 0; s->flags = 0; return false; }
    if (now-s->last_us > 100000 || (s->seen & (1u << pos))) {
        s->seen = 0;
        s->flags = 0;
    }
    s->last_us = now;
    s->digits[pos] = bcd;
    s->seen |= 1u << pos;
    s->flags |= raw & 0x7e00u;
    if (s->seen != 31) return false;
    uint64_t magnitude = 0;
    for (unsigned i = 0; i < 5; ++i) magnitude = magnitude * 10 + s->digits[i];
    uint64_t next = magnitude << 16;
    if (s->flags & (1u << 9)) next |= DM_UNDER;
    if (s->flags & (1u << 10)) next |= DM_OVER;
    unsigned dp = (s->flags >> 11) & 15u;
    /* Two bits encode exactly four decimal positions. Reject contradictory
     * multi-point scans; all-off defaults to position zero. */
    if (dp && (dp & (dp-1))) { s->seen = 0; s->flags = 0; return false; }
    for (unsigned i = 0; i < 4; ++i)
        if (dp & (1u << i)) next |= (uint64_t)i << 61;
    *display = next;
    s->seen = 0;
    s->flags = 0;
    return true;
}
