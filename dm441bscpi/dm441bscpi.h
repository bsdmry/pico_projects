#ifndef DM441BSCPI_H
#define DM441BSCPI_H
#include <stdbool.h>
#include <stdint.h>
/* LED bits follow the MSB-first physical word, active levels normalized.
 * Packed status: LEDs[15:0], magnitude[58:16], over[59], under[60],
 * DP[62:61] (0=four fractional digits ... 3=one fractional digit), negative[63]. */
#define DM_UNDER (UINT64_C(1) << 60)
#define DM_OVER (UINT64_C(1) << 59)
#define DM_NEGATIVE (UINT64_C(1) << 63)
/* External sampling LED / logic-analyzer output on GPIO13. */
#define DM_SAMPLE_LED_GPIO 13
uint64_t dm_status(void);
/* Latest raw words: SPI0[31:16], SPI1[15:0], before normalization. */
uint32_t dm_raw(void);
void dm_core1(void);
bool scpi_start(void);
void scpi_disconnect(void);
#endif
