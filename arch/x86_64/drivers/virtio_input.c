/* Aurora v2 - virtio-input PCI tablet probe and event queue.
 *
 * QEMU's -device virtio-tablet-pci exposes a modern virtio PCI input device.
 * This driver maps the capability BARs, negotiates VIRTIO_F_VERSION_1 and
 * feeds the input service normalized absolute pointer events.
 */
#include <arch/io.h>
#include <arch/pci.h>
#include <arch/pmm.h>
#include <arch/vmm.h>
#include <arch/virtio_input.h>
#include <kernel/log.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define VIRTIO_VENDOR 0x1AF4u
#define VIRTIO_INPUT_DEVICE 0x1052u

#define VIRTIO_MMIO_WINDOW 0xffffffffa0000000ull
#define VIRTIO_MMIO_SLOT 0x10000ull
#define VIRTIO_MMIO_SLOTS 3u

#define VIRTIO_PCI_CAP_VENDOR 0x09u
#define VIRTIO_PCI_CAP_COMMON 1u
#define VIRTIO_PCI_CAP_NOTIFY 2u
#define VIRTIO_PCI_CAP_DEVICE 4u

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01u
#define VIRTIO_STATUS_DRIVER 0x02u
#define VIRTIO_STATUS_DRIVER_OK 0x04u
#define VIRTIO_STATUS_FEATURES_OK 0x08u
#define VIRTIO_F_VERSION_1 32u

#define VIRTIO_INPUT_CFG_ABS_INFO 0x12u
#define EV_SYN 0x00u
#define EV_KEY 0x01u
#define EV_ABS 0x03u
#define ABS_X 0x00u
#define ABS_Y 0x01u
#define BTN_LEFT 0x110u

#define VIRTIO_QUEUE_SIZE 64u
#define VIRTIO_QUEUE_BYTES 4096ull
#define VIRTIO_AVAIL_OFFSET 1024ull
#define VIRTIO_USED_OFFSET 1160ull
#define VRING_DESC_F_WRITE 0x2u

typedef struct __attribute__((packed)) {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_driver;
    uint64_t queue_device;
} virtio_pci_common_t;

typedef struct __attribute__((packed)) {
    uint8_t select;
    uint8_t subsel;
    uint8_t size;
    uint8_t reserved[5];
    uint8_t data[128];
} virtio_input_config_t;

typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} virtio_input_event_t;

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} virtio_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTIO_QUEUE_SIZE];
    uint16_t used_event;
} virtio_avail_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t index;
    struct {
        uint32_t id;
        uint32_t length;
    } ring[VIRTIO_QUEUE_SIZE];
    uint16_t avail_event;
} virtio_used_t;

typedef struct {
    uint64_t physical;
    uint8_t *virtual;
    uint32_t length;
} mmio_region_t;

_Static_assert(sizeof(virtio_input_event_t) == 8, "virtio input event size");
_Static_assert(sizeof(virtio_desc_t) == 16, "virtio descriptor size");
_Static_assert(VIRTIO_AVAIL_OFFSET + 6u + 2u * VIRTIO_QUEUE_SIZE + 2u <= VIRTIO_USED_OFFSET,
               "virtio avail ring overlaps used ring");
_Static_assert(VIRTIO_USED_OFFSET + 6u + 8u * VIRTIO_QUEUE_SIZE + 2u <= VIRTIO_QUEUE_BYTES,
               "virtio used ring exceeds queue frame");

static volatile virtio_pci_common_t *g_common;
static volatile virtio_input_config_t *g_device;
static volatile uint8_t *g_notify;
static uint32_t g_notify_multiplier;
static uint16_t g_notify_offset;
static uint16_t g_queue_size;
static bool g_ready;

static virtio_desc_t *g_descriptors;
static virtio_avail_t *g_avail;
static virtio_used_t *g_used;
static virtio_input_event_t *g_events;
static uint16_t g_avail_index;
static uint16_t g_used_seen;
static uint32_t g_abs_min_x;
static uint32_t g_abs_max_x = 1;
static uint32_t g_abs_min_y;
static uint32_t g_abs_max_y = 1;
static uint32_t g_raw_x;
static uint32_t g_raw_y;
static uint8_t g_buttons;
static uint16_t g_last_x = 0xFFFFu;
static uint16_t g_last_y = 0xFFFFu;
static uint8_t g_last_buttons = 0xFFu;

