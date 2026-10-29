/* Aurora v2 - tiny formatted serial logging.
 *
 * This is deliberately a small, kernel-owned formatter. It understands the
 * width/padding plus the s, c, d, u, x, p and percent conversions used by boot
 * diagnostics. It is not a general printf and does not allocate.
 */
#include <arch/io.h>
#include <arch/serial.h>
#include <kernel/log.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void emit_char(char character)
{
    serial_putc(character);
}

static void emit_string(const char *text)
{
    if (text == NULL) {
        text = "(null)";
    }
    while (*text != '\0') {
        emit_char(*text++);
    }
}

static void emit_number(uint64_t value, unsigned base, bool negative,
                        unsigned min_width, char pad)
{
    char buffer[32];
    unsigned length = 0;

    if (base < 2 || base > 16) {
        base = 10;
    }

    do {
        unsigned digit = (unsigned)(value % base);
        buffer[length++] = (char)(digit < 10 ? '0' + digit
                                             : 'a' + digit - 10);
        value /= base;
    } while (value != 0);

    if (negative) {
        emit_char('-');
    }
    while (length < min_width) {
        emit_char(pad);
        min_width--;
    }
    while (length > 0) {
        emit_char(buffer[--length]);
    }
}

void kprintf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);

    while (*format != '\0') {
        if (*format != '%') {
            emit_char(*format++);
            continue;
        }

        format++;
        char pad = ' ';
        if (*format == '0') {
            pad = '0';
            format++;
        }
        unsigned width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (unsigned)(*format - '0');
            format++;
        }

        bool is_64 = false;
        if (*format == 'l') {
            is_64 = true;
            format++;
            if (*format == 'l') {
                format++;
            }
        }

        switch (*format) {
        case 's':
            emit_string(va_arg(arguments, const char *));
            break;
        case 'c':
            emit_char((char)va_arg(arguments, int));
            break;
        case 'd': {
            int64_t signed_value = is_64
                                       ? va_arg(arguments, int64_t)
                                       : (int64_t)va_arg(arguments, int);
            bool negative = signed_value < 0;
            uint64_t magnitude = negative
                                     ? (uint64_t)(-signed_value)
                                     : (uint64_t)signed_value;
            emit_number(magnitude, 10, negative, width, pad);
            break;
        }
        case 'u': {
            uint64_t value = is_64
                                 ? va_arg(arguments, uint64_t)
                                 : (uint64_t)va_arg(arguments, unsigned int);
            emit_number(value, 10, false, width, pad);
            break;
        }
        case 'x': {
            uint64_t value = is_64
                                 ? va_arg(arguments, uint64_t)
                                 : (uint64_t)va_arg(arguments, unsigned int);
            emit_number(value, 16, false, width, pad);
            break;
        }
        case 'p': {
            uintptr_t value = (uintptr_t)va_arg(arguments, void *);
            emit_string("0x");
            emit_number(value, 16, false, 16, '0');
            break;
        }
        case '%':
            emit_char('%');
            break;
        case '\0':
            format--;
            break;
        default:
            emit_char('%');
            emit_char(*format);
            break;
        }
        format++;
    }

    va_end(arguments);
}

void kpanic(const char *message)
{
    cli();
    kprintf("\n*** AURORA PANIC: %s\n", message);
    for (;;) {
        hlt();
    }
}
