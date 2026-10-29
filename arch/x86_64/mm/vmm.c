/* Aurora v2 - 4-level page tables for user processes.
 *
 * Limine already provides kernel high-half mappings and the HHDM. A process
 * address space copies PML4 entries 256..511 from the kernel and builds the
 * low canonical half itself. This keeps kernel mappings identical across
 * address spaces while every process gets isolated user pages.
 */
#include <arch/boot.h>
#include <arch/io.h>
#include <arch/pmm.h>
#include <arch/vmm.h>
#include <kernel/log.h>
#include <string.h>

#define PAGE_SIZE 4096ull
#define ADDRESS_MASK 0x000FFFFFFFFFF000ull
#define KERNEL_CR3_PRESENT_INDEX 256u
#define EFER_MSR 0xC0000080u
#define EFER_NXE (1u << 11)
#define CR4_LA57 (1ull << 12)

static uint64_t g_kernel_cr3;
static uint64_t g_hhdm;

static uint64_t *table(uint64_t physical)
{
    return (uint64_t *)(uintptr_t)(g_hhdm + physical);
}

void vmm_init(void)
{
    g_kernel_cr3 = read_cr3() & ADDRESS_MASK;
    g_hhdm = boot_hhdm_offset();

    if ((read_cr4() & CR4_LA57) != 0) {
        kpanic("VMM: Limine was booted with 5-level paging; "
               "set paging_mode: 4level");
    }

    uint64_t efer = read_msr(EFER_MSR);
    write_msr(EFER_MSR, efer | EFER_NXE);

    kprintf("[vmm] kernel CR3=%p, HHDM=%p, NX enabled\n", (void *)(uintptr_t)g_kernel_cr3,
            (void *)(uintptr_t)g_hhdm);
}

bool vmm_create_space(address_space_t *space)
{
    if (space == NULL) {
        return false;
    }
    uint64_t pml4_phys = pmm_alloc_frame();
    if (!pml4_phys)
        return false;
    uint64_t *pml4 = table(pml4_phys);
    uint64_t *kernel_pml4 = table(g_kernel_cr3);

    for (uint32_t index = KERNEL_CR3_PRESENT_INDEX; index < 512; index++) {
        pml4[index] = kernel_pml4[index];
    }
    /* Low half starts empty and is owned by user mappings alone. */
    for (uint32_t index = 0; index < KERNEL_CR3_PRESENT_INDEX; index++) {
        pml4[index] = 0;
    }

    space->pml4_phys = pml4_phys;
    space->pml4 = pml4;
    return true;
}

static uint64_t *ensure_table(uint64_t *entry, uint64_t flags)
{
    if ((*entry & VMM_PRESENT) == 0) {
        uint64_t physical = pmm_alloc_frame();
        if (!physical)
            return NULL;
        *entry = physical | flags;
        return table(physical);
    }
    if (*entry & (1ull << 7))
        return NULL; /* no huge-page descent */
    return table(*entry & ADDRESS_MASK);
}

bool vmm_map_kernel_mmio(uint64_t virtual_address, uint64_t physical_address, uint64_t size)
{
    if (size == 0 || (virtual_address & (PAGE_SIZE - 1)) != 0 ||
        (physical_address & (PAGE_SIZE - 1)) != 0 || virtual_address + size < virtual_address) {
        return false;
    }

    uint64_t *pml4 = table(g_kernel_cr3);
    uint64_t end = virtual_address + size;
    for (uint64_t address = virtual_address; address < end;
         address += PAGE_SIZE, physical_address += PAGE_SIZE) {
        uint32_t pml4_index = (uint32_t)((address >> 39) & 0x1FF);
        uint32_t pdpt_index = (uint32_t)((address >> 30) & 0x1FF);
        uint32_t pd_index = (uint32_t)((address >> 21) & 0x1FF);
        uint32_t pt_index = (uint32_t)((address >> 12) & 0x1FF);

        uint64_t parent_flags = VMM_PRESENT | VMM_WRITE;
        uint64_t *pdpt = ensure_table(&pml4[pml4_index], parent_flags);
        if (!pdpt)
            return false;
        uint64_t *pd = ensure_table(&pdpt[pdpt_index], parent_flags);
        if (!pd)
            return false;
        uint64_t *pt = ensure_table(&pd[pd_index], parent_flags);
        if (!pt)
            return false;
        pt[pt_index] =
            (physical_address & ADDRESS_MASK) | VMM_PRESENT | VMM_WRITE | VMM_PCD | VMM_NX;
        invlpg(address);
    }
    return true;
}

