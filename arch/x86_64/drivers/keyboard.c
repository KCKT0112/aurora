/* Aurora v2 - PS/2 keyboard scancode access. */
#include <arch/input.h>
#include <arch/io.h>

void keyboard_init(void)
{
    /* The QEMU i8042 is configured by firmware. */
}

uint8_t keyboard_read_scancode(void)
{
    return inb(0x60);
}
