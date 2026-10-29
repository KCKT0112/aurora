/* Aurora v2 - 8253/8254 PIT timer. */
#ifndef AURORA_ARCH_PIT_H
#define AURORA_ARCH_PIT_H

#include <stdint.h>

#define AURORA_TICK_HZ 100u

void pit_init(void);
uint64_t pit_on_tick(void);
uint64_t pit_ticks(void);

#endif /* AURORA_ARCH_PIT_H */
