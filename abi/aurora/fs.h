/* Aurora v2 - read-only initramfs image format.
 *
 * The image is generated from the staged system tree and mapped read-only
 * into vfsd as a separate boot resource. vfsd validates every field before serving requests, so a
 * malformed or truncated image fails closed instead of walking bad offsets.
 */
#ifndef AURORA_FS_H
#define AURORA_FS_H

#include <stdint.h>

#define AURORA_FS_MAGIC 0x31465341u /* "ASF1" */
#define AURORA_FS_VERSION 2u
#define AURORA_FS_MAX_NODES 256u
#define AURORA_FS_NAME_BYTES 39u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t node_count;
    uint32_t root_index;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t total_size;
    uint32_t checksum;
} aurora_fs_header_t;

typedef struct __attribute__((packed)) {
    char name[AURORA_FS_NAME_BYTES + 1];
    uint32_t parent;
    uint32_t type;
    uint32_t size;
    uint32_t data_offset;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t reserved;
} aurora_fs_node_t;

_Static_assert(sizeof(aurora_fs_node_t) == 72, "FS v2 node wire size");

#define AURORA_FS_NODE_DIR 1u
#define AURORA_FS_NODE_FILE 2u

enum fs_error {
    FS_ERR_BAD_REQUEST = 1,
    FS_ERR_NOT_FOUND = 2,
    FS_ERR_NOT_DIR = 3,
    FS_ERR_NOT_FILE = 4,
    FS_ERR_TOO_MANY = 5,
    FS_ERR_DENIED = 6,
    FS_ERR_READ_ONLY = 7,
    FS_ERR_NO_SPACE = 8,
    FS_ERR_BUSY = 9,
};

#endif /* AURORA_FS_H */
