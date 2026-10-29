/* Aurora v2 - int 0x80 user-space stubs. */
#include <aurora/user.h>
#include <string.h>

static int64_t syscall3(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = arg0;
    register uint64_t rsi __asm__("rsi") = arg1;
    register uint64_t rdx __asm__("rdx") = arg2;

    __asm__ volatile("int $0x80"
                     : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx)
                     : "memory", "rcx", "r11");
    return (int64_t)rax;
}

int64_t sys_yield(void)
{
    return syscall3(SYS_YIELD, 0, 0, 0);
}

int64_t sys_pause(void)
{
    return syscall3(SYS_PAUSE, 0, 0, 0);
}

int64_t sys_exit(int64_t status)
{
    (void)syscall3(SYS_EXIT, (uint64_t)status, 0, 0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

int64_t sys_gettid(void)
{
    return syscall3(SYS_GETTID, 0, 0, 0);
}

int64_t sys_irq_subscribe(uint64_t mask)
{
    return syscall3(SYS_IRQ_SUBSCRIBE, mask, 0, 0);
}

int64_t sys_recv(ipc_message_t *message)
{
    return syscall3(SYS_RECV, (uint64_t)(uintptr_t)message, 0, 0);
}

int64_t sys_send(int64_t destination, const ipc_message_t *message)
{
    return syscall3(SYS_SEND, (uint64_t)destination, (uint64_t)(uintptr_t)message, 0);
}

int64_t sys_fb_map(framebuffer_info_t *info)
{
    return syscall3(SYS_FB_MAP, (uint64_t)(uintptr_t)info, 0, 0);
}

int64_t sys_get_ticks(void)
{
    return syscall3(SYS_GET_TICKS, 0, 0, 0);
}

int64_t sys_spawn(int image_id, uint64_t argument)
{
    return syscall3(SYS_SPAWN, (uint64_t)image_id, argument, 0);
}

int64_t sys_thread_list(thread_info_t *threads, uint32_t capacity)
{
    return syscall3(SYS_THREAD_LIST, (uint64_t)(uintptr_t)threads, capacity, 0);
}

int64_t sys_getppid(void)
{
    return syscall3(SYS_GETPPID, 0, 0, 0);
}

int64_t sys_log(const char *text, uint64_t length)
{
    return syscall3(SYS_LOG, (uint64_t)(uintptr_t)text, length, 0);
}

void user_log(const char *text)
{
    size_t length = strlen(text);
    if (length > 240) {
        length = 240;
    }
    (void)sys_log(text, length);
}

int64_t sys_info(system_info_t *info)
{
    return syscall3(SYS_INFO, (uintptr_t)info, 0, 0);
}
int64_t sys_service(int image)
{
    return syscall3(SYS_SERVICE, (uint64_t)image, 0, 0);
}
int64_t sys_kill(int tid)
{
    return syscall3(SYS_KILL, (uint64_t)tid, 0, 0);
}
int64_t sys_wait(uint64_t ticks)
{
    return syscall3(SYS_WAIT, ticks, 0, 0);
}
int64_t sys_audio_tone(uint32_t hz, uint32_t ticks)
{
    return syscall3(SYS_AUDIO_TONE, hz, ticks, 0);
}
int64_t sys_peer_caps(int tid)
{
    return syscall3(SYS_PEER_CAPS, (uint64_t)tid, 0, 0);
}
int64_t sys_boot_resource(void)
{
    return syscall3(SYS_BOOT_RESOURCE, 0, 0, 0);
}

/* Bounded backpressure: never spin indefinitely on a dead/full service. */
int64_t user_send(int64_t destination, const ipc_message_t *message)
{
    for (unsigned attempt = 0; attempt < 32; attempt++) {
        int64_t status = sys_send(destination, message);
        if (status != -AURORA_EAGAIN)
            return status;
        sys_yield();
    }
    return -AURORA_EAGAIN;
}

int64_t sys_peer_credentials(int tid, credentials_t *credentials)
{
    return syscall3(SYS_PEER_CREDENTIALS, (uint64_t)tid, (uintptr_t)credentials, 0);
}
int64_t sys_session_get(credentials_t *credentials)
{
    return syscall3(SYS_SESSION_GET, (uintptr_t)credentials, 0, 0);
}
int64_t sys_session_set(uint32_t uid)
{
    return syscall3(SYS_SESSION_SET, uid, 0, 0);
}

int64_t sys_display_mode(uint32_t width,uint32_t height,framebuffer_info_t *info)
{return syscall3(SYS_DISPLAY_MODE,width,height,(uintptr_t)info);}

int64_t sys_io_buffer(void){return syscall3(SYS_IO_BUFFER,0,0,0);}
int64_t sys_io_grant(int tid,uint32_t length){return syscall3(SYS_IO_GRANT,(uint64_t)tid,length,0);}
int64_t sys_io_copy(int tid,uint64_t token,const void *source,uint32_t length)
{
    uint64_t descriptor[2]={(uintptr_t)source,length};
    return syscall3(SYS_IO_COPY,(uint64_t)tid,token,(uintptr_t)descriptor);
}

int64_t sys_pcm_write(const int16_t *samples,uint32_t frames){return syscall3(SYS_PCM_WRITE,(uintptr_t)samples,frames,0);}
int64_t sys_pcm_control(uint32_t action,pcm_status_t *status){return syscall3(SYS_PCM_CONTROL,action,(uintptr_t)status,0);}
