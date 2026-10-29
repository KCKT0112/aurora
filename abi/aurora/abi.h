/* Aurora v2 - shared kernel/user ABI. */
#ifndef AURORA_ABI_H
#define AURORA_ABI_H

#include <stdint.h>

#define AURORA_ABI_VERSION 4
#include <aurora/security.h>

#define KERNEL_ENDPOINT 0u

/* Fixed user address-space layout shared by built-in processes. */
#define USER_BASE 0x0000000000400000ull
#define USER_IMAGE_LIMIT 0x000000000F000000ull
#define USER_STACK_TOP 0x0000000010000000ull
#define USER_STACK_SIZE 0x0000000000010000ull
#define USER_FB_BASE 0x0000000040000000ull
#define USER_IO_BASE 0x0000000020000000ull
#define USER_IO_SIZE 65536u
#define USER_RESOURCE_BASE 0x0000000030000000ull

/* Every IPC mailbox entry is exactly 128 bytes. */
typedef struct {
    uint64_t type;
    uint64_t from;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t data[10];
} ipc_message_t;

enum message_type {
    MSG_TICK = 0x100,
    MSG_KEY = 0x101,
    MSG_MOUSE_BYTE = 0x102,
    MSG_PROC_FAULT = 0x103,
    MSG_SESSION_CHANGED = 0x105,
    MSG_DISPLAY_RESIZE = 0x106,
    MSG_TABLET = 0x104, /* kernel -> input: x, y, buttons */

    /* Semantic events emitted by the input service. */
    MSG_INPUT_MOUSE = 0x200,
    MSG_INPUT_MOUSE_ABS = 0x201,
    MSG_INPUT_KEY = 0x202,

    /* Model-view messages: applications publish data, the compositor owns
     * chrome, typography and hit testing. arg0 = view id. */
    MSG_TABLE_BEGIN = 0x300,
    MSG_TABLE_ROW = 0x301,
    MSG_TABLE_END = 0x302,

    /* Compositor -> application view events. */
    MSG_VIEW_SELECT = 0x310,
    MSG_VIEW_ACTIVATE = 0x311,
    MSG_VIEW_BACK = 0x312,
    MSG_VIEW_KEY = 0x313,
    MSG_VIEW_PATH = 0x314,
    MSG_VIEW_REFRESH = 0x315,
    MSG_AUDIO_PLAY = 0x500,
    MSG_AUDIO_RESULT = 0x501,
    MSG_AUTH_LOGIN = 0x600,
    MSG_AUTH_RESULT = 0x601,
    MSG_AUTH_LOGOUT = 0x602,

    /* Read-only VFS protocol. The path or payload is in data[]. */
    MSG_FS_LIST = 0x400,
    MSG_FS_LIST_BEGIN = 0x401,
    MSG_FS_LIST_ENTRY = 0x402,
    MSG_FS_LIST_END = 0x403,
    MSG_FS_ERROR = 0x404,
    MSG_FS_READ = 0x405,
    MSG_FS_PREVIEW_BEGIN = 0x406,
    MSG_FS_PREVIEW_LINE = 0x407,
    MSG_FS_PREVIEW_END = 0x408,
    MSG_FS_WRITE_BEGIN = 0x409,
    MSG_FS_WRITE_DATA = 0x40a,
    MSG_FS_WRITE_COMMIT = 0x40b,
    MSG_FS_RESULT = 0x40c,
    MSG_FS_MKDIR = 0x40d,
    MSG_FS_REMOVE = 0x40e,
    MSG_FS_STAT = 0x40f,
    MSG_FS_STAT_RESULT = 0x410,
    MSG_FS_CHMOD = 0x411,
    MSG_FS_OPEN = 0x420,
    MSG_FS_PREAD = 0x421,
    MSG_FS_CLOSE = 0x422,
    MSG_FS_DELEGATE = 0x423,
    MSG_FS_FD_RESULT = 0x424,
    MSG_FS_CLAIM = 0x425,
    MSG_FS_REVOKE = 0x426,
    MSG_AUDIO_WAV = 0x510,
    MSG_AUDIO_STOP = 0x511,
    MSG_AUDIO_STATUS = 0x512,
    MSG_AUDIO_INFO = 0x513,
};