static void memory_barrier(void)
{
    __asm__ volatile("mfence" : : : "memory");
}

static void notify_queue(void)
{
    if (g_notify == NULL) {
        return;
    }
    uint64_t offset = (uint64_t)g_notify_offset * g_notify_multiplier;
    *(volatile uint16_t *)(const void *)(g_notify + offset) = 0;
    memory_barrier();
}

static bool map_cap_region(uint64_t bar_base, uint32_t offset, uint32_t length,
                           uint64_t slot_address, mmio_region_t *region)
{
    uint64_t start = bar_base + offset;
    uint64_t page = start & ~0xFFFull;
    uint64_t end = (start + length + 0xFFFull) & ~0xFFFull;
    uint64_t size = end - page;
    if (bar_base == 0 || length == 0 || size == 0 ||
        slot_address + size > VIRTIO_MMIO_WINDOW + VIRTIO_MMIO_SLOT * VIRTIO_MMIO_SLOTS) {
        return false;
    }
    if (!vmm_map_kernel_mmio(slot_address, page, size)) {
        return false;
    }
    region->physical = page;
    region->virtual = (uint8_t *)(uintptr_t)(slot_address + (start - page));
    region->length = length;
    return true;
}

static uint32_t read_device_abs(uint8_t axis, uint32_t *minimum)
{
    if (g_device == NULL) {
        return 0;
    }
    g_device->select = VIRTIO_INPUT_CFG_ABS_INFO;
    g_device->subsel = axis;
    memory_barrier();
    uint8_t size = g_device->size;
    if (size < 8) {
        return 0;
    }
    uint32_t min_value;
    uint32_t max_value;
    memcpy(&min_value, (const void *)g_device->data, sizeof(min_value));
    memcpy(&max_value, (const void *)(g_device->data + 4), sizeof(max_value));
    *minimum = min_value;
    return max_value;
}

static bool parse_capabilities(const pci_address_t *pci)
{
    uint8_t pointer =
        (uint8_t)(pci_config_read16(pci, 0x06) & 0x10u ? pci_config_read8(pci, 0x34) & 0xFCu : 0);
    bool have_common = false;
    bool have_device = false;
    bool have_notify = false;
    mmio_region_t common_region = {0};
    mmio_region_t device_region = {0};
    mmio_region_t notify_region = {0};

    for (uint32_t step = 0; step < 48 && pointer != 0; step++) {
        uint8_t id = pci_config_read8(pci, pointer);
        uint8_t next = pci_config_read8(pci, (uint8_t)(pointer + 1));
        if (id == VIRTIO_PCI_CAP_VENDOR) {
            uint8_t type = pci_config_read8(pci, (uint8_t)(pointer + 3));
            uint8_t bar = pci_config_read8(pci, (uint8_t)(pointer + 4));
            uint32_t offset = pci_config_read32(pci, (uint8_t)(pointer + 8));
            uint32_t length = pci_config_read32(pci, (uint8_t)(pointer + 12));
            uint64_t base = pci_bar_address(pci, bar);
            uint64_t slot = VIRTIO_MMIO_WINDOW;
            mmio_region_t *region = NULL;
            uint32_t slot_index = 0;
            if (type == VIRTIO_PCI_CAP_COMMON) {
                region = &common_region;
                slot_index = 0;
            } else if (type == VIRTIO_PCI_CAP_DEVICE) {
                region = &device_region;
                slot_index = 1;
            } else if (type == VIRTIO_PCI_CAP_NOTIFY) {
                region = &notify_region;
                slot_index = 2;
                g_notify_multiplier = pci_config_read32(pci, (uint8_t)(pointer + 16));
            }
            if (region != NULL && base != 0 &&
                map_cap_region(base, offset, length, slot + slot_index * VIRTIO_MMIO_SLOT,
                               region)) {
                if (type == VIRTIO_PCI_CAP_COMMON) {
                    have_common = true;
                } else if (type == VIRTIO_PCI_CAP_DEVICE) {
                    have_device = true;
                } else {
                    have_notify = true;
                }
                kprintf("[virtio] cap type=%u bar=%u off=%x len=%x\n", type, bar, offset, length);
            }
        }
        if (next == 0 || next == pointer) {
            break;
        }
        pointer = (uint8_t)(next & 0xFCu);
    }

    if (!have_common || !have_device || !have_notify) {
        kprintf("[virtio] missing capabilities (common=%d dev=%d ntf=%d)\n", have_common ? 1 : 0,
                have_device ? 1 : 0, have_notify ? 1 : 0);
        return false;
    }
    g_common = (volatile virtio_pci_common_t *)(const void *)common_region.virtual;
    g_device = (volatile virtio_input_config_t *)(const void *)device_region.virtual;
    g_notify = (volatile uint8_t *)(const void *)notify_region.virtual;
    return true;
}

