/* Small command terminal; commands use service IPC, never device or kernel internals. */
#include <aurora/model.h>
#include <aurora/keymap.h>
#include <aurora/fs.h>
#include <aurora/file.h>
#include <aurora/fs_validate.h>
#include <aurora/version.h>
#include <string.h>
#define HISTORY 14
#define LINE 63
static char history[HISTORY][LINE], command[64], cwd[80] = "/home/user";
static unsigned history_count, command_len;
static user_keymap_t keymap;
static account_info_t account;
static uint64_t sequence;
static int64_t vfs;
static file_client_t binary_files;
static uint64_t key_queue[128];
static unsigned key_head, key_tail, key_count;
static void queue_key(uint64_t code)
{
    if (key_count == 128)
        return;
    key_queue[key_tail] = code;
    key_tail = (key_tail + 1) % 128;
    key_count++;
}

static void line(const char *text)
{
    do {
        if (history_count == HISTORY) {
            memmove(history, history + 1, (HISTORY - 1) * LINE);
            history_count--;
        }
        unsigned n = 0;
        while (text[n] && n < LINE - 1) {
            history[history_count][n] = text[n];
            n++;
        }
        history[history_count++][n] = 0;
        text += n;
    } while (*text);
}
static void publish(void)
{
    table_row_t rows[TABLE_MAX_ROWS] = {0};
    for (unsigned i = 0; i < history_count; i++) {
        text_copy(rows[i].label, sizeof(rows[i].label), history[i]);
        text_copy(rows[i].value, sizeof(rows[i].value), history[i] + strlen(rows[i].label));
        rows[i].kind = TABLE_ROW_LINE;
    }
    char prompt[LINE] = "";
    text_append(prompt, sizeof(prompt), account.name);
    text_append(prompt, sizeof(prompt), account.uid == 0 ? "# " : "$ ");
    text_append(prompt, sizeof(prompt), command);
    text_append(prompt, sizeof(prompt), "_");
    text_copy(rows[history_count].label, 24, prompt);
    text_copy(rows[history_count].value, 40, prompt + strlen(rows[history_count].label));
    rows[history_count].kind = TABLE_ROW_LINE;
    model_publish(VIEW_TERMINAL, cwd, "Local session", rows, history_count + 1);
}
static bool receive(int64_t peer, uint64_t request, ipc_message_t *msg)
{
    uint64_t until = (uint64_t)sys_get_ticks() + 300;
    do {
        while (sys_recv(msg) == 0) {
            if (msg->from == (uint64_t)sys_service(IMAGE_DISPLAY) && msg->type == MSG_VIEW_KEY &&
                msg->arg0 == VIEW_TERMINAL)
                queue_key(msg->arg1);
            if (msg->from == (uint64_t)peer && msg->arg0 == request)
                return true;
        }
        sys_wait(10);
    } while ((uint64_t)sys_get_ticks() < until);
    line("Service timeout. Try again.");
    return false;
}
static bool path_request(uint64_t type, const char *path, uint64_t *request)
{
    ipc_message_t msg = {.type = type, .arg0 = ++sequence};
    text_copy((char *)msg.data, sizeof(msg.data), path);
    *request = sequence;
    if (user_send(vfs, &msg) < 0) {
        line("File service unavailable.");
        return false;
    }
    return true;
}
static bool read_path(const char *path, bool preview, bool change_dir)
{
    uint64_t request;
    if (!path_request(preview ? MSG_FS_READ : MSG_FS_LIST, path, &request))
        return false;
    ipc_message_t msg;
    while (receive(vfs, request, &msg)) {
        if (msg.type == MSG_FS_ERROR) {
            char text[80];
            memcpy(text, msg.data, 79);
            text[79] = 0;
            line(text);
            return false;
        }
        if (msg.type == MSG_FS_LIST_ENTRY && !change_dir) {
            fs_entry_t entry;
            memcpy(&entry, msg.data, sizeof(entry));
            entry.name[39] = 0;
            char text[64] = "  ";
            text_append(text, sizeof(text), entry.name);
            if (entry.type == FS_NODE_DIR)
                text_append(text, sizeof(text), "/");
            line(text);
        }
        if (msg.type == MSG_FS_PREVIEW_LINE) {
            char text[80];
            memcpy(text, msg.data, 79);
            text[79] = 0;
            line(text);
        }
        if (msg.type == MSG_FS_LIST_END || msg.type == MSG_FS_PREVIEW_END)
            return true;
    }
    return false;
}
static void mutate(uint64_t type, const char *path, const char *text)
{
    uint64_t request = ++sequence;
    ipc_message_t msg = {.type = type, .arg0 = request, .arg1 = strlen(text)};
    text_copy((char *)msg.data, sizeof(msg.data), path);
    if (user_send(vfs, &msg) < 0) {
        line("File service unavailable.");
        return;
    }
    if (type == MSG_FS_WRITE_BEGIN) {
        memset(&msg, 0, sizeof(msg));
        msg.type = MSG_FS_WRITE_DATA;
        msg.arg0 = request;
        msg.arg2 = strlen(text);
        memcpy(msg.data, text, (size_t)msg.arg2);
        if (user_send(vfs, &msg) < 0) {
            line("Write interrupted.");
            return;
        }
        msg.type = MSG_FS_WRITE_COMMIT;
        if (user_send(vfs, &msg) < 0) {
            line("Write interrupted.");
            return;
        }
    }
    if (receive(vfs, request, &msg)) {
        line(msg.type == MSG_FS_RESULT && !msg.arg1 ? "Done (RAM only; resets on reboot)."
             : msg.arg1 == FS_ERR_READ_ONLY ? "Read-only location. Use /home/user or /tmp."
                                            : "Request rejected (path, capacity or permissions).");
    }
}
static void file_event(const ipc_message_t *msg, void *context)
{
    (void)context;
    if (msg->from == (uint64_t)sys_service(IMAGE_DISPLAY) && msg->type == MSG_VIEW_KEY &&
        msg->arg0 == VIEW_TERMINAL)
        queue_key(msg->arg1);
}
static bool default_music(char path[80])
{
    char directory[80];
    text_copy(directory, sizeof(directory), account.home);
    text_append(directory, sizeof(directory), "/Music");
    uint64_t request;
    if (!path_request(MSG_FS_LIST, directory, &request))
        return false;
    ipc_message_t msg;
    bool found = false;
    while (receive(vfs, request, &msg)) {
        if (msg.type == MSG_FS_ERROR)
            return false;
        if (msg.type == MSG_FS_LIST_ENTRY && !found) {
            fs_entry_t entry;
            memcpy(&entry, msg.data, sizeof(entry));
            entry.name[39] = 0;
            size_t n = strlen(entry.name);
            if (entry.type == FS_NODE_FILE && n > 4 && !strcmp(entry.name + n - 4, ".wav")) {
                text_copy(path, 80, directory);
                text_append(path, 80, "/");
                text_append(path, 80, entry.name);
                found = true;
            }
        }
        if (msg.type == MSG_FS_LIST_END)
            return found;
    }
    return false;
}
static void play_file(const char *argument)
{
    char path[80];
    if (!*argument) {
        if (!default_music(path)) {
            line("No WAV file in your Music directory.");
            return;
        }
    } else if (!aurora_path_normalize(cwd, argument, path, sizeof(path))) {
        line("Invalid path.");
        return;
    }
    int64_t audio = sys_service(IMAGE_AUDIO);
    uint32_t size;
    int64_t handle = file_open(&binary_files, path, &size);
    if (handle < 0) {
        char log[80] = "terminal: audio open failed ";
        text_u64(log, sizeof(log), (uint64_t)-handle);
        user_log(log);
        line("Cannot open audio file (missing or denied).");
        return;
    }
    int64_t delegated = file_delegate(&binary_files, (uint64_t)handle, audio);
    file_close(&binary_files, (uint64_t)handle);
    if (delegated < 0) {
        user_log("terminal: audio delegation failed");
        line("Cannot grant audio service read access.");
        return;
    }
    ipc_message_t msg = {.type = MSG_AUDIO_WAV, .arg0 = ++sequence, .arg1 = (uint64_t)delegated};
    if (user_send(audio, &msg) < 0 || !receive(audio, sequence, &msg) ||
        msg.type != MSG_AUDIO_RESULT || (int64_t)msg.arg1 < 0) {
        file_revoke(&binary_files, (uint64_t)delegated);
        line("WAV rejected: unsupported, busy, denied or no device.");
        return;
    }
    user_log("terminal: playback request accepted");
    line("Playing WAV. Use audio for status, stop to stop.");
}
static void audio_command(bool stop_playback)
{
    int64_t audio = sys_service(IMAGE_AUDIO);
    ipc_message_t msg = {.type = stop_playback ? MSG_AUDIO_STOP : MSG_AUDIO_STATUS,
                         .arg0 = ++sequence};
    if (user_send(audio, &msg) < 0 || !receive(audio, sequence, &msg)) {
        line("Audio service unavailable.");
        return;
    }
    if (stop_playback) {
        line((int64_t)msg.arg1 < 0 ? "Stop denied." : "Playback stopped.");
        return;
    }
    if (msg.type != MSG_AUDIO_INFO) {
        line("Audio status unavailable.");
        return;
    }
    audio_info_t info;
    memcpy(&info, msg.data, sizeof(info));
    char text[64] = "";
    text_copy(text, sizeof(text), info.playing ? "Playing " : "Idle ");
    text_u64(text, sizeof(text), info.sample_rate);
    text_append(text, sizeof(text), " Hz / ");
    text_u64(text, sizeof(text), info.bits);
    text_append(text, sizeof(text), " bit / ");
    text_u64(text, sizeof(text), info.channels);
    text_append(text, sizeof(text), " ch");
    line(text);
    text[0] = 0;
    text_u64(text, sizeof(text), info.position_frames / PCM_RATE);
    text_append(text, sizeof(text), " / ");
    text_u64(text, sizeof(text), info.total_frames / PCM_RATE);
    text_append(text, sizeof(text), " seconds");
    line(text);
}
static void metadata(const char *path, bool change, uint32_t mode)
{
    uint64_t request = ++sequence;
    ipc_message_t msg = {
        .type = change ? MSG_FS_CHMOD : MSG_FS_STAT, .arg0 = request, .arg1 = mode};
    text_copy((char *)msg.data, sizeof(msg.data), path);
    if (user_send(vfs, &msg) < 0 || !receive(vfs, request, &msg)) {
        line("File service unavailable.");
        return;
    }
    if (change) {
        line(msg.type == MSG_FS_RESULT && msg.arg1 == 0 ? "Permissions updated (RAM session)."
                                                        : "Permission change denied.");
        return;
    }
    if (msg.type != MSG_FS_STAT_RESULT) {
        line("Cannot inspect path (missing or denied).");
        return;
    }
    fs_stat_t info;
    memcpy(&info, msg.data, sizeof(info));
    char value[64] = "uid=";
    text_u64(value, sizeof(value), info.uid);
    text_append(value, sizeof(value), " gid=");
    text_u64(value, sizeof(value), info.gid);
    text_append(value, sizeof(value), " mode=");
    char octal[5];
    for (unsigned i = 0; i < 4; i++)
        octal[i] = (char)('0' + ((info.mode >> ((3 - i) * 3)) & 7));
    octal[4] = 0;
    text_append(value, sizeof(value), octal);
    line(value);
}
static void run_command(void)
{
    char echo[80] = "$ ";
    text_append(echo, sizeof(echo), command);
    line(echo);
    char *arg = command;
    while (*arg && *arg != ' ')
        arg++;
    if (*arg)
        *arg++ = 0;
    while (*arg == ' ')
        arg++;
    if (!command[0])
        return;
    if (!strcmp(command, "help")) {
        line("help  clear  pwd  ls [path]  cd <path>  cat <file>");
        line("write <file> <text>  mkdir <path>  rm <path>");
        line("ps  info  caps  beep  whoami  id  logout");
        line("stat <path>  chmod <octal-mode> <path>");
        line("play [path]  audio  stop   (play defaults to Music WAV)");
        line("Writes are temporary. No shell pipelines or executables yet.");
    } else if (!strcmp(command, "clear"))
        history_count = 0;
    else if (!strcmp(command, "pwd"))
        line(cwd);
    else if (!strcmp(command, "info") || !strcmp(command, "caps")) {
        system_info_t info;
        if (sys_info(&info) < 0)
            return;
        line("Aurora " AURORA_VERSION_STRING " / x86_64 / ABI 4");
        char text[64] = "Uptime ";
        text_u64(text, sizeof(text), info.uptime_ticks / info.tick_hz);
        text_append(text, sizeof(text), "s   Free memory ");
        text_u64(text, sizeof(text), info.free_memory_bytes / 1024 / 1024);
        text_append(text, sizeof(text), " MiB");
        line(text);
        line("Grants: file read/write, inspect, audio requests");
        line("No framebuffer, raw input, spawn or task-control access.");
        user_log("terminal: info command completed");
    } else if (!strcmp(command, "whoami") || !strcmp(command, "id")) {
        system_info_t info;
        sys_info(&info);
        char text[64] = "uid=";
        text_u64(text, sizeof(text), info.uid);
        text_append(text, sizeof(text), " gid=");
        text_u64(text, sizeof(text), info.gid);
        text_append(text, sizeof(text), " user=");
        text_append(text, sizeof(text), account.name);
        line(text);
        user_log(text);
    } else if (!strcmp(command, "logout")) {
        ipc_message_t msg = {.type = MSG_AUTH_LOGOUT};
        if (user_send(sys_service(IMAGE_AUTH), &msg) < 0)
            line("Unable to log out.");
    } else if (!strcmp(command, "play"))
        play_file(arg);
    else if (!strcmp(command, "audio"))
        audio_command(false);
    else if (!strcmp(command, "stop"))
        audio_command(true);
    else if (!strcmp(command, "ps")) {
        thread_info_t threads[12];
        int64_t n = sys_thread_list(threads, 12);
        for (int i = 0; i < n; i++) {
            char text[64] = "";
            text_u64(text, sizeof(text), threads[i].tid);
            text_append(text, sizeof(text), "  ");
            text_append(text, sizeof(text), threads[i].name);
            text_append(text, sizeof(text), "  ");
            text_u64(text, sizeof(text), threads[i].memory_bytes / 1024);
            text_append(text, sizeof(text), " KiB");
            line(text);
        }
    } else if (!strcmp(command, "beep")) {
        int64_t audio = sys_service(IMAGE_AUDIO);
        ipc_message_t msg = {.type = MSG_AUDIO_PLAY, .arg0 = ++sequence, .arg1 = 660, .arg2 = 20};
        if (audio > 0 && user_send(audio, &msg) == 0 && receive(audio, sequence, &msg))
            line(msg.arg1 == 0 ? "Notification tone played." : "Audio device busy.");
        else
            line("Audio service unavailable.");
    } else if (!strcmp(command, "chmod")) {
        uint32_t mode = 0;
        unsigned digits = 0;
        while (*arg >= '0' && *arg <= '7') {
            mode = mode * 8 + (unsigned)(*arg++ - '0');
            digits++;
        }
        if (!digits || digits > 4 || *arg != ' ' || mode > 01777) {
            line("Usage: chmod 600 path");
            return;
        }
        while (*arg == ' ')
            arg++;
        char path[80];
        if (!*arg || !aurora_path_normalize(cwd, arg, path, sizeof(path))) {
            line("Invalid path.");
            return;
        }
        metadata(path, true, mode);
    } else {
        char *body = arg;
        if (!strcmp(command, "write")) {
            while (*body && *body != ' ')
                body++;
            if (*body)
                *body++ = 0;
        }
        char path[80];
        if (!aurora_path_normalize(cwd, arg, path, sizeof(path))) {
            line("Invalid or long path.");
            return;
        }
        if (!strcmp(command, "stat"))
            metadata(path, false, 0);
        else if (!strcmp(command, "ls"))
            read_path(path, false, false);
        else if (!strcmp(command, "cd")) {
            if (read_path(path, false, true))
                strcpy(cwd, path);
        } else if (!strcmp(command, "cat"))
            read_path(path, true, false);
        else if (!strcmp(command, "write"))
            mutate(MSG_FS_WRITE_BEGIN, path, body);
        else if (!strcmp(command, "mkdir"))
            mutate(MSG_FS_MKDIR, path, "");
        else if (!strcmp(command, "rm"))
            mutate(MSG_FS_REMOVE, path, "");
        else
            line("Unknown command. Type help.");
    }
}
static void key(uint64_t value)
{
    char ch = user_key_ascii(&keymap, (uint16_t)value);
    if (value & 0x100)
        return;
    uint8_t raw = (uint8_t)value, code = raw & 0x7f;
    if (raw & 0x80)
        return;
    if (code == 14) {
        if (command_len)
            command[--command_len] = 0;
        return;
    }
    if (code == 28) {
        run_command();
        command_len = 0;
        command[0] = 0;
        return;
    }
    if (ch && command_len < 58) {
        command[command_len++] = ch;
        command[command_len] = 0;
    }
}
int umain(uint64_t argument)
{
    (void)argument;
    system_info_t info;
    if (sys_info(&info) < 0)
        return 1;
    account = aurora_account(info.uid);
    if (account.uid == AURORA_NO_USER)
        return 1;
    text_copy(cwd, sizeof(cwd), account.home);
    vfs = sys_service(IMAGE_VFS);
    file_client_init(&binary_files, file_event, NULL);
    if (vfs < 0)
        return 1;
    line("Aurora Terminal");
    line("Type help to explore. Use id to inspect your account.");
    line("RAM session: changes reset on reboot.");
    user_log("terminal: online");
    publish();
    for (;;) {
        ipc_message_t msg;
        bool changed = false;
        while (sys_recv(&msg) == 0) {
            if (msg.from == (uint64_t)sys_service(IMAGE_DISPLAY) && msg.type == MSG_VIEW_KEY &&
                msg.arg0 == VIEW_TERMINAL) {
                queue_key(msg.arg1);
            }
        }
        while (key_count) {
            uint64_t code = key_queue[key_head];
            key_head = (key_head + 1) % 128;
            key_count--;
            key(code);
            changed = true;
        }
        if (changed)
            publish();
        sys_wait(100);
    }
}
