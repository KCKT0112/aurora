/* Aurora v2 - fixed-size asynchronous message mailboxes. */
#include <kernel/ipc.h>
#include <kernel/process.h>
#include <kernel/log.h>
#include <kernel/sched.h>
#include <string.h>

#define IRQ_COUNT 16

static int g_irq_subscriber[IRQ_COUNT];
static uint64_t g_message_count;

void ipc_init(void)
{
    for (uint32_t irq = 0; irq < IRQ_COUNT; irq++) {
        g_irq_subscriber[irq] = -1;
    }
    g_message_count = 0;
    kprintf("[ipc] %u-slot mailboxes ready\n", CONFIG_MAILBOX_CAPACITY);
}

void mailbox_init(mailbox_t *mailbox)
{
    memset(mailbox, 0, sizeof(*mailbox));
}

bool mailbox_push(mailbox_t *mailbox, const ipc_message_t *message)
{
    /* Only adjacent motion from the same source/button state may be replaced.
     * Never cross a key, click edge, or transaction boundary. */
    if (mailbox->count && (message->type == MSG_TABLET || message->type == MSG_INPUT_MOUSE_ABS)) {
        uint32_t previous = (mailbox->tail + CONFIG_MAILBOX_CAPACITY - 1) % CONFIG_MAILBOX_CAPACITY;
        ipc_message_t *last = &mailbox->messages[previous];
        uint64_t buttons = message->type == MSG_TABLET ? message->arg2 : message->arg0;
        uint64_t last_buttons = message->type == MSG_TABLET ? last->arg2 : last->arg0;
        if (last->type == message->type && last->from == message->from && buttons == last_buttons) {
            *last = *message;
            return true;
        }
    }
    if (mailbox->count == CONFIG_MAILBOX_CAPACITY) {
        return false;
    }
    mailbox->messages[mailbox->tail] = *message;
    mailbox->tail = (mailbox->tail + 1) % CONFIG_MAILBOX_CAPACITY;
    mailbox->count++;
    return true;
}

bool mailbox_pop(mailbox_t *mailbox, ipc_message_t *message)
{
    if (mailbox->count == 0) {
        return false;
    }
    *message = mailbox->messages[mailbox->head];
    memset(&mailbox->messages[mailbox->head], 0, sizeof(*message));
    mailbox->head = (mailbox->head + 1) % CONFIG_MAILBOX_CAPACITY;
    mailbox->count--;
    return true;
}

int ipc_send(int destination_tid, const ipc_message_t *message, uint64_t sender_tid)
{
    thread_t *destination = sched_thread_get(destination_tid);
    ipc_message_t copy;

    if (destination == NULL || message == NULL || destination->state == THREAD_DEAD) {
        return -AURORA_ENOENT;
    }
    copy = *message;
    copy.from = sender_tid;
    bool queued = mailbox_push(&destination->mailbox, &copy);
    volatile uint8_t *bytes = (volatile uint8_t *)&copy;
    for (unsigned i = 0; i < sizeof(copy); i++)
        bytes[i] = 0;
    if (!queued)
        return -AURORA_EAGAIN;
    if (destination->state == THREAD_WAITING)
        destination->state = THREAD_READY;
    g_message_count++;
    return 0;
}

bool ipc_subscribe_irqs(int tid, uint64_t mask)
{
    thread_t *thread = sched_thread_get(tid);
    uint64_t allowed = 0;
    if (process_has_cap(thread, CAP_TIMER))
        allowed |= 1ull;
    if (process_has_cap(thread, CAP_INPUT))
        allowed |= (1ull << 1) | (1ull << 12) | (1ull << 15);
    if (!thread || (mask & ~allowed)) {
        return false;
    }
    for (uint32_t irq = 0; irq < IRQ_COUNT; irq++) {
        if ((mask & (1ull << irq)) != 0 && g_irq_subscriber[irq] != -1 &&
            g_irq_subscriber[irq] != tid) {
            return false;
        }
    }
    for (uint32_t irq = 0; irq < IRQ_COUNT; irq++) {
        if ((mask & (1ull << irq)) != 0) {
            g_irq_subscriber[irq] = tid;
        } else if (g_irq_subscriber[irq] == tid) {
            g_irq_subscriber[irq] = -1;
        }
    }
    return true;
}

void ipc_emit_irq(uint32_t irq, uint64_t type, uint64_t arg0)
{
    ipc_emit_irq_args(irq, type, arg0, 0, 0);
}

void ipc_emit_irq_args(uint32_t irq, uint64_t type, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    if (irq >= IRQ_COUNT) {
        return;
    }
    int subscriber = g_irq_subscriber[irq];
    if (subscriber < 0) {
        return;
    }
    ipc_message_t message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.arg0 = arg0;
    message.arg1 = arg1;
    message.arg2 = arg2;
    (void)ipc_send(subscriber, &message, KERNEL_ENDPOINT);
}

uint64_t ipc_message_count(void)
{
    return g_message_count;
}

void ipc_unsubscribe(int tid)
{
    for (unsigned i = 0; i < IRQ_COUNT; i++)
        if (g_irq_subscriber[i] == tid)
            g_irq_subscriber[i] = -1;
}
