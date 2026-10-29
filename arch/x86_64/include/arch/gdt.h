/* Aurora v2 - x86_64 GDT and TSS. */
#ifndef AURORA_ARCH_GDT_H
#define AURORA_ARCH_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x1B
#define GDT_USER_DATA   0x23
#define GDT_TSS         0x28

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

#endif /* AURORA_ARCH_GDT_H */
