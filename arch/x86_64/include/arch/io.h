/* Aurora v2 - x86_64 port I/O and CPU helpers. */
#ifndef AURORA_ARCH_IO_H
#define AURORA_ARCH_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;__asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));return value;
}

static inline void hlt(void)
{
    __asm__ volatile("hlt");
}

static inline void cli(void)
{
    __asm__ volatile("cli" : : : "memory");
}

static inline void sti(void)
{
    __asm__ volatile("sti" : : : "memory");
}

static inline void pause_cpu(void)
{
    __asm__ volatile("pause");
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint64_t read_cr2(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline uint64_t read_cr3(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void write_cr3(uint64_t value)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

static inline uint64_t read_cr4(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

static inline void write_cr4(uint64_t value)
{
    __asm__ volatile("mov %0, %%cr4" : : "r"(value) : "memory");
}

static inline uint64_t read_msr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void write_msr(uint32_t msr, uint64_t value)
{
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

static inline void invlpg(uint64_t address)
{
    __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
}

#endif /* AURORA_ARCH_IO_H */
