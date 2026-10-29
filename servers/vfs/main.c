/* VFS owns a validated immutable boot image and a bounded RAM overlay.
 * All path policy and transactional writes execute in this user process.
 */
#include <aurora/abi.h>
#include <aurora/fs.h>
#include <aurora/permissions.h>
#include <aurora/fs_validate.h>
#include <aurora/user.h>
#include <aurora/model.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "handles.h"

#define PATH_BYTES 80
#define MAX_CHILDREN FS_MAX_ENTRIES
#define MAX_PREVIEW_LINES FS_MAX_PREVIEW_LINES
#define PREVIEW_TEXT_BYTES FS_PREVIEW_LINE_BYTES
#define PREVIEW_LINE_CAPACITY (PREVIEW_TEXT_BYTES + 1)

#define OVERLAY_NODES 24
#define OVERLAY_BYTES 4096
static const uint8_t *g_image;
static aurora_fs_header_t g_header_storage;
static const aurora_fs_header_t *g_header = &g_header_storage;
static aurora_fs_node_t g_nodes[AURORA_FS_MAX_NODES];
static uint32_t g_base_count;
static credentials_t g_caller;
static uint8_t g_overlay[OVERLAY_NODES][OVERLAY_BYTES];
static int g_overlay_slot[AURORA_FS_MAX_NODES];
static bool g_slot_used[OVERLAY_NODES];
static bool g_removed[AURORA_FS_MAX_NODES];
static uint64_t g_versions[AURORA_FS_MAX_NODES];
static struct {
    int owner;
    uint64_t request, deadline;
    uint32_t size, received;
    char path[PATH_BYTES];
    uint8_t data[OVERLAY_BYTES];
} g_write;

static size_t name_length(const char *name)
{
    size_t length = 0;
    while (length < AURORA_FS_NAME_BYTES && name[length] != '\0') {
        length++;
    }
    return length;
}

static bool image_valid(void)
{
    int64_t size = sys_boot_resource();
    if (size <= 0)
        return false;
    g_image = (const uint8_t *)(uintptr_t)USER_RESOURCE_BASE;
    if (!aurora_fs_valid(g_image, (size_t)size))
        return false;
    memcpy(&g_header_storage, g_image, sizeof(g_header_storage));
    g_base_count = g_header->node_count;
    if (g_base_count + OVERLAY_NODES > AURORA_FS_MAX_NODES)
        return false;
    memcpy(g_nodes, g_image + sizeof(aurora_fs_header_t),
           (size_t)g_base_count * sizeof(aurora_fs_node_t));
    for (unsigned i = 0; i < AURORA_FS_MAX_NODES; i++)
        g_overlay_slot[i] = -1;
    return true;
}

static bool name_equals(const char *name, const char *component, size_t length)
{
    size_t stored = name_length(name);
    return stored == length && memcmp(name, component, length) == 0;
}

/* Resolve an absolute path. Intermediate components must be directories; the
 * final component may be a file. Returns the node index or -1. */
static int resolve_path(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return -1;
    }
    int current = (int)g_header->root_index;
    const char *cursor = path;
    while (*cursor == '/') {
        cursor++;
    }
    while (*cursor != '\0') {
        if (!aurora_dac_allowed(&g_caller, &g_nodes[current], ACCESS_EXECUTE))
            return -2;
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        size_t length = (size_t)(cursor - start);
        const char *after = cursor;
        while (*after == '/') {
            after++;
        }
        bool last = *after == '\0';
        int next = -1;
        for (uint32_t index = 0; index < g_header->node_count; index++) {
            const aurora_fs_node_t *node = &g_nodes[index];
            if (!g_removed[index] && index != 0 && node->parent == (uint32_t)current &&
                name_equals(node->name, start, length) &&
                (last || node->type == AURORA_FS_NODE_DIR)) {
                next = (int)index;
                break;
            }
        }
        if (next < 0) {
            return -1;
        }
        current = next;
        cursor = after;
    }
    return current;
}

