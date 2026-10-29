/* Reclaimable, zero-on-allocation physical frames; serialized on the boot CPU. */
#include <arch/boot.h>
#include <arch/pmm.h>
#include <kernel/log.h>
#include <string.h>

#define PAGE_SIZE 4096ull
#define RESERVED_LOW (64ull * 1024ull * 1024ull)

static uint64_t g_hhdm;
#define MAX_FRAMES (1024u * 1024u) /* up to 4 GiB in the selected extent */
static uint8_t g_allocated[MAX_FRAMES / 8];
static uint64_t g_start;
static uint32_t g_count, g_hint, g_used;
static uint64_t g_end;

uint64_t pmm_hhdm_offset(void)
{
    return g_hhdm;
}

void pmm_init(void)
{
    g_hhdm = boot_hhdm_offset();
    const struct limine_memmap_response *response = boot_memmap();
    if (response == NULL) {
        kpanic("PMM: no Limine memory map");
    }

    uint64_t best_start = 0;
    uint64_t best_end = 0;
    for (uint64_t index = 0; index < response->entry_count; index++) {
        const struct limine_memmap_entry *entry = response->entries[index];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        uint64_t start = entry->base;
        uint64_t end = entry->base + entry->length;
        if (start < RESERVED_LOW) {
            start = RESERVED_LOW;
        }
        start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (end <= start || end - start < (16ull * 1024 * 1024)) {
            continue;
        }
        if (end - start > best_end - best_start) {
            best_end = end;
            best_start = start;
        }
    }

    if (best_end == 0) {
        kpanic("PMM: no usable memory above 64 MiB");
    }
    g_start = best_start;
    g_count = (uint32_t)((best_end - best_start) / PAGE_SIZE > MAX_FRAMES
                             ? MAX_FRAMES
                             : (best_end - best_start) / PAGE_SIZE);
    g_end = g_start + (uint64_t)g_count * PAGE_SIZE;
    g_hint = g_used = 0;
    memset(g_allocated, 0, sizeof(g_allocated));
    kprintf("[pmm] frames from %p to %p (%u MiB)\n", (void *)(uintptr_t)best_start,
            (void *)(uintptr_t)best_end, (unsigned)((best_end - best_start) >> 20));
}

uint64_t pmm_alloc_frame(void)
{
    if (g_used == g_count)
        return 0;
    for (uint32_t n = 0; n < g_count; n++) {
        uint32_t i = (g_hint + n) % g_count;
        uint8_t bit = (uint8_t)(1u << (i % 8));
        if (g_allocated[i / 8] & bit)
            continue;
        g_allocated[i / 8] |= bit;
        g_used++;
        g_hint = (i + 1) % g_count;
        uint64_t physical = g_start + (uint64_t)i * PAGE_SIZE;
        memset((void *)(uintptr_t)(g_hhdm + physical), 0, PAGE_SIZE);
        return physical;
    }
    return 0;
}

void pmm_free_frame(uint64_t physical)
{
    if (physical < g_start || physical >= g_end || (physical & 4095))
        kpanic("PMM: invalid free");
    uint32_t i = (uint32_t)((physical - g_start) / PAGE_SIZE);
    uint8_t bit = (uint8_t)(1u << (i % 8));
    if (!(g_allocated[i / 8] & bit))
        kpanic("PMM: double free");
    g_allocated[i / 8] &= (uint8_t)~bit;
    g_used--;
    if (i < g_hint)
        g_hint = i;
}

uint64_t pmm_free_bytes(void)
{
    return (uint64_t)(g_count - g_used) * PAGE_SIZE;
}
