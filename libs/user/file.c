#include <aurora/file.h>
#include <aurora/user.h>
#include <string.h>
void file_client_init(file_client_t *c, file_event_fn event, void *context)
{
    *c = (file_client_t){.endpoint = sys_service(IMAGE_VFS), .event = event, .context = context};
}
static int64_t rpc(file_client_t *c, ipc_message_t *msg)
{
    if (c->endpoint <= 0)
        return -AURORA_ENOENT;
    uint64_t request = ++c->sequence;
    msg->arg0 = request;
    if (user_send(c->endpoint, msg) < 0)
        return -AURORA_EIO;
    uint64_t deadline = (uint64_t)sys_get_ticks() + 300;
    do {
        while (sys_recv(msg) == 0) {
            if (msg->from == (uint64_t)c->endpoint && msg->type == MSG_FS_FD_RESULT &&
                msg->arg0 == request)
                return (int64_t)msg->arg1;
            if (c->event)
                c->event(msg, c->context);
        }
        sys_wait(1);
    } while ((uint64_t)sys_get_ticks() < deadline);
    return -AURORA_EIO;
}
int64_t file_open(file_client_t *c, const char *path, uint32_t *size)
{
    size_t n = strlen(path);
    if (n >= 80)
        return -AURORA_EINVAL;
    ipc_message_t msg = {.type = MSG_FS_OPEN};
    memcpy(msg.data, path, n + 1);
    int64_t fd = rpc(c, &msg);
    if (fd > 0 && size)
        *size = (uint32_t)msg.arg2;
    return fd;
}
int64_t file_pread(file_client_t *c, uint64_t fd, void *buffer, uint32_t count, uint64_t offset)
{
    if (count > USER_IO_SIZE || (!buffer && count))
        return -AURORA_EINVAL;
    int64_t mapped = sys_io_buffer();
    if (mapped < 0)
        return mapped;
    if (!count)
        return 0;
    int64_t grant = sys_io_grant((int)c->endpoint, count);
    if (grant < 0)
        return grant;
    ipc_message_t msg = {.type = MSG_FS_PREAD, .arg1 = fd, .arg2 = offset, .arg3 = (uint64_t)grant};
    msg.data[0] = count;
    int64_t result = rpc(c, &msg);
    (void)sys_io_grant(0, 0);
    if (result < 0)
        return result;
    if ((uint64_t)result > count)
        return -AURORA_EIO;
    memcpy(buffer, (const void *)(uintptr_t)mapped, (size_t)result);
    return result;
}
int64_t file_close(file_client_t *c, uint64_t fd)
{
    ipc_message_t msg = {.type = MSG_FS_CLOSE, .arg1 = fd};
    return rpc(c, &msg);
}
int64_t file_delegate(file_client_t *c, uint64_t fd, int64_t endpoint)
{
    ipc_message_t msg = {.type = MSG_FS_DELEGATE, .arg1 = fd, .arg2 = (uint64_t)endpoint};
    return rpc(c, &msg);
}

int64_t file_claim(file_client_t *c, uint64_t fd, int64_t source, uint32_t *size)
{
    ipc_message_t msg = {.type = MSG_FS_CLAIM, .arg1 = fd, .arg2 = (uint64_t)source};
    int64_t result = rpc(c, &msg);
    if (result == 0)
        *size = (uint32_t)msg.arg2;
    return result;
}

int64_t file_revoke(file_client_t *c, uint64_t fd)
{
    ipc_message_t msg = {.type = MSG_FS_REVOKE, .arg1 = fd};
    return rpc(c, &msg);
}