bool vmm_map_range(address_space_t *space, uint64_t virtual_address, uint64_t physical_address,
                   uint64_t size, uint64_t flags)
{
    if (space == NULL || virtual_address >= (1ull << 47) || size > (1ull << 47) - virtual_address ||
        (size & (PAGE_SIZE - 1)) || size == 0 || (virtual_address & (PAGE_SIZE - 1)) ||
        (physical_address & (PAGE_SIZE - 1)) || virtual_address + size < virtual_address) {
        return false;
    }

    uint64_t end = virtual_address + size;
    for (uint64_t address = virtual_address; address < end;
         address += PAGE_SIZE, physical_address += PAGE_SIZE) {
        uint32_t pml4_index = (uint32_t)((address >> 39) & 0x1FF);
        uint32_t pdpt_index = (uint32_t)((address >> 30) & 0x1FF);
        uint32_t pd_index = (uint32_t)((address >> 21) & 0x1FF);
        uint32_t pt_index = (uint32_t)((address >> 12) & 0x1FF);

        uint64_t parent_flags = VMM_PRESENT | VMM_WRITE | VMM_USER;
        uint64_t *pdpt = ensure_table(&space->pml4[pml4_index], parent_flags);
        if (!pdpt)
            return false;
        uint64_t *pd = ensure_table(&pdpt[pdpt_index], parent_flags);
        if (!pd)
            return false;
        uint64_t *pt = ensure_table(&pd[pd_index], parent_flags);
        if (!pt)
            return false;

        if (pt[pt_index] & VMM_PRESENT)
            return false;
        pt[pt_index] = (physical_address & ADDRESS_MASK) | flags;
        invlpg(address);
    }
    return true;
}

static bool translate(const address_space_t *space, uint64_t address, uint64_t *physical,
                      bool *writable)
{
    if (address >= (1ull << 47)) {
        return false;
    }
    uint32_t pml4_index = (uint32_t)((address >> 39) & 0x1FF);
    bool parents_writable = true;
    uint64_t entry = space->pml4[pml4_index];
    if ((entry & VMM_PRESENT) == 0 || (entry & VMM_USER) == 0) {
        return false;
    }
    parents_writable &= (entry & VMM_WRITE) != 0;
    uint64_t *pdpt = table(entry & ADDRESS_MASK);
    uint32_t pdpt_index = (uint32_t)((address >> 30) & 0x1FF);
    entry = pdpt[pdpt_index];
    if ((entry & VMM_PRESENT) == 0 || (entry & VMM_USER) == 0) {
        return false;
    }
    if (entry & (1ull << 7))
        return false;
    parents_writable &= (entry & VMM_WRITE) != 0;
    uint64_t *pd = table(entry & ADDRESS_MASK);
    uint32_t pd_index = (uint32_t)((address >> 21) & 0x1FF);
    entry = pd[pd_index];
    if ((entry & VMM_PRESENT) == 0 || (entry & VMM_USER) == 0) {
        return false;
    }
    if (entry & (1ull << 7))
        return false;
    parents_writable &= (entry & VMM_WRITE) != 0;
    uint64_t *pt = table(entry & ADDRESS_MASK);
    uint32_t pt_index = (uint32_t)((address >> 12) & 0x1FF);
    entry = pt[pt_index];
    if ((entry & VMM_PRESENT) == 0 || (entry & VMM_USER) == 0) {
        return false;
    }
    *physical = (entry & ADDRESS_MASK) | (address & (PAGE_SIZE - 1));
    *writable = parents_writable && (entry & VMM_WRITE) != 0;
    return true;
}

bool vmm_user_range(const address_space_t *space, uint64_t address, uint64_t size, bool write)
{
    if (space == NULL || space->pml4 == NULL || size == 0 || address >= (1ull << 47) ||
        size > (1ull << 47) - address) {
        return false;
    }
    uint64_t last = address + size - 1;
    uint64_t page = address & ~(PAGE_SIZE - 1);

    while (page <= last) {
        uint64_t physical;
        bool writable;
        if (!translate(space, page, &physical, &writable) || (write && !writable)) {
            return false;
        }
        if (page + PAGE_SIZE < page) {
            return false;
        }
        page += PAGE_SIZE;
    }
    return true;
}

void vmm_switch(const address_space_t *space)
{
    write_cr3(space != NULL ? space->pml4_phys : g_kernel_cr3);
}

void vmm_switch_kernel(void)
{
    write_cr3(g_kernel_cr3);
}

uint64_t vmm_hhdm(void)
{
    return g_hhdm;
}

void *vmm_phys_to_virt(uint64_t physical)
{
    return (void *)(uintptr_t)(g_hhdm + physical);
}

/* Software-owned bit excludes MMIO mappings from physical frame reclamation. */
static void destroy_table(uint64_t physical, unsigned level)
{
    uint64_t *entries = table(physical);
    unsigned count = level == 4 ? 256 : 512;
    for (unsigned i = 0; i < count; i++) {
        uint64_t entry = entries[i];
        if (!(entry & VMM_PRESENT))
            continue;
        if (level > 1)
            destroy_table(entry & ADDRESS_MASK, level - 1);
        else if (entry & VMM_OWNED)
            pmm_free_frame(entry & ADDRESS_MASK);
    }
    pmm_free_frame(physical);
}

void vmm_destroy_space(address_space_t *space)
{
    if (space == NULL || space->pml4_phys == 0)
        return;
    destroy_table(space->pml4_phys, 4);
    memset(space, 0, sizeof(*space));
}

bool vmm_copy_to_space(const address_space_t *space,uint64_t destination,const void *source,uint64_t size)
{
    if(!size)return true;
    if(!vmm_user_range(space,destination,size,true))return false;
    const uint8_t *in=source;
    while(size) {
        uint64_t physical;bool writable;
        if(!translate(space,destination,&physical,&writable) || !writable)return false;
        uint64_t chunk=4096-(destination&4095);if(chunk>size)chunk=size;
        memcpy(vmm_phys_to_virt(physical),in,(size_t)chunk);
        destination+=chunk;in+=chunk;size-=chunk;
    }
    return true;
}
