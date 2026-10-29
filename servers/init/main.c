/* Boot/session supervisor. Apps receive immutable credentials from the kernel. */
#include <aurora/user.h>
#include <stdbool.h>
static void lifecycle_test(void)
{
    if (sys_spawn(IMAGE_SECURITY_TEST, 0) <= 0)
        return;
    for (unsigned i = 0; i < 100 && sys_service(IMAGE_SECURITY_TEST) > 0; i++)
        sys_wait(1);
    system_info_t before, after;
    sys_info(&before);
    bool stable = true;
    for (unsigned pass = 0; pass < 16; pass++) {
        if (sys_spawn(IMAGE_SECURITY_TEST, 0) <= 0) {
            stable = false;
            break;
        }
        for (unsigned i = 0; i < 100 && sys_service(IMAGE_SECURITY_TEST) > 0; i++)
            sys_wait(1);
        sys_info(&after);
        if (sys_service(IMAGE_SECURITY_TEST) > 0 ||
            after.free_memory_bytes != before.free_memory_bytes)
            stable = false;
    }
    user_log(stable ? "lifecycle: 16 spawn/exit cycles, no frame leak" : "lifecycle: FAILED");
}
int umain(uint64_t abi)
{
    if (abi != AURORA_ABI_VERSION)
        return 1;
    user_log("init: ABI 4, credential-aware session startup");
    int64_t display = sys_spawn(IMAGE_DISPLAY, 0);
    if (display <= 0 || sys_spawn(IMAGE_INPUT, (uint64_t)display) <= 0 ||
        sys_spawn(IMAGE_VFS, 0) <= 0 || sys_spawn(IMAGE_AUDIO, 0) <= 0 ||
        sys_spawn(IMAGE_AUTH, 0) <= 0)
        return 1;
    const int apps[] = {IMAGE_FILES, IMAGE_TASK, IMAGE_TERMINAL};
    uint64_t last_start[3] = {0};
    uint32_t generation = 0;
    bool tested = false;
    user_log("init: waiting for authenticated login");
    for (;;) {
        ipc_message_t ignored;
        while (sys_recv(&ignored) == 0) {
        }
        credentials_t session;
        sys_session_get(&session);
        if (session.uid == AURORA_NO_USER) {
            generation = session.session;
            sys_wait(100);
            continue;
        }
        bool changed = generation != session.session;
        generation = session.session;
        uint64_t now = (uint64_t)sys_get_ticks();
        for (unsigned i = 0; i < 3; i++) {
            if (sys_service(apps[i]) < 0 && (changed || now - last_start[i] >= 200)) {
                if (sys_spawn(apps[i], 0) > 0)
                    user_log(changed ? "init: session app started" : "init: application restarted");
                last_start[i] = now;
            }
        }
        if (changed) {
            if (!tested && session.uid == 1000) {
                lifecycle_test();
                tested = true;
            }
            (void)sys_spawn(IMAGE_SERVICE_TEST, 0);
            user_log("init: session online");
        }
        sys_wait(100);
    }
}
