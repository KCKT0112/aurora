/* Aurora v2 - IRQ routing into the microkernel scheduler and IPC. */
#include <arch/apic.h>
#include <arch/audio.h>
#include <arch/input.h>
#include <arch/irq.h>
#include <arch/host_display.h>
#include <arch/pic.h>
#include <arch/pit.h>
#include <arch/virtio_input.h>
#include <aurora/abi.h>
#include <kernel/ipc.h>
#include <kernel/log.h>
#include <kernel/sched.h>

void irq_init(void)
{
    (void)apic_init_pic_passthrough();
    pic_remap(0x20, 0x28);
    pit_init();
    host_display_init();
    keyboard_init();
    bool mouse_ok = mouse_init();

    pic_unmask(0);
    pic_unmask(1);
    pic_unmask(2);
    if (mouse_ok) {
        pic_unmask(12);
    }
    kprintf("[irq] PIT=%u Hz, keyboard ready, mouse=%s\n", AURORA_TICK_HZ, mouse_ok ? "on" : "off");
}

void irq_handle(uint32_t irq)
{
    switch (irq) {
    case 0: {
        uint64_t ticks = pit_on_tick();
        host_display_poll();
        virtio_pointer_event_t pointer;
        for (unsigned budget = 0; budget < 64 && virtio_input_poll(&pointer); budget++) {
            ipc_emit_irq_args(15, MSG_TABLET, pointer.x, pointer.y, pointer.buttons);
        }
        if ((ticks % 10) == 0) {
            ipc_emit_irq(0, MSG_TICK, ticks);
        }
        audio_tick(ticks);
        sched_tick(ticks);
        break;
    }
    case 1:
        ipc_emit_irq(1, MSG_KEY, keyboard_read_scancode());
        break;
    case 12:
        ipc_emit_irq(12, MSG_MOUSE_BYTE, mouse_read_byte());
        break;
    default:
        break;
    }
    pic_send_eoi(irq);
}
