/* Aurora v2 - first userland process loading. */
#ifndef AURORA_KERNEL_PROCESS_H
#define AURORA_KERNEL_PROCESS_H

#include <stdbool.h>
#include <stdint.h>
#include <kernel/thread.h>

bool process_start_init(void);
int process_spawn_image(int image_id, uint64_t argument, int parent_tid);
int process_display_tid(void);
int process_service_tid(int image_id);
credentials_t process_session(void);
int process_session_set(uint32_t uid);
void process_release(process_t *process);
bool process_has_cap(const thread_t *thread, uint64_t capability);

#endif /* AURORA_KERNEL_PROCESS_H */
