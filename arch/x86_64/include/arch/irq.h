/* Aurora v2 - legacy 8259/PIT/PS-2 IRQ setup. */
#ifndef AURORA_ARCH_IRQ_H
#define AURORA_ARCH_IRQ_H

#include <stdint.h>

void irq_init(void);
void irq_handle(uint32_t irq);

#endif /* AURORA_ARCH_IRQ_H */
