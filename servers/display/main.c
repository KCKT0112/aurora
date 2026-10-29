/* The compositor owns rendering; init owns startup and process policy. */
#include <aurora/user.h>
#include "desktop.h"

static bool allowed_view(const ipc_message_t *msg)
{
    int image;
    switch (msg->arg0) {
    case VIEW_FILES:
    case VIEW_PREVIEW:
        image = IMAGE_FILES;
        break;
    case VIEW_TASK:
        image = IMAGE_TASK;
        break;
    case VIEW_TERMINAL:
        image = IMAGE_TERMINAL;
        break;
    default:
        return false;
    }
    int64_t owner = sys_service(image);
    return owner > 0 && msg->from == (uint64_t)owner;
}
int umain(uint64_t argument)
{
    (void)argument;
    framebuffer_info_t fb;
    if (sys_fb_map(&fb) < 0 || sys_irq_subscribe(1) < 0)
        return 1;
    desktop_init(&fb);
    user_log("display: workspace online, ring 3");
    uint32_t generation = UINT32_MAX;
    for (;;) {
        credentials_t session;
        if (sys_session_get(&session) == 0 && generation != session.session) {
            generation = session.session;
            desktop_session(&session);
        }
        ipc_message_t msg;
        int64_t input = sys_service(IMAGE_INPUT);
        int budget = 128;
        while (budget-- && sys_recv(&msg) == 0) {
            if (msg.from == KERNEL_ENDPOINT && msg.type == MSG_SESSION_CHANGED) {
                if (sys_session_get(&session) == 0 && generation != session.session) {
                    generation = session.session;
                    desktop_session(&session);
                }
            } else if (msg.from == KERNEL_ENDPOINT && msg.type == MSG_DISPLAY_RESIZE) {
                if (msg.arg0 <= 3840 && msg.arg1 <= 2160)
                    desktop_resize((uint32_t)msg.arg0, (uint32_t)msg.arg1);
            } else if (msg.from == (uint64_t)sys_service(IMAGE_AUTH) &&
                       msg.type == MSG_AUTH_RESULT) {
                desktop_auth_result(&msg);
            } else if (msg.from == KERNEL_ENDPOINT && msg.type == MSG_TICK)
                desktop_tick(msg.arg0);
            else if (input > 0 && msg.from == (uint64_t)input) {
                if (msg.type == MSG_INPUT_MOUSE)
                    desktop_pointer((int32_t)msg.arg1, (int32_t)msg.arg2, (uint8_t)msg.arg0);
                else if (msg.type == MSG_INPUT_MOUSE_ABS)
                    desktop_pointer_absolute((uint16_t)msg.arg1, (uint16_t)msg.arg2,
                                             (uint8_t)msg.arg0);
                else if (msg.type == MSG_INPUT_KEY)
                    desktop_key((uint8_t)msg.arg0);
            } else if (allowed_view(&msg))
                desktop_model(&msg);
        }
        if (desktop_render_pending())
            desktop_render();
        sys_wait(100);
    }
}
