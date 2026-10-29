/* Aurora v2 - PS/2 input service.
 *
 * The kernel forwards raw IRQ bytes. This ring-3 service owns the PS/2 packet
 * state machine and delivers semantic pointer/keyboard events to the session.
 * QEMU absolute-pointer backends plug in here in the next iteration.
 */
#include <aurora/abi.h>
#include <aurora/user.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint8_t g_packet[3];
static uint32_t g_packet_index;
static bool g_tablet_seen;

static void send_event(uint64_t type, uint64_t arg0, uint64_t arg1, uint64_t arg2,
                       uint64_t display_tid)
{
    ipc_message_t message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.arg0 = arg0;
    message.arg1 = arg1;
    message.arg2 = arg2;
    (void)user_send((int64_t)display_tid, &message);
}

static void handle_mouse_byte(uint8_t byte, uint64_t display_tid)
{
    if (g_packet_index == 0 && (byte & 0xC8u) != 0x08u) {
        return;
    }
    g_packet[g_packet_index++] = byte;
    if (g_packet_index < 3) {
        return;
    }
    g_packet_index = 0;

    uint8_t flags = g_packet[0];
    int32_t delta_x = g_packet[1];
    int32_t delta_y = g_packet[2];
    if ((flags & 0x10u) != 0) {
        delta_x -= 256;
    }
    if ((flags & 0x20u) != 0) {
        delta_y -= 256;
    }

    if (g_tablet_seen)
        return; /* absolute and relative devices must not fight */
    send_event(MSG_INPUT_MOUSE, (uint64_t)(flags & 0x07u), (uint64_t)(int64_t)delta_x,
               (uint64_t)(int64_t)delta_y, display_tid);
}

int umain(uint64_t display_tid)
{
    if (display_tid == 0) {
        user_log("input: no display endpoint supplied");
        return 1;
    }
    if (sys_irq_subscribe((1ull << 1) | (1ull << 12) | (1ull << 15)) < 0) {
        user_log("input: IRQ subscription failed");
        return 1;
    }
    user_log("input: service online");

    for (;;) {
        ipc_message_t message;
        while (sys_recv(&message) == 0) {
            if (message.from != KERNEL_ENDPOINT)
                continue;
            if (message.type == MSG_SESSION_CHANGED) {
                memset(g_packet, 0, sizeof(g_packet));
                g_packet_index = 0;
            } else if (message.type == MSG_MOUSE_BYTE) {
                handle_mouse_byte((uint8_t)message.arg0, display_tid);
            } else if (message.type == MSG_KEY) {
                send_event(MSG_INPUT_KEY, message.arg0, 0, 0, display_tid);
            } else if (message.type == MSG_TABLET) {
                if (!g_tablet_seen) {
                    g_tablet_seen = true;
                    user_log("input: absolute tablet online");
                }
                send_event(MSG_INPUT_MOUSE_ABS, message.arg2, message.arg0, message.arg1,
                           display_tid);
            }
        }
        sys_wait(100);
    }
}
