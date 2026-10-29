/* Aurora v2 - File Manager.
 *
 * A ring-3 navigation and text-preview client of the VFS service. It owns navigation policy and
 * path state, publishes a table model to the compositor and opens text files
 * in a shared preview view. It never draws pixels or touches the framebuffer.
 */
#include <aurora/abi.h>
#include <aurora/user.h>
#include <aurora/model.h>
#include <aurora/file.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PATH_BYTES 80

static uint64_t g_display_tid;
static uint64_t g_vfs_tid;
static char g_path[PATH_BYTES] = "/home/user";
static fs_entry_t g_entries[FS_MAX_ENTRIES];
static uint32_t g_entry_count;
static fs_entry_t g_pending_entries[FS_MAX_ENTRIES];
static char g_pending_path[PATH_BYTES];
static uint32_t g_pending_count;
static uint64_t g_list_request;
static uint64_t g_read_request;
static uint64_t g_table_sequence;
static file_client_t binary_files;
static uint64_t audio_request, audio_handle;
static bool audio_stopping;
static char g_status[TABLE_CAPTION_BYTES + 1];

static char g_preview_title[PATH_BYTES];
static uint32_t g_preview_size;
static uint32_t g_preview_count;
static char g_preview_lines[FS_MAX_PREVIEW_LINES][FS_PREVIEW_LINE_BYTES + 1];

static bool is_root(void)
{
    return g_path[0] == '/' && g_path[1] == '\0';
}

static bool join_path(char *destination, const char *base, const char *name)
{
    size_t base_length = strlen(base);
    size_t name_length = strlen(name);
    bool base_is_root = base[0] == '/' && base[1] == '\0';
    size_t slash = base_is_root || base_length == 0 ? 0 : 1;
    if (base_length + slash + name_length + 1 > PATH_BYTES) {
        return false;
    }
    size_t used = 0;
    for (size_t index = 0; index < base_length; index++) {
        destination[used++] = base[index];
    }
    if (base_length == 0) {
        destination[used++] = '/';
    }
    if (slash != 0) {
        destination[used++] = '/';
    }
    for (size_t index = 0; index < name_length; index++) {
        destination[used++] = name[index];
    }
    destination[used] = '\0';
    return true;
}

static void parent_path(char *destination)
{
    size_t length = strlen(g_path);
    while (length > 1 && g_path[length - 1] != '/') {
        length--;
    }
    while (length > 1 && g_path[length - 1] == '/') {
        length--;
    }
    if (length == 0) {
        length = 1;
    }
    size_t index = 0;
    while (index < length && index + 1 < PATH_BYTES) {
        destination[index] = g_path[index];
        index++;
    }
    destination[index] = '\0';
}

static void send_message(uint64_t destination, uint64_t type, uint64_t arg0, uint64_t arg1,
                         const void *payload, size_t length)
{
    ipc_message_t message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.arg0 = arg0;
    message.arg1 = arg1;
    message.arg3 = g_table_sequence;
    if (payload != NULL && length > 0) {
        if (length > sizeof(message.data)) {
            length = sizeof(message.data);
        }
        memcpy(message.data, payload, length);
    }
    (void)user_send((int64_t)destination, &message);
}

static void request_list(const char *path)
{
    if (strlen(path) >= PATH_BYTES) {
        user_log("files: path too long");
        return;
    }
    char text[96] = "files: list ";
    text_append(text, sizeof(text), path);
    user_log(text);
    g_list_request++;
    send_message(g_vfs_tid, MSG_FS_LIST, g_list_request, 0, path, strlen(path) + 1);
}

