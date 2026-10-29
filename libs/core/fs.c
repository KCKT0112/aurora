/* Strict tree validation and canonical paths. No allocator or kernel dependencies. */
#include <aurora/fs_validate.h>
#include <string.h>

bool aurora_fs_valid(const void *data, size_t size)
{
    if (!data || size < sizeof(aurora_fs_header_t))
        return false;
    const aurora_fs_header_t *h = data;
    if (h->magic != AURORA_FS_MAGIC || h->version != AURORA_FS_VERSION || h->node_count == 0 ||
        h->node_count > AURORA_FS_MAX_NODES || h->root_index || h->total_size != size ||
        h->data_offset != sizeof(*h) + (uint64_t)h->node_count * sizeof(aurora_fs_node_t) ||
        h->data_offset > size || h->data_size != size - h->data_offset)
        return false;
    const uint8_t *bytes = data;
    uint32_t hash = 0x811c9dc5u;
    for (size_t i = sizeof(*h); i < size; i++)
        hash = (hash ^ bytes[i]) * 0x01000193u;
    if (hash != h->checksum)
        return false;
    const aurora_fs_node_t *nodes = (const void *)(bytes + sizeof(*h));
    for (uint32_t i = 0; i < h->node_count; i++) {
        const aurora_fs_node_t *n = &nodes[i];
        if (n->name[AURORA_FS_NAME_BYTES] || !n->name[0] || n->reserved || n->mode > 01777 ||
            n->parent >= h->node_count ||
            (n->type != AURORA_FS_NODE_DIR && n->type != AURORA_FS_NODE_FILE) ||
            n->data_offset > h->data_size || n->size > h->data_size - n->data_offset)
            return false;
        if (n->type == AURORA_FS_NODE_DIR && (n->size || n->data_offset))
            return false;
        if (i == 0) {
            if (strcmp(n->name, "/") || n->parent || n->type != AURORA_FS_NODE_DIR)
                return false;
            continue;
        }
        if (n->parent >= i || nodes[n->parent].type != AURORA_FS_NODE_DIR ||
            !strcmp(n->name, ".") || !strcmp(n->name, ".."))
            return false;
        for (unsigned k = 0; n->name[k]; k++)
            if ((unsigned char)n->name[k] < 32 || n->name[k] == '/')
                return false;
        for (uint32_t j = 1; j < i; j++)
            if (nodes[j].parent == n->parent && !strcmp(nodes[j].name, n->name))
                return false;
    }
    return true;
}

bool aurora_path_normalize(const char *cwd, const char *path, char *out, size_t cap)
{
    if (!cwd || cwd[0] != '/' || !path || !out || cap < 2)
        return false;
    char joined[160];
    size_t len = 0;
    if (path[0] != '/') {
        for (size_t i = 0; cwd[i]; i++) {
            if (len + 2 >= sizeof(joined))
                return false;
            joined[len++] = cwd[i];
        }
        joined[len++] = '/';
    }
    for (size_t i = 0; path[i]; i++) {
        if (len + 1 >= sizeof(joined))
            return false;
        joined[len++] = path[i];
    }
    joined[len] = 0;
    size_t used = 1, at = 0;
    out[0] = '/';
    while (at < len) {
        while (joined[at] == '/')
            at++;
        size_t start = at;
        while (joined[at] && joined[at] != '/')
            at++;
        size_t count = at - start;
        if (!count || (count == 1 && joined[start] == '.'))
            continue;
        if (count == 2 && joined[start] == '.' && joined[start + 1] == '.') {
            while (used > 1 && out[used - 1] != '/')
                used--;
            if (used > 1)
                used--;
            continue;
        }
        if (count > AURORA_FS_NAME_BYTES)
            return false;
        if (used > 1) {
            if (used + 1 >= cap)
                return false;
            out[used++] = '/';
        }
        if (count >= cap - used)
            return false;
        for (size_t i = 0; i < count; i++) {
            unsigned char c = (unsigned char)joined[start + i];
            if (c < 32 || c == 127)
                return false;
            out[used++] = (char)c;
        }
    }
    out[used] = 0;
    return true;
}
