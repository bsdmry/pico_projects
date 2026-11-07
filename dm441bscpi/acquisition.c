#include "dm441bscpi.h"
#include "display_decoder.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/mutex.h"

auto_init_mutex(status_lock);
static uint64_t status;
static uint32_t raw_status;
static uint64_t display;
static uint16_t leds;
static dm_decoder_t decoder;

#define DIGIT_SYNC_GPIO 12
#define DIGIT_PERIOD_US 2000
#define DIGIT_MIDPOINT_US 1000
#define DISPLAY_DIGITS 5

static uint display_alarm;
static absolute_time_t sample_at;
static unsigned samples_remaining;

uint64_t dm_status(void) {
    mutex_enter_blocking(&status_lock);
    uint64_t value = status;
    mutex_exit(&status_lock);
    return value;
}
uint32_t dm_raw(void) {
    mutex_enter_blocking(&status_lock);
    uint32_t value = raw_status;
    mutex_exit(&status_lock);
    return value;
}
static void publish(void) {
    uint64_t value = display | leds;
    if (leds & (1u << 6)) value |= DM_NEGATIVE;
    mutex_enter_blocking(&status_lock);
    status = value;
    mutex_exit(&status_lock);
}
/* 74HC166 parallel load is synchronous: /PE low alone does not latch.
 * Use one rising clock with /PE low, then shift the loaded word over SPI.
 * GPIO CS pins must be wired to /PE; /CE low and /MR high externally. */
static uint16_t read_chain(spi_inst_t *spi, uint load, uint clock) {
    uint16_t word;
    if (spi == spi0) gpio_xor_mask(1u << DM_SAMPLE_LED_GPIO);
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
static void stop_display_sampling(void) {
    hardware_alarm_cancel(display_alarm);
    samples_remaining = 0;
    decoder = (dm_decoder_t){0};
}

static void schedule_display_sample(void) {
    if (hardware_alarm_set_target(display_alarm, sample_at)) {
        /* A missed deadline invalidates the scan. Wait for the next DIGIT_5
         * edge instead of taking catch-up reads near digit transitions. */
        stop_display_sampling();
    }
}

static void sample_display(uint alarm_num) {
    (void)alarm_num;
    if (!samples_remaining) return;
    int64_t lateness = absolute_time_diff_us(sample_at, get_absolute_time());
    if (lateness < 0) return;
    if (lateness >= DIGIT_MIDPOINT_US) {
        stop_display_sampling();
        return;
    }
    uint16_t raw = read_chain(spi0, 5, 6);
    mutex_enter_blocking(&status_lock);
    raw_status = ((uint32_t)raw << 16) | (raw_status & 0xffffu);
    mutex_exit(&status_lock);
    if (dm_decode(&decoder, raw, time_us_64(), &display)) publish();
    if (--samples_remaining) {
        /* Absolute deadlines keep SPI transaction time out of the period. */
        sample_at = delayed_by_us(sample_at, DIGIT_PERIOD_US);
        schedule_display_sample();
    }
}

static void digit_sync(uint gpio, uint32_t events) {
    if (gpio != DIGIT_SYNC_GPIO || !(events & GPIO_IRQ_EDGE_RISE)) return;
    absolute_time_t edge = get_absolute_time();
    stop_display_sampling();
    samples_remaining = DISPLAY_DIGITS;
    sample_at = delayed_by_us(edge, DIGIT_MIDPOINT_US);
    schedule_display_sample();
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
    gpio_init(DM_SAMPLE_LED_GPIO);
    gpio_put(DM_SAMPLE_LED_GPIO, 0);
    gpio_set_dir(DM_SAMPLE_LED_GPIO, GPIO_OUT);
    init_chain(spi0, 4, 5, 6);
    init_chain(spi1, 8, 9, 10);
    gpio_init(11);
    gpio_set_dir(11, GPIO_IN);
    gpio_disable_pulls(11);
    gpio_init(DIGIT_SYNC_GPIO);
    gpio_set_dir(DIGIT_SYNC_GPIO, GPIO_IN);
    gpio_disable_pulls(DIGIT_SYNC_GPIO);
    /* Claim and register on Core1 so the SPI0 timer IRQ also runs on Core1. */
    display_alarm = (uint)hardware_alarm_claim_unused(true);
    hardware_alarm_set_callback(display_alarm, sample_display);
    gpio_set_irq_enabled_with_callback(DIGIT_SYNC_GPIO, GPIO_IRQ_EDGE_RISE,
                                      true, digit_sync);
    absolute_time_t next = get_absolute_time();
    for (;;) {
        if (time_reached(next)) {
            uint16_t raw = read_chain(spi1, 9, 10);
            /* Serialize publication with the Core1 GPIO ISR. SPI1 itself
             * stays interruptible so the display timer can read SPI0 on schedule. */
            uint32_t irq = save_and_disable_interrupts();
            mutex_enter_blocking(&status_lock);
            raw_status = (raw_status & 0xffff0000u) | raw;
            mutex_exit(&status_lock);
            leds = dm_decode_leds(raw);
            publish();
            restore_interrupts(irq);
            next = delayed_by_ms(get_absolute_time(), 10);
        }
        tight_loop_contents();
    }
}