static void play_wav(const char *path)
{
    int64_t audio = sys_service(IMAGE_AUDIO);
    uint32_t size;
    int64_t handle = file_open(&binary_files, path, &size);
    if (handle < 0) {
        text_copy(g_status, sizeof(g_status), "Cannot read WAV");
        return;
    }
    int64_t delegated = file_delegate(&binary_files, (uint64_t)handle, audio);
    file_close(&binary_files, (uint64_t)handle);
    if (delegated < 0) {
        text_copy(g_status, sizeof(g_status), "Audio unavailable");
        return;
    }
    audio_stopping = false;
    audio_handle = (uint64_t)delegated;
    ipc_message_t msg = {.type = MSG_AUDIO_WAV, .arg0 = ++audio_request, .arg1 = audio_handle};
    if (user_send(audio, &msg) < 0) {
        file_revoke(&binary_files, audio_handle);
        text_copy(g_status, sizeof(g_status), "Audio unavailable");
    } else
        text_copy(g_status, sizeof(g_status), "Opening WAV...");
}
static void request_preview(const char *path)
{
    if (strlen(path) >= PATH_BYTES) {
        user_log("files: path too long");
        return;
    }
    char text[96] = "files: preview ";
    text_append(text, sizeof(text), path);
    user_log(text);
    g_read_request++;
    send_message(g_vfs_tid, MSG_FS_READ, g_read_request, 0, path, strlen(path) + 1);
}

static void send_table_header(uint32_t view, const char *title, const char *caption, uint32_t rows)
{
    g_table_sequence++;
    table_header_t header;
    memset(&header, 0, sizeof(header));
    text_copy(header.title, sizeof(header.title), title);
    text_copy(header.caption, sizeof(header.caption), caption);
    send_message(g_display_tid, MSG_TABLE_BEGIN, view, rows, &header, sizeof(header));
}

static void send_row(uint32_t view, uint32_t index, const char *label, const char *value,
                     uint32_t kind)
{
    table_row_t row;
    memset(&row, 0, sizeof(row));
    text_copy(row.label, sizeof(row.label), label);
    text_copy(row.value, sizeof(row.value), value);
    row.kind = (uint8_t)kind;
    send_message(g_display_tid, MSG_TABLE_ROW, view, index, &row, sizeof(row));
}

static void send_table_end(uint32_t view, uint32_t rows)
{
    send_message(g_display_tid, MSG_TABLE_END, view, rows, NULL, 0);
}

static void format_entry_value(char *buffer, size_t capacity, const fs_entry_t *entry)
{
    buffer[0] = '\0';
    if (entry->type == FS_NODE_DIR) {
        text_append(buffer, capacity, "folder");
        if (entry->child_count > 0) {
            text_append(buffer, capacity, " \xC2\xB7 ");
            text_u64(buffer, capacity, entry->child_count);
            text_append(buffer, capacity, " items");
        }
    } else {
        text_u64(buffer, capacity, entry->size);
        text_append(buffer, capacity, " bytes");
    }
}

static void publish_files(void)
{
    char title[TABLE_TITLE_BYTES + 1];
    char caption[TABLE_CAPTION_BYTES + 1];
    char value[TABLE_VALUE_BYTES + 1];

    text_copy(title, sizeof(title), g_path);
    text_copy(caption, sizeof(caption), "");
    text_u64(caption, sizeof(caption), g_entry_count);
    text_append(caption, sizeof(caption), " items");
    if (g_status[0])
        text_copy(caption, sizeof(caption), g_status);

    bool parent = !is_root();
    uint32_t rows = g_entry_count + (parent ? 1 : 0);
    send_table_header(VIEW_FILES, title, caption, rows);

    uint32_t row = 0;
    if (parent) {
        send_row(VIEW_FILES, row++, "..", "parent folder", TABLE_ROW_DIR);
    }
    for (uint32_t index = 0; index < g_entry_count; index++) {
        const fs_entry_t *entry = &g_entries[index];
        format_entry_value(value, sizeof(value), entry);
        send_row(VIEW_FILES, row++, entry->name, value,
                 entry->type == FS_NODE_DIR ? TABLE_ROW_DIR : TABLE_ROW_FILE);
    }
    send_table_end(VIEW_FILES, rows);
}

