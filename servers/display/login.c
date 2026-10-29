#include "login.h"
#include <aurora/user.h>
#include <aurora/keymap.h>
#include <string.h>
#include <ui/draw.h>
#include <ui/vector.h>
static login_request_t form;
static user_keymap_t keymap;
static unsigned field;
static bool pending;
static uint64_t request;
static const char *status = "Sign in to your workspace";
void login_reset(void)
{
    user_erase(&form, sizeof(form));
    memset(&keymap, 0, sizeof(keymap));
    strcpy(form.username, "user");
    field = 1;
    pending = false;
    status = "Sign in to your workspace";
}
static void submit(void)
{
    if (pending || !form.username[0] || !form.password[0])
        return;
    ipc_message_t msg = {.type = MSG_AUTH_LOGIN, .arg0 = ++request};
    memcpy(msg.data, &form, sizeof(form));
    int64_t result = user_send(sys_service(IMAGE_AUTH), &msg);
    user_erase(msg.data, sizeof(msg.data));
    user_erase(form.password, sizeof(form.password));
    if (result < 0)
        status = "Sign-in service unavailable";
    else {
        pending = true;
        status = "Verifying your account...";
    }
}
void login_key(uint16_t scan)
{
    char ch = user_key_ascii(&keymap, scan);
    if (pending || (scan & 0x180))
        return;
    if (scan == 15) {
        field ^= 1;
        return;
    }
    if (scan == 28) {
        submit();
        return;
    }
    char *value = field ? form.password : form.username;
    unsigned limit = field ? 63 : 15;
    size_t n = strlen(value);
    if (scan == 14) {
        if (n)
            value[n - 1] = 0;
    } else if (ch && n < limit) {
        value[n] = ch;
        value[n + 1] = 0;
    }
}
void login_click(int x, int y, int width, int height)
{
    int left = (width - 440) / 2, top = (height - 430) / 2;
    if (x < left + 36 || x >= left + 404)
        return;
    if (y >= top + 148 && y < top + 196)
        field = 0;
    else if (y >= top + 236 && y < top + 284)
        field = 1;
    else if (y >= top + 315 && y < top + 361)
        submit();
}
void login_result(const ipc_message_t *msg)
{
    if (msg->arg0 != request)
        return;
    pending = false;
    status = (int64_t)msg->arg1 == -AURORA_EAGAIN ? "Please wait before trying again"
                                                  : "Incorrect username or password";
}
void login_draw(ui_surface_t *s, const ui_font_t *font)
{
    int x = ((int)s->width - 440) / 2, y = ((int)s->height - 430) / 2;
    ui_shadow(s, x, y, 440, 430, 18, true);
    ui_fill_round_rect(s, x, y, 440, 430, 18, 250, 251, 250, 255);
    ui_font_draw(font, s, 28, 600, x + 36, y + 30, "Welcome to Aurora", 32, 43, 56, 255);
    ui_font_draw(font, s, 14, 400, x + 36, y + 76, status, 116, 128, 140, 255);
    const char *labels[] = {"Username", "Password"};
    for (unsigned i = 0; i < 2; i++) {
        int top = y + 148 + (int)i * 88;
        ui_font_draw(font, s, 13, 600, x + 36, top - 25, labels[i], 70, 83, 92, 255);
        ui_fill_round_rect(s, x + 36, top, 368, 48, 8, field == i ? 57 : 215,
                           field == i ? 118 : 222, field == i ? 107 : 219, 255);
        ui_fill_round_rect(s, x + 38, top + 2, 364, 44, 6, 245, 248, 245, 255);
        char hidden[64] = {0};
        for (unsigned j = 0; j < strlen(form.password); j++)
            hidden[j] = '*';
        ui_font_draw(font, s, 16, 400, x + 52, top + 15, i ? hidden : form.username, 32, 43, 56,
                     255);
        if (field == i && !pending) {
            int end = x + 52 + (int)ui_font_text_width(font, 16, 400, i ? hidden : form.username);
            ui_vline(s, end + 2, top + 13, 21, 57, 118, 107, 255);
        }
    }
    ui_fill_round_rect(s, x + 36, y + 315, 368, 46, 8, 57, 118, 107, 255);
    const char *action = pending ? "Signing in..." : "Sign in";
    int label_x = x + (440 - (int)ui_font_text_width(font, 16, 600, action)) / 2;
    ui_font_draw(font, s, 16, 600, label_x, y + 330, action, 255, 255, 255, 255);
    ui_font_draw(font, s, 12, 400, x + 36, y + 389, "Tab switches fields  /  Enter signs in", 116,
                 128, 140, 255);
}
