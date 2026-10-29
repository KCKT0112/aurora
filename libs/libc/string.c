/* Aurora v2 - minimal freestanding string library shared by all targets. */
#include <string.h>

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;

    while (count--) {
        *out++ = *in++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;

    if (out < in) {
        while (count--) {
            *out++ = *in++;
        }
    } else if (out > in) {
        out += count;
        in += count;
        while (count--) {
            *--out = *--in;
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *out = destination;

    while (count--) {
        *out++ = (unsigned char)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = left;
    const unsigned char *b = right;

    while (count--) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        a++;
        b++;
    }
    return 0;
}

size_t strlen(const char *text)
{
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char *left, const char *right, size_t count)
{
    while (count-- > 0) {
        if (*left != *right) {
            return (unsigned char)*left - (unsigned char)*right;
        }
        if (*left == '\0') {
            return 0;
        }
        left++;
        right++;
    }
    return 0;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;

    while ((*destination++ = *source++) != '\0') {
        /* empty */
    }
    return result;
}

char *strncpy(char *destination, const char *source, size_t count)
{
    size_t index = 0;
    while (index < count && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }
    while (index < count) {
        destination[index++] = '\0';
    }
    return destination;
}
