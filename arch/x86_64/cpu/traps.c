/* Aurora v2 - trap dispatch for exceptions, IRQs and syscalls. */
#include <arch/io.h>
#include <arch/irq.h>
#include <arch/isr.h>
#include <arch/vmm.h>
#include <aurora/abi.h>
#include <kernel/log.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <stddef.h>

static bool from_user(const struct interrupt_frame *frame)
{
    return (frame->cs & 3u) == 3u;
}

static void handle_user_exception(struct interrupt_frame *frame)
{
    uint64_t fault_address = frame->vector == 14 ? read_cr2() : 0;
    thread_t *thread = sched_current();
    kprintf("[fault] user tid=%d vec=%u err=%x rip=%p addr=%p\n",
            thread != NULL ? thread->tid : -1, (unsigned)frame->vector,
            (unsigned)frame->error, (void *)(uintptr_t)frame->rip,
            (void *)(uintptr_t)fault_address);
    sched_terminate_current();
}

static void handle_kernel_exception(struct interrupt_frame *frame)
{
    uint64_t fault_address = frame->vector == 14 ? read_cr2() : 0;
    kprintf("\n*** KERNEL EXCEPTION vec=%u err=%x rip=%p addr=%p\n",
            (unsigned)frame->vector, (unsigned)frame->error,
            (void *)(uintptr_t)frame->rip, (void *)(uintptr_t)fault_address);
    cli();
    for (;;) {
        hlt();
    }
}

struct interrupt_frame *interrupt_dispatch(struct interrupt_frame *frame)
{
    uint64_t vector = frame->vector;

    if (vector == 0x80) {
        syscall_handle(frame);
    } else if (vector >= 32 && vector < 48) {
        irq_handle((uint32_t)(vector - 32));
    } else if (vector < 32) {
        if (from_user(frame)) {
            handle_user_exception(frame);
        } else {
            handle_kernel_exception(frame);
        }
    } else if (from_user(frame)) {
        handle_user_exception(frame);
    } else {
        handle_kernel_exception(frame);
    }

    return sched_on_interrupt(frame);
}
