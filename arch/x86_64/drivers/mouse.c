/* Aurora v2 - PS/2 auxiliary mouse bring-up. */
#include <arch/input.h>
#include <arch/io.h>
#include <kernel/log.h>

#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64
#define PS2_DATA 0x60
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL 0x02

static bool g_available;

static bool wait_input_clear(void)
{
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if ((inb(PS2_STATUS) & PS2_STATUS_INPUT_FULL) == 0) {
            return true;
        }
    }
    return false;
}

static bool wait_output_full(void)
{
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if ((inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) != 0) {
            return true;
        }
    }
    return false;
}

static bool controller_command(uint8_t command)
{
    if (!wait_input_clear()) {
        return false;
    }
    outb(PS2_COMMAND, command);
    return true;
}

static bool controller_data(uint8_t value)
{
    if (!wait_input_clear()) {
        return false;
    }
    outb(PS2_DATA, value);
    return true;
}

static bool aux_command(uint8_t command)
{
    if (!controller_command(0xD4) || !controller_data(command)) {
        return false;
    }
    if (!wait_output_full()) {
        return false;
    }
    uint8_t response = inb(PS2_DATA);
    return response == 0xFA || response == 0xFE;
}

static void drain_output(void)
{
    for (uint32_t spin = 0; spin < 32; spin++) {
        if ((inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) == 0) {
            return;
        }
        (void)inb(PS2_DATA);
    }
}

bool mouse_init(void)
{
    g_available = false;
    drain_output();
    if (!controller_command(0xA8) || !controller_command(0x20) ||
        !wait_output_full()) {
        return false;
    }
    uint8_t config = inb(PS2_DATA);
    config |= 0x02;
    config &= (uint8_t)~0x20;
    if (!controller_command(0x60) || !controller_data(config) ||
        !aux_command(0xF6)) {
        return false;
    }
    drain_output();
    if (!aux_command(0xF4)) {
        return false;
    }
    g_available = true;
    kprintf("[input] PS/2 mouse enabled on IRQ12\n");
    return true;
}

uint8_t mouse_read_byte(void)
{
    return inb(PS2_DATA);
}
