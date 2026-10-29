/* Read-only descriptors: monotonic IDs, per-owner quotas, explicit audio delegation.
 * An fd is never a global path lookup on behalf of a root service. */
#include "handles.h"
#include <aurora/user.h>
#include <string.h>
#define HANDLE_COUNT 64
#define PER_OWNER 8
typedef struct {
    uint64_t id, version;
    int owner, source;
    uint32_t node, size;
    credentials_t credentials;
} handle_t;
static handle_t handles[HANDLE_COUNT];
static uint64_t next_id = 1;
static bool live(handle_t *h)
{
    credentials_t owner, source;
    if (!h->id)
        return false;
    if (sys_peer_credentials(h->owner, &owner) < 0 ||
        sys_peer_credentials(h->source, &source) < 0 || source.uid != h->credentials.uid ||
        source.gid != h->credentials.gid || source.session != h->credentials.session) {
        memset(h, 0, sizeof(*h));
        return false;
    }
    return true;
}
static handle_t *allocate(int owner)
{
    unsigned count = 0;
    handle_t *free_slot = NULL;
    for (unsigned i = 0; i < HANDLE_COUNT; i++) {
        if (live(&handles[i])) {
            if (handles[i].owner == owner)
                count++;
        } else if (!free_slot)
            free_slot = &handles[i];
    }
    if (count >= PER_OWNER || next_id >= INT64_MAX)
        return NULL;
    return free_slot;
}
static handle_t *lookup(int owner, uint64_t id)
{
    for (unsigned i = 0; i < HANDLE_COUNT; i++)
        if (handles[i].id == id && handles[i].owner == owner && live(&handles[i]))
            return &handles[i];
    return NULL;
}
static void reply(int to, uint64_t request, int64_t status, uint64_t value)
{
    ipc_message_t msg = {
        .type = MSG_FS_FD_RESULT, .arg0 = request, .arg1 = (uint64_t)status, .arg2 = value};
    (void)user_send(to, &msg);
}
void vfs_handles_session_changed(void)
{
    for (unsigned i = 0; i < HANDLE_COUNT; i++)
        (void)live(&handles[i]);
}
bool vfs_handle_request(const ipc_message_t *m)
{
    if ((m->type < MSG_FS_OPEN || m->type > MSG_FS_DELEGATE) && m->type != MSG_FS_CLAIM &&
        m->type != MSG_FS_REVOKE)
        return false;
    if (m->from > INT32_MAX)
        return true;
    int owner = (int)m->from;
    credentials_t creds;
    if (sys_peer_credentials(owner, &creds) < 0)
        return true;
    if (m->type == MSG_FS_OPEN) {
        char path[80];
        memcpy(path, m->data, 80);
        bool terminated = false;
        for (unsigned i = 0; i < 80; i++)
            if (!path[i]) {
                terminated = true;
                break;
            }
        if (!terminated) {
            reply(owner, m->arg0, -AURORA_EINVAL, 0);
            return true;
        }
        handle_t *h = allocate(owner);
        if (!h) {
            reply(owner, m->arg0, -AURORA_ENOMEM, 0);
            return true;
        }
        uint32_t node, size;
        uint64_t version;
        int result = vfs_open_node(path, &creds, &node, &version, &size);
        if (result < 0) {
            reply(owner, m->arg0, result, 0);
            return true;
        }
        *h = (handle_t){.id = next_id++,
                        .version = version,
                        .owner = owner,
                        .source = owner,
                        .node = node,
                        .size = size,
                        .credentials = creds};
        reply(owner, m->arg0, (int64_t)h->id, size);
        return true;
    }
    if (m->type == MSG_FS_REVOKE) {
        for (unsigned i = 0; i < HANDLE_COUNT; i++)
            if (handles[i].id == m->arg1 && handles[i].source == owner) {
                memset(&handles[i], 0, sizeof(handles[i]));
                reply(owner, m->arg0, 0, 0);
                return true;
            }
        reply(owner, m->arg0, -AURORA_EPERM, 0);
        return true;
    }
    handle_t *h = lookup(owner, m->arg1);
    if (!h) {
        reply(owner, m->arg0, -AURORA_EPERM, 0);
        return true;
    }
    if (m->type == MSG_FS_CLAIM) {
        if (owner != sys_service(IMAGE_AUDIO) || m->arg2 != (uint64_t)h->source)
            reply(owner, m->arg0, -AURORA_EPERM, 0);
        else
            reply(owner, m->arg0, 0, h->size);
        return true;
    }
    if (m->type == MSG_FS_CLOSE) {
        memset(h, 0, sizeof(*h));
        reply(owner, m->arg0, 0, 0);
        return true;
    }
    if (m->type == MSG_FS_DELEGATE) {
        int64_t audio = sys_service(IMAGE_AUDIO);
        if (audio <= 0 || m->arg2 != (uint64_t)audio || h->source != owner) {
            reply(owner, m->arg0, -AURORA_EPERM, 0);
            return true;
        }
        handle_t *copy = allocate((int)audio);
        if (!copy) {
            reply(owner, m->arg0, -AURORA_ENOMEM, 0);
            return true;
        }
        *copy = *h;
        copy->owner = (int)audio;
        copy->id = next_id++;
        reply(owner, m->arg0, (int64_t)copy->id, h->size);
        return true;
    }
    uint64_t requested = m->data[0];
    if (requested > USER_IO_SIZE) {
        reply(owner, m->arg0, -AURORA_EINVAL, 0);
        return true;
    }
    const void *data = NULL;
    int count =
        vfs_read_node(h->node, h->version, &h->credentials, m->arg2, (uint32_t)requested, &data);
    if (count >= 0)
        count = (int)sys_io_copy(owner, m->arg3, data, (uint32_t)count);
    reply(owner, m->arg0, count, 0);
    return true;
}
