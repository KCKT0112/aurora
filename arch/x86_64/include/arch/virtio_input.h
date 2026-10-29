/* Aurora v2 - virtio-input absolute tablet. */
#ifndef AURORA_ARCH_VIRTIO_INPUT_H
#define AURORA_ARCH_VIRTIO_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* Normalized 16-bit pointer coordinates and button state. */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t buttons;
} virtio_pointer_event_t;

bool virtio_input_init(void);
bool virtio_input_ready(void);
bool virtio_input_poll(virtio_pointer_event_t *event);

#endif /* AURORA_ARCH_VIRTIO_INPUT_H */