static uint32_t child_count(uint32_t parent)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < g_header->node_count; index++) {
        if (!g_removed[index] && index != g_header->root_index && g_nodes[index].parent == parent) {
            count++;
        }
    }
    return count;
}

static void send_message(int destination, uint64_t type, uint64_t arg0, uint64_t arg1,
                         uint64_t arg2, const void *payload, size_t length)
{
    ipc_message_t message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.arg0 = arg0;
    message.arg1 = arg1;
    message.arg2 = arg2;
    if (payload != NULL && length > 0) {
        if (length > sizeof(message.data)) {
            length = sizeof(message.data);
        }
        memcpy(message.data, payload, length);
    }
    (void)user_send(destination, &message);
}

static void reply_error(int destination, uint64_t request, uint64_t code, const char *text)
{
    send_message(destination, MSG_FS_ERROR, request, code, 0, text,
                 text != NULL ? strlen(text) : 0);
}

static int compare_children(const aurora_fs_node_t *left, const aurora_fs_node_t *right)
{
    if (left->type != right->type) {
        return left->type == AURORA_FS_NODE_DIR ? -1 : 1;
    }
    const char *a = left->name;
    const char *b = right->name;
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
        }
        a++;
        b++;
    }
    if (*a == *b) {
        return 0;
    }
    return *a == '\0' ? -1 : 1;
}

static void serve_list(int destination, uint64_t request, const char *path)
{
    int directory = resolve_path(path);
    if (directory < 0) {
        reply_error(destination, request, directory == -2 ? FS_ERR_DENIED : FS_ERR_NOT_FOUND,
                    directory == -2 ? "permission denied" : "path not found");
        return;
    }
    if (g_nodes[directory].type != AURORA_FS_NODE_DIR) {
        reply_error(destination, request, FS_ERR_NOT_DIR, "not a directory");
        return;
    }
    if (!aurora_dac_allowed(&g_caller, &g_nodes[directory], ACCESS_READ | ACCESS_EXECUTE)) {
        reply_error(destination, request, FS_ERR_DENIED, "permission denied");
        return;
    }
    if (child_count((uint32_t)directory) > MAX_CHILDREN) {
        reply_error(destination, request, FS_ERR_TOO_MANY, "directory exceeds the view limit");
        return;
    }

    const aurora_fs_node_t *children[MAX_CHILDREN];
    uint32_t count = 0;
    for (uint32_t index = 0; index < g_header->node_count; index++) {
        if (g_removed[index] || index == g_header->root_index ||
            g_nodes[index].parent != (uint32_t)directory) {
            continue;
        }
        const aurora_fs_node_t *node = &g_nodes[index];
        uint32_t position = count;
        while (position > 0 && compare_children(children[position - 1], node) > 0) {
            children[position] = children[position - 1];
            position--;
        }
        children[position] = node;
        count++;
    }

    send_message(destination, MSG_FS_LIST_BEGIN, request, count, 0, path, strlen(path));
    for (uint32_t index = 0; index < count; index++) {
        const aurora_fs_node_t *node = children[index];
        fs_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.name, node->name, FS_NAME_BYTES);
        entry.type = node->type;
        entry.size = node->size;
        entry.child_count =
            node->type == AURORA_FS_NODE_DIR ? child_count((uint32_t)(node - g_nodes)) : 0;
        send_message(destination, MSG_FS_LIST_ENTRY, request, index, 0, &entry, sizeof(entry));
    }
    send_message(destination, MSG_FS_LIST_END, request, count, 0, NULL, 0);
}

static void push_line(char lines[][PREVIEW_LINE_CAPACITY], uint32_t *count, char *line,
                      size_t *length)
{
    if (*count >= MAX_PREVIEW_LINES) {
        return;
    }
    line[*length] = '\0';
    strncpy(lines[*count], line, PREVIEW_TEXT_BYTES);
    lines[*count][PREVIEW_TEXT_BYTES] = '\0';
    (*count)++;
    *length = 0;
}

