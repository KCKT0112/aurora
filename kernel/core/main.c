/* Aurora v2 - x86_64 kernel entry (M0 bring-up). */
#include <arch/boot.h>
#include <arch/fb.h>
#include <arch/audio.h>
#include <arch/gdt.h>
#include <arch/idt.h>
#include <arch/io.h>
#include <arch/irq.h>
#include <arch/name.h>
#include <arch/pmm.h>
#include <arch/serial.h>
#include <arch/vmm.h>
#include <arch/virtio_input.h>
#include <aurora/version.h>
#include <kernel/ipc.h>
#include <kernel/log.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/splash.h>
#include <stdbool.h>
#include <stdint.h>
#include <limine.h>

static void log_memory_map(void)
{
    const struct limine_memmap_response *response = boot_memmap();
    if (response == NULL) {
        kprintf("[boot] memory map unavailable\n");
        return;
    }

    uint64_t usable_bytes = 0;
    for (uint64_t index = 0; index < response->entry_count; index++) {
        const struct limine_memmap_entry *entry = response->entries[index];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable_bytes += entry->length;
        }
    }

    kprintf("[boot] memory map: %u entries, %u MiB usable\n",
            (unsigned)response->entry_count,
            (unsigned)(usable_bytes >> 20));
}

void kmain(void)
{
    serial_init();
    kprintf("\n");
    kprintf("[aurora] %s %s, %s\n", AURORA_NAME, AURORA_VERSION_STRING,
            AURORA_ARCH_STRING);
    kprintf("[boot] entered long mode through Limine\n");

    if (!boot_protocol_supported()) {
        kpanic("Limine base revision 6 is not supported");
    }

    kprintf("[boot] bootloader: %s %s\n", boot_loader_name(),
            boot_loader_version());
    kprintf("[boot] HHDM offset: %p\n", (void *)(uintptr_t)boot_hhdm_offset());
    log_memory_map();

    const struct limine_framebuffer *framebuffer = boot_framebuffer();
    if (framebuffer == NULL) {
        kpanic("bootloader did not provide a framebuffer");
    }

    kprintf("[boot] framebuffer %ux%u %u-bpp "
            "pitch=%u address=%p\n",
            (unsigned)framebuffer->width, (unsigned)framebuffer->height,
            framebuffer->bpp, (unsigned)framebuffer->pitch,
            framebuffer->address);

    if (!fb_init(framebuffer)) {
        kpanic("unsupported framebuffer format (need 32-bit RGB masks)");
    }

    gdt_init();
    idt_init();
    pmm_init();
    vmm_init();
    virtio_input_init();
    pcm_init();

    ipc_init();
    sched_init();

    splash_render();
    kprintf("[boot] splash rendered at %ux%u\n",
            (unsigned)framebuffer->width, (unsigned)framebuffer->height);

    if (!process_start_init()) {
        kpanic("could not start the ring-3 init supervisor");
    }

    irq_init();
    kprintf("[boot] kernel core ready; entering idle loop\n");

    sti();
    for (;;) {
        hlt();
    }
}
