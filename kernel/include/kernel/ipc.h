/* Aurora v2 - message-passing IPC. */
#ifndef AURORA_KERNEL_IPC_H
#define AURORA_KERNEL_IPC_H

#include <aurora/abi.h>
#include <kernel/thread.h>

void ipc_init(void);
void ipc_unsubscribe(int tid);
void mailbox_init(mailbox_t *mailbox);
bool mailbox_push(mailbox_t *mailbox, const ipc_message_t *message);
bool mailbox_pop(mailbox_t *mailbox, ipc_message_t *message);
int ipc_send(int destination_tid, const ipc_message_t *message, uint64_t sender_tid);
bool ipc_subscribe_irqs(int tid, uint64_t mask);
void ipc_emit_irq(uint32_t irq, uint64_t type, uint64_t arg0);
void ipc_emit_irq_args(uint32_t irq, uint64_t type, uint64_t arg0, uint64_t arg1, uint64_t arg2);
uint64_t ipc_message_count(void);

#endif /* AURORA_KERNEL_IPC_H */
