#ifndef AURORA_FS_VALIDATE_H
#define AURORA_FS_VALIDATE_H
#include <stddef.h>
#include <stdbool.h>
#include <aurora/fs.h>
bool aurora_fs_valid(const void *data, size_t size);
bool aurora_path_normalize(const char *cwd, const char *path, char *out, size_t capacity);
#endif
