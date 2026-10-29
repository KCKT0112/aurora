/* Aurora v2 - process/thread objects and IPC mailboxes. */
#ifndef AURORA_KERNEL_THREAD_H
#define AURORA_KERNEL_THREAD_H

#include <arch/vmm.h>
#include <aurora/abi.h>
#include <stdbool.h>
#include <stdint.h>

#define CONFIG_MAX_THREADS 12
#define CONFIG_MAX_PROCESSES 12
#define CONFIG_MAILBOX_CAPACITY 32
#define CONFIG_KERNEL_STACK_SIZE (16u * 1024u)

struct interrupt_frame;

typedef enum {
    THREAD_FREE = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_DEAD,
    THREAD_WAITING,
} thread_state_t;

typedef struct {
    ipc_message_t messages[CONFIG_MAILBOX_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} mailbox_t;

typedef struct process {
    bool used;
    uint64_t capabilities;
    credentials_t credentials;
    uint32_t io_pages, io_limit;
    int io_peer;
    uint64_t io_token;
    uint64_t memory_bytes;
    int image_id;
    address_space_t space;
    int main_tid;
    int parent_tid;
} process_t;

typedef struct thread {
    int tid;
    char name[16];
    thread_state_t state;
    bool is_user;
    bool is_idle;
    uint8_t *kernel_stack;
    uint64_t kernel_stack_top;
    struct interrupt_frame *saved_frame;
    struct process *process;
    mailbox_t mailbox;
    uint64_t cpu_ticks;
    uint64_t wake_tick;
} thread_t;

#endif /* AURORA_KERNEL_THREAD_H */
