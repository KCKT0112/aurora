#include <aurora/model.h>
#include <string.h>
void text_copy(char *dst, size_t capacity, const char *src)
{
    if (!capacity)
        return;
    size_t i = 0;
    while (i + 1 < capacity && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}
void text_append(char *dst, size_t capacity, const char *src)
{
    size_t n = strlen(dst);
    if (n < capacity)
        text_copy(dst + n, capacity - n, src);
}
void text_u64(char *dst, size_t capacity, uint64_t value)
{
    char reversed[21], digits[21];
    unsigned n = 0, j = 0;
    do {
        reversed[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        digits[j++] = reversed[--n];
    digits[j] = 0;
    text_append(dst, capacity, digits);
}
bool model_publish(uint32_t view, const char *title, const char *caption, const table_row_t *rows,
                   uint32_t count)
{
    int64_t display = sys_service(IMAGE_DISPLAY);
    if (display < 0 || count > TABLE_MAX_ROWS)
        return false;
    static uint64_t sequence;
    uint64_t transaction = ++sequence;
    table_header_t header = {0};
    text_copy(header.title, sizeof(header.title), title);
    text_copy(header.caption, sizeof(header.caption), caption);
    ipc_message_t msg = {.type = MSG_TABLE_BEGIN, .arg0 = view, .arg1 = count, .arg3 = transaction};
    memcpy(msg.data, &header, sizeof(header));
    if (user_send(display, &msg) < 0)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        memset(&msg, 0, sizeof(msg));
        msg.type = MSG_TABLE_ROW;
        msg.arg0 = view;
        msg.arg1 = i;
        msg.arg3 = transaction;
        memcpy(msg.data, &rows[i], sizeof(rows[i]));
        if (user_send(display, &msg) < 0)
            return false;
    }
    msg.type = MSG_TABLE_END;
    msg.arg1 = count;
    return user_send(display, &msg) == 0;
}
