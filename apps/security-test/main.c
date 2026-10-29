/* Only included in TEST_IMAGE=1. Every check executes from an unprivileged process. */
#include <aurora/user.h>
#include <stdbool.h>
static bool denied(int64_t value)
{
    return value < 0;
}
int umain(uint64_t argument)
{
    (void)argument;
    framebuffer_info_t fb;
    thread_info_t threads[12];
    system_info_t info;
    ipc_message_t msg = {.type = MSG_FS_LIST};
    int64_t display = sys_service(IMAGE_DISPLAY);
    bool ok = sys_info(&info) == 0 && info.capabilities == 0;
    ok &= denied(sys_fb_map(&fb));
    ok &= denied(sys_display_mode(1920, 1080, &fb));
    ok &= denied(sys_spawn(IMAGE_INPUT, 0));
    ok &= denied(sys_irq_subscribe(1ull << 1));
    ok &= denied(sys_irq_subscribe(1ull << 63));
    ok &= denied(sys_thread_list(threads, 12));
    ok &= denied(sys_kill((int)display));
    ok &= denied(sys_audio_tone(440, 10));
    pcm_status_t pcm;
    int16_t samples[2] = {0};
    ok &= denied(sys_pcm_write(samples, 1));
    ok &= denied(sys_pcm_control(PCM_STOP, &pcm));
    ok &= denied(sys_io_grant((int)display, USER_IO_SIZE + 1));
    ok &= denied(sys_io_copy((int)display, 123, samples, sizeof(samples)));
    ok &= denied(sys_boot_resource());
    credentials_t creds;
    ok &= denied(sys_peer_credentials((int)display, &creds));
    ok &= denied(sys_session_set(0));
    ok &= denied(sys_session_set(AURORA_NO_USER));
    msg.type = MSG_AUTH_LOGIN;
    ok &= denied(sys_send(sys_service(IMAGE_AUTH), &msg));
    msg.type = MSG_FS_LIST;
    ok &= denied(sys_peer_caps((int)display));
    ok &= denied(sys_send(sys_service(IMAGE_VFS), &msg));
    msg.type = MSG_FS_FD_RESULT;
    ok &= denied(sys_send(sys_service(IMAGE_AUDIO), &msg));
    msg.type = MSG_TICK;
    ok &= denied(sys_send(display, &msg));
    ok &= denied(sys_info((system_info_t *)(uintptr_t)0xffffffff80000000ull));
    ok &= denied(sys_recv((ipc_message_t *)(uintptr_t)(USER_STACK_TOP - 8)));
    ok &= denied(sys_send(display, (ipc_message_t *)(uintptr_t)0x800000000000ull));
    user_log(ok ? "security: all denial checks passed" : "security: FAILED");
    return ok ? 0 : 1;
}
