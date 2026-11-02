#ifndef DM441BSCPI_H
#define DM441BSCPI_H
#include <stdbool.h>
#include <stdint.h>
/* Packed status: LEDs[15:0], magnitude[58:16], over[59], under[60],
 * DP[62:61] (0=after DIGIT_1 ... 3=after DIGIT_4), negative[63]. */
#define DM_UNDER (UINT64_C(1) << 60)
#define DM_OVER (UINT64_C(1) << 59)
#define DM_NEGATIVE (UINT64_C(1) << 63)
uint64_t dm_status(void);
void dm_core1(void);
bool scpi_start(void);
#endif
