#ifndef OPENDPS_H
#define OPENDPS_H
#include <stdbool.h>
#include <stdint.h>
#include "hardware/uart.h"
typedef struct { uart_inst_t *uart; uint16_t voltage,voltage_set,current,current_set; bool output_enabled; } opendps_t;
void opendps_init(opendps_t *,uart_inst_t *,uint,uint);
void opendps_set_idle_hook(void (*hook)(void));
bool opendps_query(opendps_t *,uint32_t);
bool opendps_set_voltage(opendps_t *,uint16_t);
bool opendps_set_current(opendps_t *,uint16_t);
bool opendps_set_output(opendps_t *,bool);
bool opendps_is_cc(const opendps_t *);
#endif
