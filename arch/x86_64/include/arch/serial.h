/* Aurora v2 - serial output interface. */
#ifndef AURORA_ARCH_SERIAL_H
#define AURORA_ARCH_SERIAL_H

#include <stddef.h>

void serial_init(void);
void serial_putc(char character);
void serial_write(const char *text, size_t length);

#endif /* AURORA_ARCH_SERIAL_H */