static void serve_preview(int destination, uint64_t request, const char *path)
{
    int file = resolve_path(path);
    if (file < 0) {
        reply_error(destination, request, file == -2 ? FS_ERR_DENIED : FS_ERR_NOT_FOUND,
                    file == -2 ? "permission denied" : "path not found");
        return;
    }
    const aurora_fs_node_t *node = &g_nodes[file];
    if (node->type != AURORA_FS_NODE_FILE) {
        reply_error(destination, request, FS_ERR_NOT_FILE, "not a file");
        return;
    }

    if (!aurora_dac_allowed(&g_caller, node, ACCESS_READ)) {
        reply_error(destination, request, FS_ERR_DENIED, "permission denied");
        return;
    }
    static char lines[MAX_PREVIEW_LINES][PREVIEW_LINE_CAPACITY];
    uint32_t line_count = 0;
    char line[PREVIEW_LINE_CAPACITY];
    size_t length = 0;
    bool truncated = false;
    bool line_wrapped = false;
    const uint8_t *data = g_overlay_slot[file] >= 0
                              ? g_overlay[g_overlay_slot[file]]
                              : g_image + g_header->data_offset + node->data_offset;

    for (uint32_t offset = 0; offset < node->size; offset++) {
        if (line_count >= MAX_PREVIEW_LINES) {
            truncated = true;
            break;
        }
        uint8_t byte = data[offset];
        if (byte == '\n') {
            if (length > 0 || !line_wrapped)
                push_line(lines, &line_count, line, &length);
            line_wrapped = false;
            continue;
        }
        if (byte == '\r') {
            continue;
        }
        if (byte == '\t') {
            do {
                line[length++] = ' ';
            } while (length < PREVIEW_TEXT_BYTES && length % 4 != 0);
            if (length == PREVIEW_TEXT_BYTES) {
                push_line(lines, &line_count, line, &length);
                line_wrapped = true;
            }
            continue;
        }
        if (byte < 0x20 || byte > 0x7E) {
            byte = '?';
        }
        line[length++] = (char)byte;
        if (length == PREVIEW_TEXT_BYTES) {
            size_t split = length;
            for (size_t i = length; i > length / 2; i--) {
                if (line[i - 1] == ' ') {
                    split = i - 1;
                    break;
                }
            }
            char remainder[PREVIEW_LINE_CAPACITY];
            size_t remaining = split < length ? length - split - 1 : 0;
            if (remaining)
                memcpy(remainder, line + split + 1, remaining);
            length = split;
            push_line(lines, &line_count, line, &length);
            if (remaining)
                memcpy(line, remainder, remaining);
            length = remaining;
            line_wrapped = remaining == 0;
        }
    }
    if (length > 0) {
        if (line_count < MAX_PREVIEW_LINES) {
            push_line(lines, &line_count, line, &length);
        } else {
            truncated = true;
        }
    }

    send_message(destination, MSG_FS_PREVIEW_BEGIN, request, line_count, node->size, path,
                 strlen(path));
    for (uint32_t index = 0; index < line_count; index++) {
        send_message(destination, MSG_FS_PREVIEW_LINE, request, index, 0, lines[index],
                     strlen(lines[index]));
    }
    send_message(destination, MSG_FS_PREVIEW_END, request, line_count, truncated ? 1 : 0, NULL, 0);
}

static bool request_path(const ipc_message_t *message, char path[PATH_BYTES])
{
    char raw[PATH_BYTES];
    memcpy(raw, message->data, sizeof(raw));
    bool terminated = false;
    for (unsigned i = 0; i < sizeof(raw); i++)
        if (!raw[i]) {
            terminated = true;
            break;
        }
    return terminated && raw[0] == '/' && aurora_path_normalize("/", raw, path, PATH_BYTES);
}

static bool below(const char *path, const char *prefix)
{
    size_t n = strlen(prefix);
    return !strncmp(path, prefix, n) && path[n] == '/';
}

static bool writable_path(const char *path)
{
    return below(path, "/home") || below(path, "/root") || below(path, "/tmp") ||
           below(path, "/etc");
}

