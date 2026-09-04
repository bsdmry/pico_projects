#ifndef WIRECTL_H
#define WIRECTL_H
#include <stdbool.h>
#include <stdint.h>
#ifndef WIRECTL_SPI0_SCK
#define WIRECTL_SPI0_SCK 18
#endif
#ifndef WIRECTL_SPI0_TX
#define WIRECTL_SPI0_TX 19
#endif
#ifndef WIRECTL_SPI0_CS
#define WIRECTL_SPI0_CS 17
#endif
#ifndef WIRECTL_SPI1_SCK
#define WIRECTL_SPI1_SCK 10
#endif
#ifndef WIRECTL_SPI1_TX
#define WIRECTL_SPI1_TX 11
#endif
#ifndef WIRECTL_SPI1_CS
#define WIRECTL_SPI1_CS 13
#endif
#ifndef WIRECTL_UART0_TX
#define WIRECTL_UART0_TX 0
#endif
#ifndef WIRECTL_UART0_RX
#define WIRECTL_UART0_RX 1
#endif
#ifndef WIRECTL_UART1_TX
#define WIRECTL_UART1_TX 4
#endif
#ifndef WIRECTL_UART1_RX
#define WIRECTL_UART1_RX 5
#endif
#define WIRECTL_VOLTAGE_MIN 1u
#define WIRECTL_VOLTAGE_MAX 2000u
#define WIRECTL_CURRENT_MIN 5u
#define WIRECTL_CURRENT_MAX 3000u
#define LED1 28
#define LED2 27
#define LED3 26
#define LED4 22
#define CORE1_MESSAGE(command, value) ((((uint32_t)(command)) << 24) | ((value) & 0xffffffu))

typedef struct {
    uint16_t voltage[2];
    uint16_t voltage_lim[2];
    uint16_t current[2];
    uint16_t current_lim[2];
    uint8_t current_lim_trg[2];
    uint8_t output_en[2];
    bool locked[2];
} scpi_registers_t;

enum intercore_command { 
	CORE1_SET_INT1_VOLTAGE_LIM = 1, //Core0 sends Core1 voltage limit on INT1
	CORE1_GET_INT1_VOLTAGE_LIM = 2, //Core0 requests Core1 voltage limit on INT1
	CORE0_DAT_INT1_VOLTAGE_LIM = 3, //Core1 responds Core0 voltage limit on INT1
	CORE1_GET_INT1_VOLTAGE = 4, 	//Core0 requests Core1 actual voltage on INT1
	CORE0_DAT_INT1_VOLTAGE = 5, 	//Core1 responds Core0 actual voltage on INT1

	CORE1_SET_INT2_VOLTAGE_LIM = 6,	//Core0 sends Core1 voltage limit on INT2
	CORE1_GET_INT2_VOLTAGE_LIM = 7, //Core0 requests Core1 voltage limit on INT2
	CORE0_DAT_INT2_VOLTAGE_LIM = 8, //Core1 responds Core0 voltage limit on INT2
	CORE1_GET_INT2_VOLTAGE = 9, 	//Core0 requests Core1 actual voltage on INT2
	CORE0_DAT_INT2_VOLTAGE = 10, 	//Core1 responds Core0 actual voltage on INT2

	CORE1_SET_INT1_CURRENT_LIM = 11,//Core0 sends Core1 current limit on INT1 
	CORE1_GET_INT1_CURRENT_LIM = 12,//Core0 requests Core1 current limit on INT1 
	CORE0_DAT_INT1_CURRENT_LIM = 13,//Core1 responds Core0 current limit on INT1 
	CORE1_GET_INT1_CURRENT = 14, 	//Core0 requests Core1 actual current on INT1
	CORE0_DAT_INT1_CURRENT = 15, 	//Core1 responds Core0 actual current on INT1

	CORE1_SET_INT2_CURRENT_LIM = 16,//Core0 sends Core1 current limit on INT2 
	CORE1_GET_INT2_CURRENT_LIM = 17,//Core0 requests Core1 current limit on INT2
	CORE0_DAT_INT2_CURRENT_LIM = 18,//Core1 responds Core0 current limit on INT2
	CORE1_GET_INT2_CURRENT = 19, 	//Core0 requests Core1 actual current on INT2
	CORE0_DAT_INT2_CURRENT = 20,  	//Core1 responds Core0 actual current on INT2

	CORE1_SET_INT1_OUTPUT = 21, 	//Core0 sends Core1 output state for INT1
	CORE1_GET_INT1_OUTPUT = 22, 	//Core0 requests Core1 output state for INT1
	CORE0_DAT_INT1_OUTPUT = 23, 	//Core1 responds Core0 output state for INT1

	CORE1_SET_INT2_OUTPUT = 24,		//Core0 sends Core1 output state for INT2 
	CORE1_GET_INT2_OUTPUT = 25, 	//Core0 requests Core1 output state for INT2
	CORE0_DAT_INT2_OUTPUT = 26, 	//Core1 responds Core0 output state for INT2

	CORE1_SET_INT1_LOCK = 27,
	CORE1_GET_INT1_LOCK = 28,
	CORE0_DAT_INT1_LOCK = 29,
	CORE1_SET_INT2_LOCK = 30,
	CORE1_GET_INT2_LOCK = 31,
	CORE0_DAT_INT2_LOCK = 32,

	CORE0_ERR_INTERFACE_LOCKED = 33
 };

void intercore_send(enum intercore_command command, uint32_t value);
bool intercore_core0_try_pop(uint32_t *message);
void run_scpi_tcp_server(void);
#endif
