/* Aurora v2 - x86_64 IDT setup. */
#include <arch/gdt.h>
#include <arch/idt.h>
#include <kernel/log.h>
#include <string.h>

#define IDT_ENTRY_COUNT 256

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void idt_load(const void *pointer);
extern uint64_t isr_stub_table[48];
extern void isr128(void);

static struct idt_entry g_entries[IDT_ENTRY_COUNT];
static struct idt_pointer g_pointer;

static void set_gate(uint32_t vector, uint64_t handler, uint8_t attributes)
{
    struct idt_entry *entry = &g_entries[vector];
    entry->offset_low = (uint16_t)(handler & 0xFFFF);
    entry->selector = GDT_KERNEL_CODE;
    entry->ist = 0;
    entry->type_attributes = attributes;
    entry->offset_middle = (uint16_t)((handler >> 16) & 0xFFFF);
    entry->offset_high = (uint32_t)(handler >> 32);
    entry->reserved = 0;
}

void idt_init(void)
{
    memset(g_entries, 0, sizeof(g_entries));

    for (uint32_t vector = 0; vector < 48; vector++) {
        set_gate(vector, isr_stub_table[vector], 0x8E);
    }
    set_gate(0x80, (uint64_t)(uintptr_t)isr128, 0xEE);

    g_pointer.limit = (uint16_t)(sizeof(g_entries) - 1);
    g_pointer.base = (uint64_t)(uintptr_t)g_entries;
    idt_load(&g_pointer);

    kprintf("[cpu] IDT installed at %p, syscall gate DPL=3\n",
            (void *)(uintptr_t)g_entries);
}
