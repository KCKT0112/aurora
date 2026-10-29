/* Optional local emulator resize hint transport. No memory addresses or device commands. */
#include <arch/host_display.h>
#include <arch/io.h>
#include <aurora/display_protocol.h>
#include <kernel/ipc.h>
#include <kernel/process.h>
#define COM2 0x2f8
static display_parser_t parser;
static uint16_t pending_w, pending_h;
void host_display_init(void)
{
    outb(COM2 + 1, 0);
    outb(COM2 + 3, 0x80);
    outb(COM2, 1);
    outb(COM2 + 1, 0);
    outb(COM2 + 3, 3);
    outb(COM2 + 2, 0xc7);
    outb(COM2 + 4, 0xb);
}
void host_display_poll(void)
{
    for (unsigned budget = 0; budget < 32; budget++) {
        uint8_t status = inb(COM2 + 5);
        if (status == 0xff || !(status & 1))
            break;
        uint16_t w, h;
        if (display_packet_byte(&parser, inb(COM2), &w, &h)) {
            pending_w = w;
            pending_h = h;
        }
    }
    if (pending_w) {
        ipc_message_t msg = {.type = MSG_DISPLAY_RESIZE, .arg0 = pending_w, .arg1 = pending_h};
        if (ipc_send(process_display_tid(), &msg, KERNEL_ENDPOINT) == 0)
            pending_w = pending_h = 0;
    }
}