enum view_id { VIEW_TASK = 0, VIEW_FILES = 1, VIEW_PREVIEW = 2, VIEW_TERMINAL = 3, VIEW_COUNT };

#define TABLE_MAX_ROWS 16
#define TABLE_LABEL_BYTES 23
#define TABLE_VALUE_BYTES 39
#define TABLE_TITLE_BYTES 47
#define TABLE_CAPTION_BYTES 31

enum table_row_kind {
    TABLE_ROW_TEXT = 0,
    TABLE_ROW_DIR = 1,
    TABLE_ROW_FILE = 2,
    TABLE_ROW_LINE = 3, /* label + value concatenate into one text line */
};

typedef struct {
    char label[TABLE_LABEL_BYTES + 1]; /* 24 bytes */
    char value[TABLE_VALUE_BYTES + 1]; /* 40 bytes */
    uint8_t kind;
    uint8_t reserved[7];
} table_row_t; /* 72 bytes, fits one IPC payload */

typedef struct {
    char title[TABLE_TITLE_BYTES + 1];     /* 48 bytes */
    char caption[TABLE_CAPTION_BYTES + 1]; /* 32 bytes */
} table_header_t;

#define FS_NAME_BYTES 39
#define FS_MAX_ENTRIES 12
#define FS_MAX_PREVIEW_LINES 16
#define FS_PREVIEW_LINE_BYTES (TABLE_LABEL_BYTES + TABLE_VALUE_BYTES)

enum fs_node_type {
    FS_NODE_DIR = 1,
    FS_NODE_FILE = 2,
};

typedef struct {
    char name[FS_NAME_BYTES + 1]; /* 40 bytes */
    uint32_t type;
    uint32_t size;
    uint32_t child_count;
    uint32_t reserved;
} fs_entry_t;

typedef struct {
    uint32_t uid, gid, mode, type, size, reserved;
} fs_stat_t;

typedef struct {
    uint32_t tid;
    uint32_t state;
    uint32_t flags;
    uint32_t uid;
    char name[16];
    uint64_t cpu_ticks;
    uint64_t memory_bytes;
    uint64_t capabilities;
} thread_info_t;

#define THREAD_FLAG_USER 0x1u
#define THREAD_FLAG_IDLE 0x2u

enum image_id {
    IMAGE_DISPLAY = 0,
    IMAGE_INPUT = 1,
    IMAGE_TASK = 2,
    IMAGE_VFS = 3,
    IMAGE_FILES = 4,
    IMAGE_INIT = 5,
    IMAGE_TERMINAL = 6,
    IMAGE_AUDIO = 7,
    IMAGE_SECURITY_TEST = 8,
    IMAGE_SERVICE_TEST = 9,
    IMAGE_AUTH = 10,
    IMAGE_COUNT
};

enum syscall_number {
    SYS_EXIT = 1,
    SYS_YIELD = 2,
    SYS_PAUSE = 3,
    SYS_GETTID = 4,
    SYS_IRQ_SUBSCRIBE = 5,
    SYS_RECV = 6,
    SYS_SEND = 7,
    SYS_FB_MAP = 8,
    SYS_GET_TICKS = 9,
    SYS_LOG = 10,
    SYS_SPAWN = 11,
    SYS_THREAD_LIST = 12,
    SYS_GETPPID = 13,
    SYS_INFO = 14,
    SYS_SERVICE = 15,
    SYS_KILL = 16,
    SYS_WAIT = 17,
    SYS_AUDIO_TONE = 18,
    SYS_PEER_CAPS = 19,
    SYS_BOOT_RESOURCE = 20,
    SYS_PEER_CREDENTIALS = 21,
    SYS_SESSION_GET = 22,
    SYS_SESSION_SET = 23,
    SYS_DISPLAY_MODE = 24,
    SYS_IO_BUFFER = 25,
    SYS_IO_GRANT = 26,
    SYS_IO_COPY = 27,
    SYS_PCM_WRITE = 28,
    SYS_PCM_CONTROL = 29,
};

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t vaddr;
    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;
    uint8_t reserved;
} framebuffer_info_t;

_Static_assert(sizeof(ipc_message_t) == 128, "IPC wire size");
_Static_assert(sizeof(table_header_t) == 80, "table header wire size");
_Static_assert(sizeof(table_row_t) <= 80, "table row fits IPC");

#endif /* AURORA_ABI_H */