bool virtio_input_init(void)
{
    pci_address_t pci;
    g_ready = false;
    if (!pci_find_device(VIRTIO_VENDOR, VIRTIO_INPUT_DEVICE, &pci)) {
        kprintf("[virtio] absolute tablet not present\n");
        return false;
    }
    pci_enable_memory(&pci);
    kprintf("[virtio] tablet at %02x:%02x.%x\n", pci.bus, pci.device, pci.function);
    if (!parse_capabilities(&pci)) {
        return false;
    }

    uint16_t queues = g_common->num_queues;
    kprintf("[virtio] common=%p device=%p notify=%p queues=%u\n", (void *)g_common,
            (void *)g_device, (void *)g_notify, (unsigned)queues);
    g_common->queue_select = 0;
    uint16_t queue_size = g_common->queue_size;
    g_notify_offset = g_common->queue_notify_off;
    kprintf("[virtio] event queue size=%u notify_off=%u mul=%u\n", (unsigned)queue_size,
            (unsigned)g_notify_offset, (unsigned)g_notify_multiplier);

    g_abs_max_x = read_device_abs(ABS_X, &g_abs_min_x);
    g_abs_max_y = read_device_abs(ABS_Y, &g_abs_min_y);
    kprintf("[virtio] abs x=%u..%u y=%u..%u\n", (unsigned)g_abs_min_x, (unsigned)g_abs_max_x,
            (unsigned)g_abs_min_y, (unsigned)g_abs_max_y);
    if (g_abs_max_x <= g_abs_min_x || g_abs_max_y <= g_abs_min_y || queue_size < 8) {
        kprintf("[virtio] invalid absolute range or queue\n");
        return false;
    }
    g_queue_size = queue_size < VIRTIO_QUEUE_SIZE ? queue_size : VIRTIO_QUEUE_SIZE;
    if ((g_queue_size & (g_queue_size - 1u)) != 0) {
        kprintf("[virtio] queue size is not a power of two\n");
        return false;
    }
    g_raw_x = (g_abs_min_x + g_abs_max_x) / 2;
    g_raw_y = (g_abs_min_y + g_abs_max_y) / 2;

    g_common->device_status = 0;
    for (uint32_t spin = 0; spin < 10000 && g_common->device_status != 0; spin++) {
        pause_cpu();
    }
    g_common->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
    g_common->device_status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;

    g_common->device_feature_select = 0;
    uint32_t device_features_lo = g_common->device_feature;
    g_common->device_feature_select = 1;
    uint32_t device_features_hi = g_common->device_feature;
    kprintf("[virtio] features=%08x:%08x\n", device_features_hi, device_features_lo);
    if ((device_features_hi & (1u << (VIRTIO_F_VERSION_1 - 32))) == 0) {
        kprintf("[virtio] VIRTIO_F_VERSION_1 not offered\n");
        g_common->device_status = 0;
        return false;
    }
    g_common->driver_feature_select = 0;
    g_common->driver_feature = 0;
    g_common->driver_feature_select = 1;
    g_common->driver_feature = 1u << (VIRTIO_F_VERSION_1 - 32);
    g_common->device_status =
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    if ((g_common->device_status & VIRTIO_STATUS_FEATURES_OK) == 0) {
        kprintf("[virtio] device rejected features\n");
        g_common->device_status = 0;
        return false;
    }

    uint64_t queue_frame = pmm_alloc_frame();
    uint64_t event_frame = pmm_alloc_frame();
    if (!queue_frame || !event_frame) {
        if (queue_frame)
            pmm_free_frame(queue_frame);
        if (event_frame)
            pmm_free_frame(event_frame);
        kprintf("[virtio] insufficient memory for tablet queue\n");

        g_common->device_status = 0;
        return false;
    }
    uint8_t *queue = vmm_phys_to_virt(queue_frame);
    memset(queue, 0, VIRTIO_QUEUE_BYTES);
    memset(vmm_phys_to_virt(event_frame), 0, 4096);

    g_descriptors = (virtio_desc_t *)(void *)queue;
    g_avail = (virtio_avail_t *)(void *)(queue + VIRTIO_AVAIL_OFFSET);
    g_used = (virtio_used_t *)(void *)(queue + VIRTIO_USED_OFFSET);
    g_events = (virtio_input_event_t *)(void *)vmm_phys_to_virt(event_frame);

    g_common->queue_select = 0;
    g_common->queue_size = g_queue_size;
    for (uint16_t index = 0; index < g_queue_size; index++) {
        g_descriptors[index].address = event_frame + index * sizeof(*g_events);
        g_descriptors[index].length = sizeof(*g_events);
        g_descriptors[index].flags = VRING_DESC_F_WRITE;
        g_descriptors[index].next = 0;
        g_avail->ring[index] = index;
    }
    g_avail->flags = 0;
    g_avail->index = g_queue_size;
    g_avail_index = g_queue_size;
    g_used_seen = 0;

    g_common->queue_desc = queue_frame;
    g_common->queue_driver = queue_frame + VIRTIO_AVAIL_OFFSET;
    g_common->queue_device = queue_frame + VIRTIO_USED_OFFSET;
    g_common->queue_enable = 1;
    memory_barrier();
    g_common->device_status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                              VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
    memory_barrier();
    notify_queue();
    g_ready = true;
    kprintf("[virtio] tablet ready, queue=%u\n", (unsigned)g_queue_size);
    return true;
}

