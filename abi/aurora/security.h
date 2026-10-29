/* ABI 4: boot policy grants. Bits are authority, never caller-supplied roles. */
#ifndef AURORA_SECURITY_H
#define AURORA_SECURITY_H
#include <stdint.h>

#define CAP_SPAWN (1ull << 0)
#define CAP_FRAMEBUFFER (1ull << 1)
#define CAP_INPUT (1ull << 2)
#define CAP_TIMER (1ull << 3)
#define CAP_INSPECT (1ull << 4)
#define CAP_TASK_CONTROL (1ull << 5)
#define CAP_FS_READ (1ull << 6)
#define CAP_FS_WRITE (1ull << 7)
#define CAP_AUDIO (1ull << 8)
#define CAP_AUDIO_DEVICE (1ull << 9)
#define CAP_SERVICE (1ull << 10)
#define CAP_SESSION (1ull << 11)

enum aurora_error {
    AURORA_EPERM = 1,
    AURORA_ENOENT = 2,
    AURORA_EIO = 5,
    AURORA_EAGAIN = 11,
    AURORA_ENOMEM = 12,
    AURORA_EFAULT = 14,
    AURORA_EBUSY = 16,
    AURORA_EINVAL = 22,
    AURORA_ENOSYS = 38,
};

typedef struct {
    uint32_t abi_version;
    uint32_t size;
    uint64_t capabilities;
    uint64_t uptime_ticks;
    uint64_t free_memory_bytes;
    uint32_t tick_hz;
    uint32_t uid;
    uint32_t gid;
    uint32_t session;
} system_info_t;

#define AURORA_NO_USER UINT32_MAX
#define AURORA_ACCOUNT_COUNT 3u
#define AURORA_AUTH_MAGIC 0x41555448u
#define AURORA_AUTH_VERSION 1u
#define AURORA_PASSWORD_MAX 63u
#define AURORA_AUTH_ITERATIONS 100000u

typedef struct {
    uint32_t uid, gid, session, reserved;
} credentials_t;

typedef struct {
    char name[16];
    char home[32];
    uint32_t uid, gid;
} account_info_t;

typedef struct {
    account_info_t account;
    uint8_t salt[16];
    uint8_t verifier[32];
} auth_record_t;

typedef struct {
    uint32_t magic, version, count, iterations;
    auth_record_t records[AURORA_ACCOUNT_COUNT];
} auth_database_t;

typedef struct {
    char username[16];
    char password[64];
} login_request_t;

_Static_assert(sizeof(auth_record_t) == 104, "account record wire size");
_Static_assert(sizeof(auth_database_t) == 328, "account database wire size");
_Static_assert(sizeof(login_request_t) == 80, "login request fits IPC");

/* Public identity metadata; hashes never belong in the public account table. */
static inline account_info_t aurora_account(uint32_t uid)
{
    if (uid == 0)
        return (account_info_t){"root", "/root", 0, 0};
    if (uid == 1000)
        return (account_info_t){"user", "/home/user", 1000, 1000};
    if (uid == 1001)
        return (account_info_t){"guest", "/home/guest", 1001, 1001};
    return (account_info_t){"", "", AURORA_NO_USER, AURORA_NO_USER};
}
#endif