static int parent_node(const char *path)
{
    char parent[PATH_BYTES];
    strcpy(parent, path);
    size_t slash = strlen(parent);
    while (slash && parent[slash - 1] != '/')
        slash--;
    if (slash > 1)
        parent[slash - 1] = 0;
    else
        parent[1] = 0;
    return resolve_path(parent);
}

static bool may_write(const char *path, bool remove)
{
    int node = resolve_path(path), parent = parent_node(path);
    if (node == -2 || parent < 0 ||
        !aurora_dac_allowed(&g_caller, &g_nodes[parent], ACCESS_EXECUTE))
        return false;
    if (remove && node >= 0)
        return aurora_unlink_allowed(&g_caller, &g_nodes[parent], &g_nodes[node]);
    if (node >= 0)
        return aurora_dac_allowed(&g_caller, &g_nodes[node], ACCESS_WRITE);
    return aurora_dac_allowed(&g_caller, &g_nodes[parent], ACCESS_WRITE | ACCESS_EXECUTE);
}

static int create_node(const char *path, uint32_t type)
{
    char parent[PATH_BYTES];
    strcpy(parent, path);
    size_t slash = strlen(parent);
    while (slash && parent[slash - 1] != '/')
        slash--;
    const char *name = path + slash;
    if (!*name || strlen(name) > AURORA_FS_NAME_BYTES)
        return -1;
    if (slash > 1)
        parent[slash - 1] = 0;
    else
        parent[1] = 0;
    int directory = resolve_path(parent);
    if (directory < 0 || g_nodes[directory].type != FS_NODE_DIR ||
        child_count((uint32_t)directory) >= FS_MAX_ENTRIES)
        return -1;
    unsigned index;
    for (index = g_base_count; index < g_header->node_count; index++)
        if (g_removed[index])
            break;
    if (index == g_header->node_count) {
        if (index >= g_base_count + OVERLAY_NODES)
            return -1;
        g_header_storage.node_count++;
    }
    g_versions[index]++;
    memset(&g_nodes[index], 0, sizeof(g_nodes[index]));
    strcpy(g_nodes[index].name, name);
    g_nodes[index].parent = (uint32_t)directory;
    g_nodes[index].type = type;
    g_nodes[index].mode = type == FS_NODE_DIR ? 0700 : 0600;
    g_nodes[index].uid = g_caller.uid;
    g_nodes[index].gid = g_caller.gid;
    g_removed[index] = false;
    return (int)index;
}

static void result(const ipc_message_t *msg, uint64_t error)
{
    send_message((int)msg->from, MSG_FS_RESULT, msg->arg0, error, 0, NULL, 0);
}

static void serve_mutation(const ipc_message_t *msg, const char *path)
{
    int64_t caps = sys_peer_caps((int)msg->from);
    if (caps < 0 || !(caps & CAP_FS_WRITE)) {
        result(msg, FS_ERR_DENIED);
        return;
    }
    if (!writable_path(path)) {
        result(msg, FS_ERR_READ_ONLY);
        return;
    }
    if (!may_write(path, msg->type == MSG_FS_REMOVE)) {
        result(msg, FS_ERR_DENIED);
        return;
    }
    if (g_write.owner && (uint64_t)sys_get_ticks() < g_write.deadline &&
        !strcmp(g_write.path, path) && msg->type != MSG_FS_WRITE_BEGIN) {
        result(msg, FS_ERR_BUSY);
        return;
    }
    int node = resolve_path(path);
    if (msg->type == MSG_FS_MKDIR) {
        result(msg, node >= 0                            ? FS_ERR_BAD_REQUEST
                    : create_node(path, FS_NODE_DIR) < 0 ? FS_ERR_NO_SPACE
                                                         : 0);
    } else if (msg->type == MSG_FS_REMOVE) {
        if (node < 0) {
            result(msg, FS_ERR_NOT_FOUND);
            return;
        }
        if (g_nodes[node].type == FS_NODE_DIR && child_count((uint32_t)node)) {
            result(msg, FS_ERR_BUSY);
            return;
        }
        g_removed[node] = true;
        g_versions[node]++;
        int slot = g_overlay_slot[node];
        if (slot >= 0) {
            g_slot_used[slot] = false;
            g_overlay_slot[node] = -1;
        }
        result(msg, 0);
    } else if (msg->type == MSG_FS_WRITE_BEGIN) {
        if (g_write.owner && (uint64_t)sys_get_ticks() < g_write.deadline) {
            result(msg, FS_ERR_BUSY);
            return;
        }
        if (msg->arg1 > OVERLAY_BYTES || (node >= 0 && g_nodes[node].type != FS_NODE_FILE)) {
            result(msg, FS_ERR_BAD_REQUEST);
            return;
        }
        memset(&g_write, 0, sizeof(g_write));
        g_write.owner = (int)msg->from;
        g_write.request = msg->arg0;
        g_write.deadline = (uint64_t)sys_get_ticks() + 500;
        g_write.size = (uint32_t)msg->arg1;
        strcpy(g_write.path, path);
        /* ACK only on commit; chunks are strictly sequential. */
    }
}

