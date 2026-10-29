/* Aurora v2 - 8259A PIC helpers. */
#ifndef AURORA_ARCH_PIC_H
#define AURORA_ARCH_PIC_H

#include <stdint.h>

void pic_remap(uint8_t master_offset, uint8_t slave_offset);
void pic_mask_all(void);
void pic_unmask(uint8_t irq);
void pic_send_eoi(uint8_t irq);

#endif /* AURORA_ARCH_PIC_H */
