#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "led14seg.h"
#include "wirectl.h"

static volatile uint32_t core1_queue[16];
static volatile uint8_t core1_queue_read, core1_queue_write;
static volatile uint32_t core0_queue[16];
static volatile uint8_t core0_queue_read, core0_queue_write;
static volatile scpi_registers_t scpi_regs;
static led14seg_display_t displays[2];

static void core1_sio_irq(void) {
    while (multicore_fifo_rvalid()) {
        uint8_t next = (uint8_t)((core1_queue_write + 1u) & 15u);
        uint32_t message = multicore_fifo_pop_blocking();
        if (next != core1_queue_read) {
            core1_queue[core1_queue_write] = message;
            core1_queue_write = next;
        }
    }
    multicore_fifo_clear_irq();
}

static void core0_sio_irq(void) {
    while (multicore_fifo_rvalid()) {
        uint8_t next = (uint8_t)((core0_queue_write + 1u) & 15u);
        uint32_t message = multicore_fifo_pop_blocking();
        if (next != core0_queue_read) {
            core0_queue[core0_queue_write] = message;
            core0_queue_write = next;
        }
    }
    multicore_fifo_clear_irq();
}

void intercore_send(enum intercore_command command, uint32_t value) {
    multicore_fifo_push_blocking(CORE1_MESSAGE(command, value));
}

bool intercore_core0_try_pop(uint32_t *message) {
    if (core0_queue_read == core0_queue_write) return false;
    *message = core0_queue[core0_queue_read];
    core0_queue_read = (uint8_t)((core0_queue_read + 1u) & 15u);
    return true;
}

static void core1_reply(enum intercore_command command, uint32_t value) {
    multicore_fifo_push_blocking(CORE1_MESSAGE(command, value));
}

static void core1_parse_msg(void){
	uint32_t message = core1_queue[core1_queue_read];
    uint8_t command = (uint8_t)(message >> 24);
    uint16_t value = (uint16_t)message;
    char report[24];
    int length;
    core1_queue_read = (uint8_t)((core1_queue_read + 1u) & 15u);
    length = snprintf(report, sizeof(report), "%u\r\n", value);
    if (command == CORE1_SET_INT1_VOLTAGE_LIM || command == CORE1_SET_INT2_VOLTAGE_LIM) {
        unsigned index = command == CORE1_SET_INT1_VOLTAGE_LIM ? 0u : 1u;
        if (scpi_regs.locked[index]) {
            core1_reply(CORE0_ERR_INTERFACE_LOCKED, index + 1u);
        } else {
            scpi_regs.voltage_lim[index] = value;
            uart_write_blocking(index == 0u ? uart0 : uart1,
                                (const uint8_t *)report, (size_t)length);
            led14seg_set_voltage(&displays[index], value);
        }
    } else if (command == CORE1_GET_INT1_VOLTAGE_LIM) {
        core1_reply(CORE0_DAT_INT1_VOLTAGE_LIM, scpi_regs.voltage_lim[0]);
    } else if (command == CORE1_GET_INT2_VOLTAGE_LIM) {
        core1_reply(CORE0_DAT_INT2_VOLTAGE_LIM, scpi_regs.voltage_lim[1]);
    } else if (command == CORE1_GET_INT1_VOLTAGE) {
        core1_reply(CORE0_DAT_INT1_VOLTAGE, scpi_regs.voltage[0]);
    } else if (command == CORE1_GET_INT2_VOLTAGE) {
        core1_reply(CORE0_DAT_INT2_VOLTAGE, scpi_regs.voltage[1]);
    } else if (command == CORE1_SET_INT1_CURRENT_LIM || command == CORE1_SET_INT2_CURRENT_LIM) {
        unsigned index = command == CORE1_SET_INT1_CURRENT_LIM ? 0u : 1u;
        if (scpi_regs.locked[index]) core1_reply(CORE0_ERR_INTERFACE_LOCKED, index + 1u);
        else scpi_regs.current_lim[index] = value;
    } else if (command == CORE1_GET_INT1_CURRENT_LIM) {
        core1_reply(CORE0_DAT_INT1_CURRENT_LIM, scpi_regs.current_lim[0]);
    } else if (command == CORE1_GET_INT2_CURRENT_LIM) {
        core1_reply(CORE0_DAT_INT2_CURRENT_LIM, scpi_regs.current_lim[1]);
    } else if (command == CORE1_GET_INT1_CURRENT) {
        core1_reply(CORE0_DAT_INT1_CURRENT, scpi_regs.current[0]);
    } else if (command == CORE1_GET_INT2_CURRENT) {
        core1_reply(CORE0_DAT_INT2_CURRENT, scpi_regs.current[1]);
    } else if (command == CORE1_SET_INT1_OUTPUT || command == CORE1_SET_INT2_OUTPUT) {
        unsigned index = command == CORE1_SET_INT1_OUTPUT ? 0u : 1u;
        if (scpi_regs.locked[index]) core1_reply(CORE0_ERR_INTERFACE_LOCKED, index + 1u);
        else scpi_regs.output_en[index] = value != 0u;
    } else if (command == CORE1_GET_INT1_OUTPUT) {
        core1_reply(CORE0_DAT_INT1_OUTPUT, scpi_regs.output_en[0]);
    } else if (command == CORE1_GET_INT2_OUTPUT) {
        core1_reply(CORE0_DAT_INT2_OUTPUT, scpi_regs.output_en[1]);
    } else if (command == CORE1_SET_INT1_LOCK || command == CORE1_SET_INT2_LOCK) {
        unsigned index = command == CORE1_SET_INT1_LOCK ? 0u : 1u;
        scpi_regs.locked[index] = value != 0u;
    } else if (command == CORE1_GET_INT1_LOCK) {
        core1_reply(CORE0_DAT_INT1_LOCK, scpi_regs.locked[0]);
    } else if (command == CORE1_GET_INT2_LOCK) {
        core1_reply(CORE0_DAT_INT2_LOCK, scpi_regs.locked[1]);
    }
}

