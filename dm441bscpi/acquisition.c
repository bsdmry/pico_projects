#include "dm441bscpi.h"
#include "display_decoder.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "pico/mutex.h"

auto_init_mutex(status_lock);
static uint64_t status;
static uint64_t display;
static uint16_t leds;
static dm_decoder_t decoder;

uint64_t dm_status(void) {
    mutex_enter_blocking(&status_lock);
    uint64_t value = status;
    mutex_exit(&status_lock);
    return value;
}
static void publish(void) {
    uint64_t value = display | leds;
    if (leds & (1u << 9)) value |= DM_NEGATIVE;
    mutex_enter_blocking(&status_lock);
    status = value;
    mutex_exit(&status_lock);
}
/* 74HC166 parallel load is synchronous: /PE low alone does not latch.
 * Use one rising clock with /PE low, then shift the loaded word over SPI.
 * GPIO CS pins must be wired to /PE; /CE low and /MR high externally. */
static uint16_t read_chain(spi_inst_t *spi, uint load, uint clock) {
    uint16_t word;
    gpio_set_function(clock, GPIO_FUNC_SIO);
    gpio_put(clock, 0);
    gpio_put(load, 0);
    busy_wait_us_32(1);
    gpio_put(clock, 1);
    busy_wait_us_32(1);
    gpio_put(clock, 0);
    gpio_put(load, 1);
    gpio_set_function(clock, GPIO_FUNC_SPI);
    spi_read16_blocking(spi, 0, &word, 1);
    return word;
}
static void strobe(uint gpio, uint32_t events) {
    if (gpio != 11 || !(events & GPIO_IRQ_EDGE_FALL)) return;
    uint16_t raw = read_chain(spi0, 5, 6);
    if (dm_decode(&decoder, raw, time_us_64(), &display)) publish();
}
static void init_chain(spi_inst_t *spi, uint miso, uint load, uint clock) {
    spi_init(spi, 1000000);
    spi_set_format(spi, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(miso, GPIO_FUNC_SPI);
    gpio_init(clock);
    gpio_set_dir(clock, GPIO_OUT);
    gpio_put(clock, 0);
    gpio_set_function(clock, GPIO_FUNC_SPI);
    gpio_init(load);
    gpio_set_dir(load, GPIO_OUT);
    gpio_put(load, 1);
}
void dm_core1(void) {
    init_chain(spi0, 4, 5, 6);
    init_chain(spi1, 8, 9, 10);
    gpio_init(11);
    gpio_set_dir(11, GPIO_IN);
    gpio_disable_pulls(11);
    gpio_init(12);
    gpio_set_dir(12, GPIO_IN);
    gpio_disable_pulls(12);
    gpio_set_irq_enabled_with_callback(11, GPIO_IRQ_EDGE_FALL, true, strobe);
    absolute_time_t next = get_absolute_time();
    for (;;) {
        if (time_reached(next)) {
            uint16_t raw = read_chain(spi1, 9, 10);
            /* Serialize publication with the Core1 GPIO ISR. SPI1 itself
             * stays interruptible so strobes can read SPI0 immediately. */
            uint32_t irq = save_and_disable_interrupts();
            leds = dm_decode_leds(raw);
            publish();
            restore_interrupts(irq);
            next = delayed_by_ms(get_absolute_time(), 10);
        }
        tight_loop_contents();
    }
}