bool virtio_input_ready(void)
{
    return g_ready;
}

static uint16_t normalize_axis(uint32_t value, uint32_t minimum, uint32_t maximum)
{
    if (maximum <= minimum) {
        return 0;
    }
    if (value < minimum) {
        value = minimum;
    }
    if (value > maximum) {
        value = maximum;
    }
    return (uint16_t)(((uint64_t)(value - minimum) * 65535ull + (maximum - minimum) / 2) /
                      (maximum - minimum));
}

bool virtio_input_poll(virtio_pointer_event_t *event)
{
    if (!g_ready || event == NULL) {
        return false;
    }
    memory_barrier();
    uint16_t used_index = g_used->index;
    if (used_index == g_used_seen) {
        return false;
    }

    bool completed = false;
    while (g_used_seen != used_index) {
        uint32_t id = g_used->ring[g_used_seen % g_queue_size].id;
        g_used_seen++;
        if (id >= g_queue_size) {
            continue;
        }
        const virtio_input_event_t *input = &g_events[id];
        if (input->type == EV_ABS) {
            if (input->code == ABS_X) {
                g_raw_x = input->value;
            } else if (input->code == ABS_Y) {
                g_raw_y = input->value;
            }
        } else if (input->type == EV_KEY && input->code == BTN_LEFT) {
            g_buttons = input->value != 0 ? 1u : 0u;
        } else if (input->type == EV_SYN && input->code == 0) {
            completed = true;
        }

        g_avail->ring[g_avail_index % g_queue_size] = (uint16_t)id;
        g_avail_index++;
        if (completed)
            break; /* preserve each SYN_REPORT, especially button transitions */
    }
    memory_barrier();
    g_avail->index = g_avail_index;
    memory_barrier();
    notify_queue();
    if (!completed) {
        return false;
    }

    uint16_t x = normalize_axis(g_raw_x, g_abs_min_x, g_abs_max_x);
    uint16_t y = normalize_axis(g_raw_y, g_abs_min_y, g_abs_max_y);
    if (x == g_last_x && y == g_last_y && g_buttons == g_last_buttons) {
        return false;
    }
    g_last_x = x;
    g_last_y = y;
    g_last_buttons = g_buttons;
    event->x = x;
    event->y = y;
    event->buttons = g_buttons;
    return true;
}