static void core1_main(void) {
    uart_init(uart0, 115200);
    gpio_set_function(WIRECTL_UART0_TX, GPIO_FUNC_UART);
    gpio_set_function(WIRECTL_UART0_RX, GPIO_FUNC_UART);
    uart_init(uart1, 115200);
    gpio_set_function(WIRECTL_UART1_TX, GPIO_FUNC_UART);
    gpio_set_function(WIRECTL_UART1_RX, GPIO_FUNC_UART);
    led14seg_init(&displays[0], spi0, WIRECTL_SPI0_SCK, WIRECTL_SPI0_TX,
                  WIRECTL_SPI0_CS);
    led14seg_init(&displays[1], spi1, WIRECTL_SPI1_SCK, WIRECTL_SPI1_TX,
                  WIRECTL_SPI1_CS);
    led14seg_set_voltage(&displays[0], 0);
    led14seg_set_voltage(&displays[1], 0);
    multicore_fifo_clear_irq();
    irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(1), core1_sio_irq);
    irq_set_enabled(SIO_FIFO_IRQ_NUM(1), true);
    for (;;) {
        while (core1_queue_read != core1_queue_write) {
			core1_parse_msg();
        }
        /* Each call feeds one multiplexed digit. At 500 us per pass every
         * digit on both displays is refreshed at 250 Hz. */
        led14seg_refresh_next(&displays[0]);
        led14seg_refresh_next(&displays[1]);
        sleep_us(500);
    }
}

int main(void) {
    stdio_init_all();
    gpio_init(LED1); gpio_set_dir(LED1, GPIO_OUT); gpio_put(LED1, 0);
    gpio_init(LED2); gpio_set_dir(LED2, GPIO_OUT); gpio_put(LED2, 0);
    gpio_init(LED3); gpio_set_dir(LED3, GPIO_OUT); gpio_put(LED3, 0);
    gpio_init(LED4); gpio_set_dir(LED4, GPIO_OUT); gpio_put(LED4, 0);

    multicore_launch_core1(core1_main);
	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_FIFO_IRQ_NUM(0), core0_sio_irq);
	irq_set_enabled(SIO_FIFO_IRQ_NUM(0), true);
	gpio_put(LED1, 1);
    if (cyw43_arch_init()) {
        printf("failed to initialise Wi-Fi\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
		gpio_put(LED3, 1);
        printf("failed to connect\n");
        cyw43_arch_deinit();
        return 1;
    }
	gpio_put(LED2, 1);
    printf("Connected\n");
    run_scpi_tcp_server();
    cyw43_arch_deinit();
    return 0;
}
