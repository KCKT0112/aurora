/* Aurora v2 - physical frame allocator. */
#ifndef AURORA_ARCH_PMM_H
#define AURORA_ARCH_PMM_H

#include <stdint.h>

void pmm_init(void);
uint64_t pmm_alloc_frame(void);
void pmm_free_frame(uint64_t physical);
uint64_t pmm_free_bytes(void);
uint64_t pmm_hhdm_offset(void);

#endif /* AURORA_ARCH_PMM_H */
