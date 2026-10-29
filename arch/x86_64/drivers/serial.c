/* Aurora v2 - 16550 UART (COM1). */
#include <arch/io.h>
#include <arch/serial.h>

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char character)
{
    while ((inb(COM1 + 5) & 0x20) == 0) {
        /* wait for transmitter */
    }
    outb(COM1, (uint8_t)character);
}

void serial_write(const char *text, size_t length)
{
    for (size_t index = 0; index < length; index++) {
        if (text[index] == '\n') {
            serial_putc('\r');
        }
        serial_putc(text[index]);
    }
}
