#ifndef LED14SEG_H
#define LED14SEG_H
#include <stdint.h>
#include "hardware/spi.h"

#define LED14SEG_CHARACTER_COUNT 8u

static const uint16_t syms[95] = {
    0x0000, 0x1400, 0x8002, 0xe0aa, //sp, !, ", #
    0xbbaa, 0x1144, 0xe192, 0x0100, //$, %, &, '
    0x1014, 0x0141, 0x00ff, 0x00aa, // (, ), *, +
    0x1000, 0x0008, 0x0008, 0x0044, //, -, . , /
    0xff44, 0x0c00, 0x7788, 0x3f88, // 0, 1, 2, 3,
    0x9d88, 0xbb88, 0xfb88, 0x1f00, // 4, 5, 6, 7
    0xff88, 0xbf88, 0x0000, 0x0000, // 8, 9, ;, ;
    0x0014, 0x0000, 0x0041, 0x0728, // <, =, >, ?
    0x7fa0, 0xdf88, 0xfb8c, 0xf300, // @, A, B, C,
    0x3f22, 0xf388, 0xc388, 0xfb08, // D, E, F, G, 
    0xdd88, 0x3322, 0x7c00, 0xd194, // H, I, J, K,
    0xf100, 0xdd05, 0xdd11, 0xff00, // L, M, N, O,
    0xc788, 0xff10, 0xd798, 0xbb88, // P, Q, R, S,
    0x0322, 0xfd00, 0xc144, 0xdd50, // T, U, V, W,
    0x1155, 0x0125, 0x3344, 0xe300, // X, Y, Z, [,
    0x0011, 0x3e00, 0x0050, 0x2000, // \, ], ^, _,
    0x0001, 0xdf88, 0xfb8c, 0xf300, // `, a, b, c,
    0x3f22, 0xf388, 0xc388, 0xfb08, // d, e, f, g,
    0xdd88, 0x3322, 0x7c00, 0xd194, // h, i, j, k,
    0xf100, 0xdd05, 0xdd11, 0xff00, // l, m, n, o,
    0xc788, 0xff10, 0xd798, 0xbb88, // p, q, r, s,
    0x0322, 0xfd00, 0xc144, 0xdd50, // t, u, v, w,
    0x1155, 0x0125, 0x3344, 0x32a2, // x, y, z, {,
    0x0022, 0x212a, 0x8888          // |, }, ~                               
    };

static const uint16_t digits[10] = {
  0xff44, 0x0c00, 0x7788, 0x3f88, 0x9d88, // 0, 1, 2, 3, 4
  0xbb88, 0xfb88, 0x1f00, 0xff88, 0xbf88  // 5, 6, 7, 8, 9
  };

#define SPACE_SYM   0x0000
#define PLUS_SYM    0x00aa
#define MINIUS_SYM  0x0008
#define HASH_SYM    0xe0aa

typedef struct {
    spi_inst_t *spi;
    uint cs_pin;
    uint16_t display_buffer[LED14SEG_CHARACTER_COUNT];
    uint8_t dot_bitmap;
    uint8_t scan_position;
} led14seg_display_t;

void led14seg_init(led14seg_display_t *display, spi_inst_t *spi,
                   uint sck_pin, uint tx_pin, uint cs_pin);
void led14seg_set_voltage(led14seg_display_t *display, uint16_t value);
void led14seg_set_current(led14seg_display_t *display, uint16_t value);
void redraw_display(led14seg_display_t *display);
uint8_t normalize_range(uint8_t *start_pos, uint8_t *stop_pos);
void fill_range(led14seg_display_t *display, uint8_t start_pos, uint8_t stop_pos, uint16_t bitmap);
void write_error_field(led14seg_display_t *display, uint8_t start_pos, uint8_t stop_pos);
void write_string(led14seg_display_t *display, const char *str, uint8_t str_len, uint8_t start_pos, uint8_t stop_pos);
void write_float(led14seg_display_t *display, float value, uint8_t start_pos, uint8_t stop_pos, uint8_t int_size, uint8_t frac_size, uint8_t is_signed, uint8_t show_plus_sign, uint8_t draw_zeros);
void write_int(led14seg_display_t *display, int16_t value, uint8_t start_pos, uint8_t stop_pos, uint8_t is_signed, uint8_t show_plus_sign, uint8_t draw_zeros);
#endif
