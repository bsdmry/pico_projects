#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "dm441bscpi.h"

#define WIFI_RETRY_MS 30000

static void wifi_connect(void) {
    /* Reset any failed/pending join. Asynchronous association and DHCP let
     * Core0 continue polling lwIP throughout the retry window. */
    cyw43_arch_disable_sta_mode();
    cyw43_arch_enable_sta_mode();
    int error = cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD,
                                             CYW43_AUTH_WPA2_AES_PSK);
    if (error) printf("Wi-Fi join could not start (%d); retry in 30 seconds\n", error);
    else printf("Wi-Fi connecting; retry in 30 seconds if still disconnected\n");
}

int main(void) {
    stdio_init_all();
    multicore_launch_core1(dm_core1);
    if (cyw43_arch_init()) { printf("Wi-Fi initialization failed\n"); return 1; }
    /* Bind once to all local addresses; keep the listener across Wi-Fi joins. */
    if (!scpi_start()) { printf("SCPI listener failed\n"); return 1; }
    wifi_connect();
    absolute_time_t retry_at = make_timeout_time_ms(WIFI_RETRY_MS);
    bool was_connected = false;
    for (;;) {
        cyw43_arch_poll();
        int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (link == CYW43_LINK_UP) {
            if (!was_connected) printf("Wi-Fi connected\n");
            was_connected = true;
        } else {
            if (was_connected) {
                printf("Wi-Fi disconnected (status %d); retry in 30 seconds\n", link);
                scpi_disconnect();
                retry_at = make_timeout_time_ms(WIFI_RETRY_MS);
                was_connected = false;
            }
            if (time_reached(retry_at)) {
                scpi_disconnect();
                wifi_connect();
                retry_at = make_timeout_time_ms(WIFI_RETRY_MS);
            }
        }
        sleep_ms(1);
    }
}
