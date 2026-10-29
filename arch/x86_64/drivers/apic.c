/* Aurora v2 - configure the local APIC to accept legacy 8259 ExtINT.
 *
 * SeaBIOS leaves LINT0 masked on the QEMU q35/max-CPU configuration used by
 * v2, so PIC IRQs would sit pending forever. x2APIC MSRs let us unmask LINT0
 * without carving out a 0xFEE00000 MMIO mapping before the VM layer exists.
 */
#include <arch/apic.h>
#include <arch/io.h>
#include <kernel/log.h>

#define IA32_APIC_BASE_MSR 0x1Bu
#define X2APIC_MSR_BASE 0x800u
#define X2APIC_TPR 0x808u
#define X2APIC_EOI 0x80Bu
#define X2APIC_SPIV 0x80Fu
#define X2APIC_LVT0 0x835u

static bool cpu_has_x2apic(void)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));
    (void)eax;
    (void)ebx;
    (void)edx;
    return (ecx & (1u << 21)) != 0;
}

bool apic_init_pic_passthrough(void)
{
    if (!cpu_has_x2apic()) {
        kprintf("[apic] x2APIC unavailable; legacy PIC may not deliver\n");
        return false;
    }

    uint64_t base = read_msr(IA32_APIC_BASE_MSR);
    write_msr(IA32_APIC_BASE_MSR, base | (1ull << 11) | (1ull << 10));

    write_msr(X2APIC_TPR, 0);
    write_msr(X2APIC_SPIV, 0x1FF);
    uint64_t lvt0 = read_msr(X2APIC_LVT0);
    lvt0 &= ~(1ull << 16); /* clear mask; keep ExtINT delivery mode */
    write_msr(X2APIC_LVT0, lvt0);
    (void)X2APIC_EOI;

    kprintf("[apic] x2APIC enabled, LINT0 ExtINT unmasked (lvt0=%x)\n",
            (unsigned)lvt0);
    return true;
}
