#ifndef AURORA_MODEL_H
#define AURORA_MODEL_H
#include <aurora/user.h>
#include <stdbool.h>
void text_copy(char *dst, size_t capacity, const char *src);
void text_append(char *dst, size_t capacity, const char *src);
void text_u64(char *dst, size_t capacity, uint64_t value);
bool model_publish(uint32_t view, const char *title, const char *caption, const table_row_t *rows,
                   uint32_t count);
#endif
