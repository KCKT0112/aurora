/* Aurora v2 - 4-level user paging on top of Limine's kernel mappings. */
#ifndef AURORA_ARCH_VMM_H
#define AURORA_ARCH_VMM_H

#include <stdbool.h>
#include <stdint.h>

#define VMM_PRESENT (1ull << 0)
#define VMM_WRITE   (1ull << 1)
#define VMM_USER    (1ull << 2)
#define VMM_PCD     (1ull << 4)
#define VMM_OWNED (1ull << 9)
#define VMM_NX      (1ull << 63)

typedef struct {
    uint64_t pml4_phys;
    uint64_t *pml4;
} address_space_t;

void vmm_init(void);
bool vmm_create_space(address_space_t *space);
bool vmm_map_range(address_space_t *space, uint64_t virtual_address,
                   uint64_t physical_address, uint64_t size,
                   uint64_t flags);
bool vmm_map_kernel_mmio(uint64_t virtual_address, uint64_t physical_address,
                         uint64_t size);
bool vmm_user_range(const address_space_t *space, uint64_t address,
                    uint64_t size, bool write);
bool vmm_copy_to_space(const address_space_t *space,uint64_t destination,const void *source,uint64_t size);
void vmm_switch(const address_space_t *space);
void vmm_switch_kernel(void);
void vmm_destroy_space(address_space_t *space);
uint64_t vmm_hhdm(void);
void *vmm_phys_to_virt(uint64_t physical);

#endif /* AURORA_ARCH_VMM_H */
