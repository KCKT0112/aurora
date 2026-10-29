/* Aurora v2 - user-space syscall bindings. */
#ifndef AURORA_USER_H
#define AURORA_USER_H

#include <aurora/abi.h>
#include <aurora/audio.h>
#include <stddef.h>
#include <stdint.h>

int64_t sys_yield(void);
int64_t sys_pause(void);
int64_t sys_exit(int64_t status) __attribute__((noreturn));
int64_t sys_gettid(void);
int64_t sys_irq_subscribe(uint64_t mask);
int64_t sys_recv(ipc_message_t *message);
int64_t sys_send(int64_t destination, const ipc_message_t *message);
int64_t sys_display_mode(uint32_t width,uint32_t height,framebuffer_info_t *info);
int64_t sys_fb_map(framebuffer_info_t *info);
int64_t sys_get_ticks(void);
int64_t sys_spawn(int image_id, uint64_t argument);
int64_t sys_thread_list(thread_info_t *threads, uint32_t capacity);
int64_t sys_getppid(void);
int64_t sys_log(const char *text, uint64_t length);
int64_t sys_io_buffer(void);
int64_t sys_io_grant(int tid,uint32_t length);
int64_t sys_io_copy(int tid,uint64_t token,const void *source,uint32_t length);
int64_t sys_pcm_write(const int16_t *samples,uint32_t frames);
int64_t sys_pcm_control(uint32_t action,pcm_status_t *status);
void user_log(const char *text);
int64_t sys_info(system_info_t *info);
int64_t sys_service(int image);
int64_t sys_kill(int tid);
int64_t sys_wait(uint64_t ticks);
int64_t sys_audio_tone(uint32_t hz, uint32_t ticks);
int64_t sys_peer_caps(int tid);
int64_t sys_boot_resource(void);
int64_t sys_peer_credentials(int tid, credentials_t *credentials);
int64_t sys_session_get(credentials_t *credentials);
int64_t sys_session_set(uint32_t uid);
int64_t user_send(int64_t destination, const ipc_message_t *message);

#endif /* AURORA_USER_H */
