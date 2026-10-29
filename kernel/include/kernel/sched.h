/* Aurora v2 - preemptive thread scheduler. */
#ifndef AURORA_KERNEL_SCHED_H
#define AURORA_KERNEL_SCHED_H

#include <aurora/abi.h>
#include <kernel/thread.h>
#include <stdint.h>

struct interrupt_frame;

void sched_init(void);
void sched_request(void);
void sched_wait(uint64_t timeout);
void sched_tick(uint64_t ticks);
int sched_kill(int tid);
struct interrupt_frame *sched_on_interrupt(struct interrupt_frame *frame);
thread_t *sched_current(void);
thread_t *sched_thread_get(int tid);
int sched_create_user_thread(process_t *process, uint64_t entry, uint64_t user_stack,
                             uint64_t argument, const char *name);
void sched_terminate_current(void);
uint64_t sched_switch_count(void);
uint32_t sched_snapshot(thread_info_t *threads, uint32_t capacity);

#endif /* AURORA_KERNEL_SCHED_H */
