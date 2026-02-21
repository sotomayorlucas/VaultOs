#ifndef VAULTOS_PIT_H
#define VAULTOS_PIT_H

#include "../../lib/types.h"

#define PIT_CHANNEL0  0x40
#define PIT_CMD       0x43
#define PIT_FREQUENCY 1193182  /* Base oscillator frequency (Hz) */

void     pit_init(uint32_t target_hz);
uint64_t pit_get_ticks(void);
uint64_t pit_get_uptime_ms(void);

#endif /* VAULTOS_PIT_H */
