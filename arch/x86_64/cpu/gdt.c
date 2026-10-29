/* Aurora v2 - 64-bit GDT, TSS and stack switching entry. */
#include <arch/gdt.h>
#include <arch/io.h>
#include <kernel/log.h>
#include <string.h>

extern void gdt_load(const void *pointer);
extern void tss_load(uint16_t selector);

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t flags;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t flags;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdt_table {
    struct gdt_entry entries[5];
    struct gdt_tss_descriptor tss;
} __attribute__((packed));

static struct gdt_table g_table;
static struct tss64 g_tss;
static struct gdt_pointer g_pointer;

static void set_entry(uint32_t index, uint64_t base, uint32_t limit,
                      uint8_t access, uint8_t flags)
{
    struct gdt_entry *entry = &g_table.entries[index];
    entry->limit_low = (uint16_t)(limit & 0xFFFF);
    entry->base_low = (uint16_t)(base & 0xFFFF);
    entry->base_middle = (uint8_t)((base >> 16) & 0xFF);
    entry->access = access;
    entry->flags = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    entry->base_high = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_init(void)
{
    memset(&g_table, 0, sizeof(g_table));
    memset(&g_tss, 0, sizeof(g_tss));

    set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0); /* 64-bit kernel code */
    set_entry(2, 0, 0xFFFFF, 0x92, 0xC0); /* kernel data */
    set_entry(3, 0, 0xFFFFF, 0xFA, 0xA0); /* 64-bit user code */
    set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0); /* user data */

    g_tss.iomap_base = (uint16_t)sizeof(g_tss);
    uint64_t base = (uint64_t)(uintptr_t)&g_tss;
    uint32_t limit = sizeof(g_tss) - 1;

    g_table.tss.limit_low = (uint16_t)(limit & 0xFFFF);
    g_table.tss.base_low = (uint16_t)(base & 0xFFFF);
    g_table.tss.base_middle = (uint8_t)((base >> 16) & 0xFF);
    g_table.tss.access = 0x89; /* present, available 64-bit TSS */
    g_table.tss.flags = (uint8_t)((limit >> 16) & 0x0F);
    g_table.tss.base_high = (uint8_t)((base >> 24) & 0xFF);
    g_table.tss.base_upper = (uint32_t)(base >> 32);

    g_pointer.limit = (uint16_t)(sizeof(g_table) - 1);
    g_pointer.base = (uint64_t)(uintptr_t)&g_table;

    gdt_load(&g_pointer);
    tss_load(GDT_TSS);

    kprintf("[cpu] GDT/TSS loaded at %p\n", (void *)(uintptr_t)&g_table);
}

void tss_set_rsp0(uint64_t rsp0)
{
    g_tss.rsp0 = rsp0;
}
