/* Test-only process; verifies service policy over the real IPC boundary. */
#include <aurora/user.h>
#include <aurora/fs.h>
#include <aurora/file.h>
#include <string.h>
#include <stdbool.h>
static int64_t vfs;
static uint64_t sequence;
static bool receive(uint64_t request, ipc_message_t *out)
{
    uint64_t deadline = (uint64_t)sys_get_ticks() + 300;
    do {
        while (sys_recv(out) == 0)
            if (out->from == (uint64_t)vfs && out->arg0 == request)
                return true;
        sys_wait(1);
    } while ((uint64_t)sys_get_ticks() < deadline);
    return false;
}
static bool send(uint64_t type, uint64_t request, uint64_t a, uint64_t b, const char *data)
{
    ipc_message_t msg = {.type = type, .arg0 = request, .arg1 = a, .arg2 = b};
    if (data)
        strncpy((char *)msg.data, data, sizeof(msg.data) - 1);
    return user_send(vfs, &msg) == 0;
}
static bool write_file(const char *path, const char *text, uint64_t error)
{
    uint64_t request = ++sequence;
    ipc_message_t response;
    if (!send(MSG_FS_WRITE_BEGIN, request, strlen(text), 0, path))
        return false;
    if (error)
        return receive(request, &response) && response.type == MSG_FS_RESULT &&
               response.arg1 == error;
    if (!send(MSG_FS_WRITE_DATA, request, 0, strlen(text), text) ||
        !send(MSG_FS_WRITE_COMMIT, request, 0, 0, NULL))
        return false;
    return receive(request, &response) && response.type == MSG_FS_RESULT && !response.arg1;
}
static bool read_file(const char *path, const char *text, uint64_t error)
{
    uint64_t request = ++sequence;
    ipc_message_t response;
    bool found = false;
    if (!send(MSG_FS_READ, request, 0, 0, path))
        return false;
    while (receive(request, &response)) {
        if (response.type == MSG_FS_ERROR)
            return error && response.arg1 == error;
        if (response.type == MSG_FS_PREVIEW_LINE && text &&
            !strncmp((const char *)response.data, text, 80))
            found = true;
        if (response.type == MSG_FS_PREVIEW_END)
            return !error && found;
    }
    return false;
}
static bool chmod_file(const char *path, uint32_t mode, uint64_t error)
{
    uint64_t req = ++sequence;
    ipc_message_t reply;
    return send(MSG_FS_CHMOD, req, mode, 0, path) && receive(req, &reply) &&
           reply.type == MSG_FS_RESULT && reply.arg1 == error;
}
static bool stat_file(const char *path, uint32_t uid, uint32_t mode)
{
    uint64_t req = ++sequence;
    ipc_message_t reply;
    fs_stat_t st;
    if (!send(MSG_FS_STAT, req, 0, 0, path) || !receive(req, &reply) ||
        reply.type != MSG_FS_STAT_RESULT)
        return false;
    memcpy(&st, reply.data, sizeof(st));
    return st.uid == uid && st.mode == mode;
}
int umain(uint64_t arg)
{
    (void)arg;
    vfs = sys_service(IMAGE_VFS);
    system_info_t info;
    sys_info(&info);
    if (info.uid == 0) {
        bool root_ok = read_file("/root/secret.txt", "This file is readable only by root.", 0);
        root_ok &= read_file("/home/user/Documents/private.txt", "user-private", 0);
        root_ok &= stat_file("/home/user/Documents/private.txt", 1000, 0600);
        root_ok &= chmod_file("/home/user/Documents/private.txt", 0400, 0);
        root_ok &= write_file("/home/user/Documents/private.txt", "user-private", 0);
        root_ok &= chmod_file("/home/user/Documents/private.txt", 0600, 0);
        root_ok &= write_file("/root/session.txt", "root-private", 0);
        root_ok &= write_file("/etc/root-test.txt", "root-config", 0);
        root_ok &= write_file("/system/forbidden", "blocked", FS_ERR_READ_ONLY);
        framebuffer_info_t framebuffer;
        root_ok &= sys_fb_map(&framebuffer) < 0;
        root_ok &= sys_spawn(IMAGE_INPUT, 0) < 0;
        root_ok &= sys_session_set(1000) < 0;
        root_ok &= sys_kill((int)sys_service(IMAGE_INPUT)) == -AURORA_EPERM;
        user_log(root_ok ? "accounts: root access checks passed" : "accounts: FAILED root");
        return root_ok ? 0 : 1;
    }
    if (info.uid == 1001) {
        bool guest_ok = read_file("/home/guest/private.txt",
                                  "Guest private file. Other ordinary users cannot read this.", 0);
        guest_ok &= read_file("/home/user/Documents/private.txt", NULL, FS_ERR_DENIED);
        guest_ok &= read_file("/root/session.txt", NULL, FS_ERR_DENIED);
        guest_ok &= read_file("/tmp/user-private.txt", NULL, FS_ERR_DENIED);
        guest_ok &= chmod_file("/tmp/user-private.txt", 0666, FS_ERR_DENIED);
        guest_ok &= write_file("/tmp/user-private.txt", "blocked", FS_ERR_DENIED);
        uint64_t req = ++sequence;
        ipc_message_t reply;
        guest_ok &= send(MSG_FS_REMOVE, req, 0, 0, "/tmp/user-private.txt");
        guest_ok &=
            receive(req, &reply) && reply.type == MSG_FS_RESULT && reply.arg1 == FS_ERR_DENIED;
        guest_ok &= write_file("/home/guest/Documents/own.txt", "guest-private", 0);
        user_log(guest_ok ? "accounts: guest isolation and sticky checks passed"
                          : "accounts: FAILED guest");
        return guest_ok ? 0 : 1;
    }
    bool ok = write_file("/tmp/service-test.txt", "first", 0);
    ok &= read_file("/tmp/service-test.txt", "first", 0);
    ok &= write_file("/tmp/../system/escape.txt", "blocked", FS_ERR_READ_ONLY);
    ok &= write_file("/home/user/../../etc/escape.txt", "blocked", FS_ERR_DENIED);
    ok &= read_file("/root/secret.txt", NULL, FS_ERR_DENIED);
    ok &= read_file("/home/guest/private.txt", NULL, FS_ERR_DENIED);
    ok &= write_file("/home/user/Documents/private.txt", "user-private", 0);
    ok &= write_file("/tmp/user-private.txt", "user-private", 0);
    ok &= stat_file("/tmp/user-private.txt", 1000, 0600);
    ok &= chmod_file("/tmp/user-private.txt", 0400, 0);
    ok &= write_file("/tmp/user-private.txt", "blocked", FS_ERR_DENIED);
    ok &= chmod_file("/tmp/user-private.txt", 0600, 0);
    ok &= chmod_file("/etc/os-release", 0600, FS_ERR_DENIED);
    /* Incomplete replacement must leave the original file untouched. */
    uint64_t request = ++sequence;
    ipc_message_t response;
    ok &= send(MSG_FS_WRITE_BEGIN, request, 10, 0, "/tmp/service-test.txt");
    ok &= send(MSG_FS_WRITE_DATA, request, 0, 2, "xx");
    ok &= send(MSG_FS_WRITE_COMMIT, request, 0, 0, NULL);
    ok &= receive(request, &response) && response.type == MSG_FS_RESULT &&
          response.arg1 == FS_ERR_BAD_REQUEST;
    ok &= read_file("/tmp/service-test.txt", "first", 0);
    ok &= write_file("/tmp/service-test.txt", "second", 0);
    ok &= read_file("/tmp/service-test.txt", "second", 0);
    request = ++sequence;
    ok &= send(MSG_FS_REMOVE, request, 0, 0, "/tmp/service-test.txt");
    ok &= receive(request, &response) && response.type == MSG_FS_RESULT && !response.arg1;
    ok &= read_file("/tmp/service-test.txt", NULL, FS_ERR_NOT_FOUND);
    /* Task control cannot terminate protected services, even with the grant. */
    ok &= sys_kill((int)sys_service(IMAGE_INPUT)) == -AURORA_EPERM;
    file_client_t client;
    file_client_init(&client, NULL, NULL);
    uint32_t size = 0;
    char data[16] = {0};
    ok &= write_file("/tmp/fd-test", "abcdef", 0);
    int64_t fd = file_open(&client, "/tmp/fd-test", &size);
    ok &= fd > 0 && size == 6;
    ok &= file_pread(&client, (uint64_t)fd, data, 3, 2) == 3 && !memcmp(data, "cde", 3);
    ok &= file_pread(&client, (uint64_t)fd, data, 3, 6) == 0;
    /* A grant authorizes one exact peer and is consumed even by an EOF copy. */
    int64_t token = sys_io_grant((int)vfs, 3);
    ipc_message_t pread = {.type = MSG_FS_PREAD,
                           .arg0 = ++sequence,
                           .arg1 = (uint64_t)fd,
                           .arg2 = 0,
                           .arg3 = (uint64_t)token};
    pread.data[0] = 3;
    ok &= token > 0 && user_send(vfs, &pread) == 0 && receive(sequence, &response) &&
          (int64_t)response.arg1 == 3;
    pread.arg0 = ++sequence;
    ok &= user_send(vfs, &pread) == 0 && receive(sequence, &response) &&
          (int64_t)response.arg1 == -AURORA_EPERM;
    token = sys_io_grant((int)sys_service(IMAGE_AUDIO), 3);
    pread.arg0 = ++sequence;
    pread.arg3 = (uint64_t)token;
    ok &= user_send(vfs, &pread) == 0 && receive(sequence, &response) &&
          (int64_t)response.arg1 == -AURORA_EPERM;
    sys_io_grant(0, 0);
    ok &= file_pread(&client, (uint64_t)fd, data, 3, UINT64_MAX) < 0;
    ok &= file_open(&client, "/root/secret.txt", NULL) == -AURORA_EPERM;
    ok &= file_delegate(&client, (uint64_t)fd, sys_service(IMAGE_DISPLAY)) < 0;
    int64_t delegated = file_delegate(&client, (uint64_t)fd, sys_service(IMAGE_AUDIO));
    ok &= delegated > 0 && file_pread(&client, (uint64_t)delegated, data, 3, 0) < 0;
    ok &= file_revoke(&client, (uint64_t)delegated) == 0;
    ok &= chmod_file("/tmp/fd-test", 0000, 0);
    ok &= file_pread(&client, (uint64_t)fd, data, 3, 0) == -AURORA_EPERM;
    ok &= chmod_file("/tmp/fd-test", 0600, 0);
    ok &= write_file("/tmp/fd-test", "changed", 0);
    ok &= file_pread(&client, (uint64_t)fd, data, 3, 0) < 0;
    ok &= file_close(&client, (uint64_t)fd) == 0;
    ok &= file_pread(&client, (uint64_t)fd, data, 3, 0) < 0;
    int64_t quota[8];
    for (unsigned i = 0; i < 8; i++) {
        quota[i] = file_open(&client, "/tmp/fd-test", NULL);
        ok &= quota[i] > 0;
    }
    ok &= file_open(&client, "/tmp/fd-test", NULL) < 0;
    for (unsigned i = 0; i < 8; i++)
        ok &= file_close(&client, (uint64_t)quota[i]) == 0;
    user_log(ok ? "services: transactional writes and policy checks passed" : "services: FAILED");
    return ok ? 0 : 1;
}
