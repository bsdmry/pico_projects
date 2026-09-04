#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "led14seg.h"

/* Ported from stm32-maple/led16seg_test. */
static const uint16_t syms[95] = {
0x0000,0x1400,0x8002,0xe0aa,0xbbaa,0x1144,0xe192,0x0100,0x1014,0x0141,0x00ff,0x00aa,0x1000,0x0008,0x0008,0x0044,
0xff44,0x0c00,0x7788,0x3f88,0x9d88,0xbb88,0xfb88,0x1f00,0xff88,0xbf88,0x0000,0x0000,0x0014,0x0000,0x0041,0x0728,
0x7fa0,0xdf88,0xfb8c,0xf300,0x3f22,0xf388,0xc388,0xfb08,0xdd88,0xffff,0x7c00,0xd194,0xf100,0xdd05,0xdd11,0xff00,
0xc788,0xffff,0xd798,0xbb88,0x0322,0xfd00,0xc144,0xdd50,0x1155,0x0125,0x3344,0xe300,0x0011,0x3e00,0x0050,0x2000,
0x0001,0xdf88,0xfb8c,0xf300,0x3f22,0xf388,0xc388,0xfb08,0xdd88,0xffff,0x7c00,0xd194,0xf100,0xdd05,0xdd11,0xff00,
0xc788,0xffff,0xd798,0xbb88,0x0322,0xfd00,0xc144,0xdd50,0x1155,0x0125,0x3344,0x32a2,0x0022,0x212a,0x8888};

static uint16_t bitmap(char ch) {
    unsigned c = (unsigned char)ch;
    return c >= 32u && c <= 126u ? syms[c - 32u] : 0x0728;
}
static void send_char(spi_inst_t *spi, uint cs, uint16_t segments, bool dot, uint8_t position) {
    uint8_t bytes[4] = {(uint8_t)((dot ? 1u : 0u) ^ 0xffu),
        (uint8_t)((segments & 0xffu) ^ 0xffu), (uint8_t)((segments >> 8) ^ 0xffu), position};
    gpio_put(cs, 0); spi_write_blocking(spi, bytes, 4); gpio_put(cs, 1);
}
void led14seg_init(led14seg_display_t *display, spi_inst_t *spi,
                   uint sck, uint tx, uint cs) {
    spi_init(spi, 2250000u);
    spi_set_format(spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(sck, GPIO_FUNC_SPI); gpio_set_function(tx, GPIO_FUNC_SPI);
    gpio_init(cs); gpio_set_dir(cs, GPIO_OUT); gpio_put(cs, 1);
    display->spi = spi;
    display->cs_pin = cs;
    display->dot_bitmap = 0;
    display->scan_position = 0;
    for (unsigned i = 0; i < LED14SEG_CHARACTER_COUNT; ++i)
        display->display_buffer[i] = bitmap(' ');
}

void led14seg_set_voltage(led14seg_display_t *display, uint16_t value) {
    char text[9];
    snprintf(text, sizeof(text), "%3u%02u V", value / 100u, value % 100u);
    for (unsigned i = 0; i < LED14SEG_CHARACTER_COUNT; ++i)
        display->display_buffer[i] = bitmap(text[i]);
    display->dot_bitmap = (uint8_t)(1u << 2);
}

void led14seg_refresh_next(led14seg_display_t *display) {
    uint8_t position = display->scan_position;
    send_char(display->spi, display->cs_pin,
              display->display_buffer[position],
              ((display->dot_bitmap >> position) & 1u) != 0u,
              (uint8_t)(0x80u >> position));
    display->scan_position = (uint8_t)((position + 1u) & 7u);
}
