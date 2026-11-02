#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "dm441bscpi.h"
int main(void) {
    stdio_init_all();
    multicore_launch_core1(dm_core1);
    if (cyw43_arch_init()) { printf("Wi-Fi initialization failed\n"); return 1; }
    cyw43_arch_enable_sta_mode();
    for (;;) {
        if (!cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                CYW43_AUTH_WPA2_AES_PSK, 30000)) break;
        printf("Wi-Fi connection failed; retrying\n");
        sleep_ms(5000);
    }
    if (!scpi_start()) { printf("SCPI listener failed\n"); return 1; }
    for (;;) { cyw43_arch_poll(); sleep_ms(1); }
}
