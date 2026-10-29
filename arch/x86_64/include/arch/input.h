/* Aurora v2 - PS/2 input access. */
#ifndef AURORA_ARCH_INPUT_H
#define AURORA_ARCH_INPUT_H

#include <stdbool.h>
#include <stdint.h>

void keyboard_init(void);
uint8_t keyboard_read_scancode(void);

bool mouse_init(void);
uint8_t mouse_read_byte(void);

#endif /* AURORA_ARCH_INPUT_H */