static void publish_preview(void)
{
    char title[TABLE_TITLE_BYTES + 1];
    char caption[TABLE_CAPTION_BYTES + 1];

    text_copy(title, sizeof(title), "Preview \xE2\x80\x94 ");
    const char *slash = g_preview_title;
    for (const char *cursor = g_preview_title; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            slash = cursor + 1;
        }
    }
    text_append(title, sizeof(title), slash);
    text_copy(caption, sizeof(caption), "read-only \xC2\xB7 ");
    text_u64(caption, sizeof(caption), g_preview_count);
    text_append(caption, sizeof(caption), " lines \xC2\xB7 ");
    text_u64(caption, sizeof(caption), g_preview_size);
    text_append(caption, sizeof(caption), " B");

    send_table_header(VIEW_PREVIEW, title, caption, g_preview_count);
    for (uint32_t index = 0; index < g_preview_count; index++) {
        const char *line = g_preview_lines[index];
        char label[TABLE_LABEL_BYTES + 1];
        char value[TABLE_VALUE_BYTES + 1];
        text_copy(label, sizeof(label), line);
        const char *rest = line;
        size_t first = strlen(label);
        if (first > 0) {
            rest = line + first;
        }
        text_copy(value, sizeof(value), rest);
        send_row(VIEW_PREVIEW, index, label, value, TABLE_ROW_LINE);
    }
    send_table_end(VIEW_PREVIEW, g_preview_count);
}

static void handle_display(const ipc_message_t *message)
{
    if (message->type == MSG_VIEW_ACTIVATE && (uint32_t)message->arg0 == VIEW_FILES) {
        uint32_t row = (uint32_t)message->arg1;
        uint32_t offset = is_root() ? 0 : 1;
        if (offset != 0 && row == 0) {
            char parent[PATH_BYTES];
            parent_path(parent);
            request_list(parent);
            return;
        }
        uint32_t index = row >= offset ? row - offset : 0xFFFFFFFFu;
        if (index >= g_entry_count) {
            return;
        }
        char path[PATH_BYTES];
        if (!join_path(path, g_path, g_entries[index].name)) {
            user_log("files: joined path too long");
            return;
        }
        if (g_entries[index].type == FS_NODE_DIR) {
            request_list(path);
        } else {
            size_t n = strlen(path);
            if (n > 4 && !strcmp(path + n - 4, ".wav")) {
                play_wav(path);
                publish_files();
            } else
                request_preview(path);
        }
    } else if (message->type == MSG_VIEW_KEY && message->arg1 == 0x39) {
        audio_stopping = true;
        ipc_message_t msg = {.type = MSG_AUDIO_STOP, .arg0 = ++audio_request};
        user_send(sys_service(IMAGE_AUDIO), &msg);
        text_copy(g_status, sizeof(g_status), "Playback stopped");
        publish_files();
    } else if (message->type == MSG_VIEW_PATH) {
        char path[PATH_BYTES];
        memcpy(path, message->data, sizeof(path));
        path[PATH_BYTES - 1] = 0;
        request_list(path);
    } else if (message->type == MSG_VIEW_REFRESH) {
        request_list(g_path);
    } else if (message->type == MSG_VIEW_BACK) {
        if (!is_root()) {
            char parent[PATH_BYTES];
            parent_path(parent);
            request_list(parent);
        }
    }
}

