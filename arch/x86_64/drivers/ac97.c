/* QEMU ICH AC97 PCM-out mechanism. Only audiod supplies copied stereo s16/48k data.
 * DMA references fixed kernel-owned pages, never user addresses. Single boot CPU. */
#include <arch/audio.h>
#include <arch/io.h>
#include <arch/pci.h>
#include <arch/pmm.h>
#include <arch/vmm.h>
#include <kernel/log.h>
#include <aurora/security.h>
#include <string.h>
#define SLOTS 32u
#define PO 0x10u
#define DCH 1u
#define LVBCI 4u
#define BCIS 8u
#define FIFOE 16u
typedef struct {
    uint32_t address, control;
} descriptor_t;
static uint16_t mixer, bus;
static descriptor_t *descriptors;
static uint64_t descriptor_frame, pages[SLOTS];
static uint32_t frames[SLOTS], head, tail, queued, queued_frames;
static uint64_t played;
static uint32_t underruns;
static bool ready, running, finishing;
static void stop_dma(void)
{
    if (!ready)
        return;
    outb(bus + PO + 0xb, 0);
    outb(bus + PO + 0xb, 2);
    for (unsigned i = 0; i < 10000 && (inb(bus + PO + 0xb) & 2); i++)
        pause_cpu();
    outl(bus + PO, (uint32_t)descriptor_frame);
    outw(bus + PO + 6, LVBCI | BCIS | FIFOE);
    head = tail = queued = queued_frames = 0;
    running = false;
    finishing = false;
}
static void reclaim(void)
{
    if (!ready || !running)
        return;
    uint8_t current = inb(bus + PO + 4);
    uint16_t status = inw(bus + PO + 6);
    if (current >= SLOTS) {
        stop_dma();
        underruns++;
        return;
    }
    while (queued && head != current) {
        queued_frames -= frames[head];
        played += frames[head];
        head = (head + 1) % SLOTS;
        queued--;
    }
    if (queued && head == current && (status & DCH) && (status & LVBCI) && inw(bus + PO + 8) == 0) {
        queued_frames -= frames[head];
        played += frames[head];
        head = (head + 1) % SLOTS;
        queued--;
    }
    if (status & (LVBCI | BCIS | FIFOE))
        outw(bus + PO + 6, status & (LVBCI | BCIS | FIFOE));
    if (!queued && (status & DCH)) {
        if (!finishing)
            underruns++;
        outb(bus + PO + 0xb, 0);
        running = false;
    }
}
void pcm_init(void)
{
    pci_address_t address;
    if (!pci_find_device(0x8086, 0x2415, &address)) {
        kprintf("[ac97] no PCM device\n");
        return;
    }
    uint32_t bar0 = pci_config_read32(&address, 0x10), bar1 = pci_config_read32(&address, 0x14);
    if (!(bar0 & 1) || !(bar1 & 1) || (bar0 & ~3u) > 0xff00 || (bar1 & ~3u) > 0xff00)
        return;
    mixer = (uint16_t)(bar0 & ~3u);
    bus = (uint16_t)(bar1 & ~3u);
    pci_config_write16(&address, 4, pci_config_read16(&address, 4) | 5u); /* I/O + bus master */
    outl(bus + 0x2c, 2);                                                  /* cold reset release */
    for (unsigned i = 0; i < 100000 && !(inl(bus + 0x30) & 0x100); i++)
        pause_cpu();
    if (!(inl(bus + 0x30) & 0x100)) {
        kprintf("[ac97] codec not ready\n");
        return;
    }
    outw(mixer, 0);
    outw(mixer + 0x26, 0);
    outw(mixer + 0x2a, 0); /* fixed 48k */
    outw(mixer + 2, 0);
    outw(mixer + 0x18, 0x0808); /* unity PCM gain, master unmuted */
    descriptor_frame = pmm_alloc_frame();
    if (!descriptor_frame || descriptor_frame > UINT32_MAX)
        goto fail;
    descriptors = vmm_phys_to_virt(descriptor_frame);
    for (unsigned i = 0; i < SLOTS; i++) {
        pages[i] = pmm_alloc_frame();
        if (!pages[i] || pages[i] > UINT32_MAX)
            goto fail;
        descriptors[i].address = (uint32_t)pages[i];
    }
    ready = true;
    stop_dma();
    kprintf("[ac97] PCM ready: 48000 Hz, stereo s16, 32 DMA pages\n");
    return;
fail:
    for (unsigned i = 0; i < SLOTS; i++)
        if (pages[i]) {
            pmm_free_frame(pages[i]);
            pages[i] = 0;
        }
    if (descriptor_frame)
        pmm_free_frame(descriptor_frame);
    descriptor_frame = 0;
    kprintf("[ac97] DMA allocation failed\n");
}
int pcm_write(const int16_t *samples, uint32_t count)
{
    if (!ready)
        return -AURORA_ENOSYS;
    if (!count || count > PCM_MAX_FRAMES)
        return -AURORA_EINVAL;
    reclaim();
    if (queued >= SLOTS - 1)
        return -AURORA_EAGAIN;
    if (!queued && !running) {
        stop_dma();
    }
    uint32_t slot = tail;
    memcpy(vmm_phys_to_virt(pages[slot]), samples, (size_t)count * 4);
    descriptors[slot].control = (count * 2) | (1u << 31);
    frames[slot] = count;
    tail = (tail + 1) % SLOTS;
    queued++;
    queued_frames += count;
    __asm__ volatile("" ::: "memory");
    outb(bus + PO + 5, (uint8_t)slot);
    /* Prebuffer before starting. PCM_START also handles short/final clips. */
    if (!running && queued >= 4) {
        outb(bus + PO + 0xb, 1);
        running = true;
    }
    return 0;
}
int pcm_control(uint32_t action, pcm_status_t *status)
{
    if (!ready) {
        if (status)
            memset(status, 0, sizeof(*status));
        return -AURORA_ENOSYS;
    }
    reclaim();
    if (action == PCM_STOP) {
        stop_dma();
        played = 0;
        underruns = 0;
    } else if (action == PCM_FINISH || action == PCM_START) {
        if (action == PCM_FINISH)
            finishing = true;
        if (queued && !running) {
            outb(bus + PO + 0xb, 1);
            running = true;
        }
    } else if (action != PCM_STATUS)
        return -AURORA_EINVAL;
    if (status)
        *status = (pcm_status_t){1, queued_frames, running, underruns, played};
    return 0;
}
void pcm_tick(void)
{
    reclaim();
}