static void write_chunk(const ipc_message_t *msg)
{
    if ((int)msg->from != g_write.owner || msg->arg0 != g_write.request ||
        (uint64_t)sys_get_ticks() >= g_write.deadline) {
        result(msg, FS_ERR_BAD_REQUEST);
        return;
    }
    if (msg->type == MSG_FS_WRITE_DATA) {
        if (msg->arg1 != g_write.received || msg->arg2 > sizeof(msg->data) ||
            msg->arg2 > g_write.size - g_write.received) {
            g_write.owner = 0;
            result(msg, FS_ERR_BAD_REQUEST);
            return;
        }
        memcpy(g_write.data + g_write.received, msg->data, (size_t)msg->arg2);
        g_write.received += (uint32_t)msg->arg2;
        return;
    }
    if (g_write.received != g_write.size) {
        g_write.owner = 0;
        result(msg, FS_ERR_BAD_REQUEST);
        return;
    }
    if (!may_write(g_write.path, false)) {
        g_write.owner = 0;
        result(msg, FS_ERR_DENIED);
        return;
    }
    int node = resolve_path(g_write.path);
    int slot = node >= 0 ? g_overlay_slot[node] : -1;
    if (slot < 0)
        for (unsigned i = 0; i < OVERLAY_NODES; i++)
            if (!g_slot_used[i]) {
                slot = (int)i;
                break;
            }
    if (slot >= 0 && node < 0)
        node = create_node(g_write.path, FS_NODE_FILE);
    if (slot < 0 || node < 0 || g_nodes[node].type != FS_NODE_FILE) {
        g_write.owner = 0;
        result(msg, FS_ERR_NO_SPACE);
        return;
    }
    memcpy(g_overlay[slot], g_write.data, g_write.size);
    g_slot_used[slot] = true;
    g_overlay_slot[node] = slot;
    g_nodes[node].size = g_write.size;
    g_versions[node]++;
    g_write.owner = 0;
    result(msg, 0);
    user_log("vfsd: atomic RAM write committed");
}

static void serve_metadata(const ipc_message_t *message, const char *path)
{
    int node = resolve_path(path);
    if (node < 0) {
        reply_error((int)message->from, message->arg0,
                    node == -2 ? FS_ERR_DENIED : FS_ERR_NOT_FOUND,
                    node == -2 ? "permission denied" : "path not found");
        return;
    }
    aurora_fs_node_t *n = &g_nodes[node];
    if (message->type == MSG_FS_STAT) {
        fs_stat_t info = {n->uid, n->gid, n->mode, n->type, n->size, 0};
        send_message((int)message->from, MSG_FS_STAT_RESULT, message->arg0, 0, 0, &info,
                     sizeof(info));
        return;
    }
    int64_t caps = sys_peer_caps((int)message->from);
    if (caps < 0 || !(caps & CAP_FS_WRITE) || (g_caller.uid != 0 && g_caller.uid != n->uid)) {
        result(message, FS_ERR_DENIED);
        return;
    }
    if (!writable_path(path)) {
        result(message, FS_ERR_READ_ONLY);
        return;
    }
    if (message->arg1 > 01777 || (n->type != FS_NODE_DIR && (message->arg1 & 01000))) {
        result(message, FS_ERR_BAD_REQUEST);
        return;
    }
    n->mode = (uint32_t)message->arg1;
    result(message, 0);
}