static void handle_vfs(const ipc_message_t *message)
{
    if (message->type == MSG_FS_LIST_BEGIN) {
        if (message->arg0 != g_list_request) {
            return;
        }
        g_pending_count = 0;
        memset(g_pending_entries, 0, sizeof(g_pending_entries));
        char path[PATH_BYTES];
        memcpy(path, message->data, PATH_BYTES - 1);
        path[PATH_BYTES - 1] = '\0';
        text_copy(g_pending_path, sizeof(g_pending_path), path);
    } else if (message->type == MSG_FS_LIST_ENTRY) {
        if (message->arg0 != g_list_request) {
            return;
        }
        uint32_t index = (uint32_t)message->arg1;
        if (index < FS_MAX_ENTRIES) {
            memcpy(&g_pending_entries[index], message->data, sizeof(fs_entry_t));
            g_pending_entries[index].name[FS_NAME_BYTES] = '\0';
            if (index + 1 > g_pending_count) {
                g_pending_count = index + 1;
            }
        }
    } else if (message->type == MSG_FS_LIST_END) {
        if (message->arg0 != g_list_request) {
            return;
        }
        uint32_t count = (uint32_t)message->arg1;
        if (count > FS_MAX_ENTRIES) {
            count = FS_MAX_ENTRIES;
        }
        if (count > g_pending_count) {
            count = g_pending_count;
        }
        text_copy(g_path, sizeof(g_path), g_pending_path);
        memcpy(g_entries, g_pending_entries, sizeof(g_entries));
        g_entry_count = count;
        g_status[0] = 0;
        publish_files();
    } else if (message->type == MSG_FS_PREVIEW_BEGIN) {
        if (message->arg0 != g_read_request) {
            return;
        }
        g_preview_count = 0;
        g_preview_size = (uint32_t)message->arg2;
        memset(g_preview_lines, 0, sizeof(g_preview_lines));
        char path[PATH_BYTES];
        memcpy(path, message->data, PATH_BYTES - 1);
        path[PATH_BYTES - 1] = '\0';
        text_copy(g_preview_title, sizeof(g_preview_title), path);
    } else if (message->type == MSG_FS_PREVIEW_LINE) {
        if (message->arg0 != g_read_request) {
            return;
        }
        uint32_t index = (uint32_t)message->arg1;
        if (index < FS_MAX_PREVIEW_LINES) {
            char line[FS_PREVIEW_LINE_BYTES + 1];
            memcpy(line, message->data, FS_PREVIEW_LINE_BYTES);
            line[FS_PREVIEW_LINE_BYTES] = '\0';
            text_copy(g_preview_lines[index], sizeof(g_preview_lines[index]), line);
            if (index + 1 > g_preview_count) {
                g_preview_count = index + 1;
            }
        }
    } else if (message->type == MSG_FS_PREVIEW_END) {
        if (message->arg0 != g_read_request) {
            return;
        }
        uint32_t count = (uint32_t)message->arg1;
        if (count > FS_MAX_PREVIEW_LINES) {
            count = FS_MAX_PREVIEW_LINES;
        }
        if (count > g_preview_count) {
            count = g_preview_count;
        }
        g_preview_count = count;
        publish_preview();
    } else if (message->type == MSG_FS_ERROR) {
        if (message->arg0 != g_list_request && message->arg0 != g_read_request) {
            return;
        }
        char text[80] = "files: vfs error: ";
        char detail[80];
        memcpy(detail, message->data, 79);
        detail[79] = 0;
        text_append(text, sizeof(text), detail);
        user_log(text);
        text_copy(g_status, sizeof(g_status), detail);
        publish_files();
    }
}

int umain(uint64_t vfs_tid)
{
    int64_t parent = sys_service(IMAGE_DISPLAY);
    vfs_tid = (uint64_t)sys_service(IMAGE_VFS);
    if (parent <= 0 || vfs_tid == 0) {
        user_log("files: missing display or vfs endpoint");
        return 1;
    }
    system_info_t info;
    if (sys_info(&info) < 0)
        return 1;
    account_info_t account = aurora_account(info.uid);
    if (account.uid == AURORA_NO_USER)
        return 1;
    text_copy(g_path, sizeof(g_path), account.home);
    g_display_tid = (uint64_t)parent;
    g_vfs_tid = vfs_tid;
    file_client_init(&binary_files, NULL, NULL);
    user_log("files: online, vfs client");
    request_list(g_path);

    for (;;) {
        ipc_message_t message;
        int budget = 64;
        while (budget-- > 0 && sys_recv(&message) == 0) {
            if (message.from == g_display_tid) {
                handle_display(&message);
            } else if (message.from == (uint64_t)sys_service(IMAGE_AUDIO) &&
                       message.type == MSG_AUDIO_RESULT && message.arg0 == audio_request) {
                if ((int64_t)message.arg1 < 0) {
                    file_revoke(&binary_files, audio_handle);
                    text_copy(g_status, sizeof(g_status), "WAV rejected or device busy");
                } else
                    text_copy(g_status, sizeof(g_status),
                              audio_stopping ? "Playback stopped" : "Audio active / Space to stop");
                publish_files();
            } else if (message.from == g_vfs_tid) {
                handle_vfs(&message);
            }
        }
        sys_wait(100);
    }
}
