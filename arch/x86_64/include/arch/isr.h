/* Aurora v2 - CPU trap frame and interrupt dispatch. */
#ifndef AURORA_ARCH_ISR_H
#define AURORA_ARCH_ISR_H

#include <stdint.h>

/* Must match the push order in cpu/isr.S. Segment fields are saved explicitly
 * because PUSH Sreg is not available for all segments in 64-bit mode. */
struct interrupt_frame {
    uint64_t gs;
    uint64_t fs;
    uint64_t es;
    uint64_t ds;

    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t vector;
    uint64_t error;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct interrupt_frame *interrupt_dispatch(struct interrupt_frame *frame);

#endif /* AURORA_ARCH_ISR_H */