static void serve_request(const ipc_message_t *message)
{
    if (message->from == KERNEL_ENDPOINT && message->type == MSG_SESSION_CHANGED) {
        memset(&g_write, 0, sizeof(g_write));
        vfs_handles_session_changed();
        return;
    }
    int destination = (int)message->from;
    if (destination <= 0 || sys_peer_credentials(destination, &g_caller) < 0)
        return;
    if (vfs_handle_request(message))
        return;
    if (message->type == MSG_FS_WRITE_DATA || message->type == MSG_FS_WRITE_COMMIT) {
        write_chunk(message);
        return;
    }
    char path[PATH_BYTES];
    if (!request_path(message, path)) {
        reply_error(destination, message->arg0, FS_ERR_BAD_REQUEST, "invalid path");
        return;
    }
    if (message->type == MSG_FS_STAT || message->type == MSG_FS_CHMOD)
        serve_metadata(message, path);
    else if (message->type == MSG_FS_LIST)
        serve_list(destination, message->arg0, path);
    else if (message->type == MSG_FS_READ)
        serve_preview(destination, message->arg0, path);
    else if (message->type == MSG_FS_WRITE_BEGIN || message->type == MSG_FS_MKDIR ||
             message->type == MSG_FS_REMOVE)
        serve_mutation(message, path);
    else
        reply_error(destination, message->arg0, FS_ERR_BAD_REQUEST, "unknown request");
}

static void log_online(void)
{
    char text[80] = "vfsd: initramfs online, ";
    text_u64(text, sizeof(text), g_header->node_count);
    text_append(text, sizeof(text), " nodes, ");
    text_u64(text, sizeof(text), g_header->data_size);
    text_append(text, sizeof(text), " data bytes");
    user_log(text);
}

int umain(uint64_t argument)
{
    (void)argument;
    if (!image_valid()) {
        user_log("vfsd: initramfs validation failed");
        return 1;
    }
    log_online();

    for (;;) {
        ipc_message_t message;
        int budget = 32;
        while (budget-- > 0 && sys_recv(&message) == 0) {
            serve_request(&message);
        }
        sys_wait(100);
    }
}

int vfs_open_node(const char *path, const credentials_t *credentials, uint32_t *node,
                  uint64_t *version, uint32_t *size)
{
    char canonical[80];
    if (!aurora_path_normalize("/", path, canonical, sizeof(canonical)) || path[0] != '/')
        return -AURORA_EINVAL;
    g_caller = *credentials;
    int index = resolve_path(canonical);
    if (index < 0)
        return index == -2 ? -AURORA_EPERM : -AURORA_ENOENT;
    if (g_nodes[index].type != FS_NODE_FILE)
        return -AURORA_EINVAL;
    if (!aurora_dac_allowed(credentials, &g_nodes[index], ACCESS_READ))
        return -AURORA_EPERM;
    *node = (uint32_t)index;
    *version = g_versions[index];
    *size = g_nodes[index].size;
    return 0;
}
int vfs_read_node(uint32_t node, uint64_t version, const credentials_t *credentials,
                  uint64_t offset, uint32_t requested, const void **data)
{
    if (node >= g_header->node_count || g_removed[node] || g_versions[node] != version)
        return -AURORA_EIO;
    const aurora_fs_node_t *n = &g_nodes[node];
    if (!aurora_dac_allowed(credentials, n, ACCESS_READ))
        return -AURORA_EPERM;
    if (offset > n->size)
        return -AURORA_EINVAL;
    uint32_t size = n->size - (uint32_t)offset;
    if (size > requested)
        size = requested;
    const uint8_t *bytes = g_overlay_slot[node] >= 0
                               ? g_overlay[g_overlay_slot[node]]
                               : g_image + g_header->data_offset + n->data_offset;
    *data = bytes + offset;
    return (int)size;
}
