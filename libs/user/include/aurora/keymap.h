#ifndef AURORA_KEYMAP_H
#define AURORA_KEYMAP_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { bool left_shift, right_shift, caps; } user_keymap_t;
char user_key_ascii(user_keymap_t *state, uint16_t scan);
void user_erase(void *data, unsigned size);
#endif
