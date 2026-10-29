/* Aurora v2 - syscall entry. */
#ifndef AURORA_KERNEL_SYSCALL_H
#define AURORA_KERNEL_SYSCALL_H

struct interrupt_frame;

void syscall_handle(struct interrupt_frame *frame);

#endif /* AURORA_KERNEL_SYSCALL_H */
