#ifndef LED14SEG_H
#define LED14SEG_H
#include <stdint.h>
#include "hardware/spi.h"

#define LED14SEG_CHARACTER_COUNT 8u

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
void led14seg_refresh_next(led14seg_display_t *display);
#endif
