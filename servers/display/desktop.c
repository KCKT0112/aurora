/* Aurora workspace: retained model views, bounded damage composition and local cursor.
 * All file/process/terminal policies stay in their respective user processes. */
#include "desktop.h"
#include "login.h"
#include <aurora/keymap.h>
#include <aurora/model.h>
#include <aurora/version.h>
#include <string.h>
#include <ui/draw.h>
#include <ui/vector.h>
#include <aurora/geometry.h>
#include <ui/font.h>

#define MAX_W 3840
#define MAX_H 2160
#define TOP 48
#define BAR 44
#define SIDEBAR 168
#define ROW_H 32
#define ROW_TOP 142
#define WINDOWS 5
#define SETTINGS 4
#define FILES VIEW_FILES
#define TASK VIEW_TASK
#define TERMINAL VIEW_TERMINAL
#define PREVIEW VIEW_PREVIEW
#define RGB(c) (uint8_t)((c) >> 16), (uint8_t)((c) >> 8), (uint8_t)(c)
#define INK 0x202b38u
#define MUTED 0x74808cu
#define ACCENT 0x39766bu
#define LINE 0xe2e5e6u
#define PAPER 0xfafbfau
#define CW 12
#define CH 18

typedef aurora_rect_t rect_t;
typedef struct {
    rect_t rect, restore;
    bool visible, maximized;
} window_t;
typedef struct {
    table_header_t header, pending_header;
    table_row_t rows[TABLE_MAX_ROWS], pending[TABLE_MAX_ROWS];
    uint64_t owner, sequence;
    uint32_t count, expected, mask;
    int selected, scroll;
    bool valid, staging;
} model_t;
static ui_surface_t fb, canvas;
static ui_font_t font;
static uint32_t pixels[MAX_W * MAX_H], wallpaper[MAX_W * MAX_H];
static window_t windows[WINDOWS];
static model_t models[VIEW_COUNT];
static credentials_t session = {AURORA_NO_USER, AURORA_NO_USER, 0, 0};
static account_info_t account;
static char home_paths[4][80];
static int zorder[WINDOWS], focus = FILES;
static int width, height, cursor_x, cursor_y, drag = -1, drag_x, drag_y;
static bool dirty, cursor_drawn, extended, cursor_dirty;
static int presented_x, presented_y;
static unsigned display_scale = 1;
static bool mode_transition;
static int resizing = -1, resize_x, resize_y;
static unsigned resize_edges;
static rect_t resize_start;
static uint8_t buttons;
static uint32_t cursor_saved[CW * CH * 4];
static rect_t damage;
static uint64_t ticks, last_click;
static int click_view = -1, click_row = -1;
static const char *titles[WINDOWS] = {"System Monitor", "Files", "Preview", "Terminal", "Settings"};
static const int dock_order[4] = {FILES, TERMINAL, TASK, SETTINGS};
static const char *places[] = {"Home",   "Documents", "Music",      "Pictures",
                               "System", "Temporary", "File system"};
static const char *paths[] = {"/home/user",
                              "/home/user/Documents",
                              "/home/user/Music",
                              "/home/user/Pictures",
                              "/system",
                              "/tmp",
                              "/"};

static int min(int a, int b)
{
    return a < b ? a : b;
}
static int max(int a, int b)
{
    return a > b ? a : b;
}
static int clamp(int a, int lo, int hi)
{
    return max(lo, min(a, hi));
}
static bool contains(rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static bool intersects(rect_t a, rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
static void fill(int x, int y, int w, int h, uint32_t c)
{
    ui_fill_rect(&canvas, x, y, w, h, RGB(c), 255);
}
static void roundbox(int x, int y, int w, int h, int r, uint32_t c)
{
    ui_fill_round_rect(&canvas, x, y, w, h, r, RGB(c), 255);
}
static void text(int x, int y, int size, int weight, const char *s, uint32_t c)
{
    ui_font_draw(&font, &canvas, (uint32_t)size, (uint16_t)weight, x, y, s, RGB(c), 255);
}
static int tw(int size, int weight, const char *s)
{
    return (int)ui_font_text_width(&font, (uint32_t)size, (uint16_t)weight, s);
}
static void fitted(int x, int y, int w, int size, int weight, const char *source, uint32_t c)
{
    char s[96];
    text_copy(s, sizeof(s), source);
    if (tw(size, weight, s) > w) {
        size_t n = strlen(s);
        while (n && tw(size, weight, s) + tw(size, weight, "...") > w)
            s[--n] = 0;
        text_append(s, sizeof(s), "...");
    }
    text(x, y, size, weight, s, c);
}
static void mark(rect_t r)
{
    int x = max(0, r.x), y = max(0, r.y), right = min(width, r.x + r.w),
        bottom = min(height, r.y + r.h);
    if (right <= x || bottom <= y)
        return;
    if (!dirty)
        damage = (rect_t){x, y, right - x, bottom - y};
    else {
        int oldright = damage.x + damage.w, oldbottom = damage.y + damage.h;
        damage.x = min(x, damage.x);
        damage.y = min(y, damage.y);
        damage.w = max(right, oldright) - damage.x;
        damage.h = max(bottom, oldbottom) - damage.y;
    }
    dirty = true;
}
static void mark_window(int id)
{
    rect_t r = windows[id].rect;
    r.x -= 24;
    r.y -= 24;
    r.w += 48;
    r.h += 56;
    mark(r);
}
static void all(void)
{
    mark((rect_t){0, 0, width, height});
}
static void icon(int id, int x, int y, uint32_t c)
{
    static const unsigned ids[WINDOWS] = {UI_ICON_MONITOR, UI_ICON_FOLDER, UI_ICON_DOCUMENT,
                                          UI_ICON_TERMINAL, UI_ICON_SETTINGS};
    if (id >= 0 && id < WINDOWS)
        ui_icon_draw(&canvas, ids[id], x, y, 22, c);
}
static rect_t dock_rect(void)
{
    return (rect_t){(width - 592) / 2, height - 76, 592, 56};
}
static void draw_shell(void)
{
    fill(0, 0, width, TOP, 0xf5f6f2);
    fill(0, TOP - 1, width, 1, 0xd9dfd9);
    roundbox(22, 12, 24, 24, 7, ACCENT);
    text(28, 14, 16, 600, "a", 0xffffff);
    text(58, 15, 16, 600, "Aurora", INK);
    fill(143, 16, 1, 17, 0xcdd6cf);
    text(160, 17, 13, 400, focus >= 0 ? titles[focus] : "Workspace", MUTED);
    char uptime[40] = "Up ";
    text_u64(uptime, sizeof(uptime), ticks / 6000);
    text_append(uptime, sizeof(uptime), "m");
    text(width - 235, 17, 13, 600, account.name, MUTED);
    text(width - 105, 17, 13, 600, uptime, INK);
    rect_t d = dock_rect();
    ui_shadow(&canvas, d.x, d.y, d.w, d.h, 15, false);
    roundbox(d.x, d.y, d.w, d.h, 15, 0xf6f7f3);
    for (int i = 0; i < 4; i++) {
        int id = dock_order[i], x = d.x + 8 + i * 144;
        if (focus == id && windows[id].visible)
            roundbox(x, d.y + 7, 140, 42, 10, 0xe2eae3);
        icon(id, x + 14, d.y + 17, focus == id ? ACCENT : MUTED);
        text(x + 47, d.y + 20, 13, 600, id == TASK ? "Monitor" : titles[id], INK);
        if (windows[id].visible)
            fill(x + 64, d.y + 49, 12, 2, ACCENT);
    }
}
static void chrome(int id)
{
    rect_t r = windows[id].rect;
    ui_shadow(&canvas, r.x, r.y, r.w, r.h, 12, focus == id);
    roundbox(r.x, r.y, r.w, r.h, 12, focus == id ? 0xa8bdb4 : 0xc8d1ca);
    roundbox(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 11, PAPER);
    fill(r.x + 1, r.y + BAR, r.w - 2, 1, LINE);
    icon(id, r.x + 17, r.y + 12, id == TERMINAL ? 0x657881 : ACCENT);
    text(r.x + 50, r.y + 15, 14, 600, titles[id], INK);
    int x = r.x + r.w - 102;
    fill(x + 7, r.y + 23, 10, 1, MUTED);
    fill(x + 38, r.y + 17, 10, 1, MUTED);
    fill(x + 38, r.y + 17, 1, 9, MUTED);
    fill(x + 47, r.y + 17, 1, 9, MUTED);
    fill(x + 38, r.y + 25, 10, 1, MUTED);
    for (int i = 0; i < 9; i++) {
        fill(x + 70 + i, r.y + 17 + i, 1, 1, MUTED);
        fill(x + 70 + i, r.y + 25 - i, 1, 1, MUTED);
    }
}
static void files_content(void)
{
    rect_t r = windows[FILES].rect;
    model_t *m = &models[FILES];
    fill(r.x + 1, r.y + BAR + 1, SIDEBAR - 1, r.h - BAR - 14, 0xf0f3ef);
    fill(r.x + SIDEBAR, r.y + BAR, 1, r.h - BAR - 1, LINE);
    text(r.x + 22, r.y + 70, 12, 600, "PLACES", MUTED);
    for (int i = 0; i < 7; i++) {
        int y = r.y + 100 + i * 38;
        bool selected = !strcmp(m->header.title, paths[i]);
        if (selected)
            roundbox(r.x + 10, y - 7, SIDEBAR - 20, 34, 7, 0xdfe9df);
        if (i < 4)
            icon(FILES, r.x + 22, y, selected ? ACCENT : 0x8b9d8c);
        else
            icon(i == 4 ? SETTINGS : PREVIEW, r.x + 22, y, 0x8b9d8c);
        text(r.x + 55, y + 2, 13, selected ? 600 : 400, places[i], selected ? ACCENT : INK);
    }
    text(r.x + 20, r.y + r.h - 63, 12, 600, "RAM SESSION", MUTED);
    text(r.x + 20, r.y + r.h - 43, 12, 400, "Resets on reboot", MUTED);
    int x = r.x + SIDEBAR + 22, w = r.w - SIDEBAR - 44;
    roundbox(x, r.y + 61, 28, 30, 6, 0xeff2ee);
    ui_icon_draw(&canvas, UI_ICON_BACK, x + 4, r.y + 65, 22, ACCENT);
    roundbox(x + 38, r.y + 61, w - 76, 30, 6, 0xeff2ee);
    fitted(x + 50, r.y + 69, w - 100, 13, 400, m->valid ? m->header.title : "Loading...", INK);
    ui_icon_draw(&canvas, UI_ICON_REFRESH, x + w - 25, r.y + 65, 22, ACCENT);
    text(x + 2, r.y + 112, 12, 600, "NAME", MUTED);
    text(x + w - 105, r.y + 112, 12, 600, "DETAILS", MUTED);
    fill(x, r.y + 132, w, 1, LINE);
    int capacity = (r.h - ROW_TOP - 42) / ROW_H;
    for (int j = 0; j < capacity; j++) {
        int i = j + m->scroll;
        if (i >= (int)m->count)
            break;
        table_row_t *row = &m->rows[i];
        int y = r.y + ROW_TOP + j * ROW_H;
        if (i == m->selected)
            roundbox(x - 5, y - 4, w + 10, 30, 6, 0xe4eee5);
        icon(row->kind == TABLE_ROW_DIR ? FILES : PREVIEW, x + 2, y + 1,
             row->kind == TABLE_ROW_DIR ? 0xc49b57 : 0x96a8b4);
        fitted(x + 37, y + 4, w - 180, 14, row->kind == TABLE_ROW_DIR ? 600 : 400, row->label, INK);
        fitted(x + w - 132, y + 5, 132, 12, 400, row->value, MUTED);
    }
    if (m->valid && !m->count)
        text(x, r.y + ROW_TOP + 16, 14, 400, "This folder is empty.", MUTED);
    fill(x, r.y + r.h - 35, w, 1, LINE);
    fitted(x, r.y + r.h - 24, w - 180, 12, 400, m->header.caption, MUTED);
    const char *hint = "Enter opens / Backspace up";
    text(x + w - tw(12, 400, hint), r.y + r.h - 24, 12, 400, hint, MUTED);
}
static void task_content(void)
{
    rect_t r = windows[TASK].rect;
    model_t *m = &models[TASK];
    int x = r.x + 24, w = r.w - 48;
    text(x, r.y + 65, 24, 600, "System activity", INK);
    fitted(x, r.y + 101, w, 13, 400, m->header.caption, MUTED);
    text(x, r.y + 135, 12, 600, "PROCESS", MUTED);
    text(x + w - 205, r.y + 135, 12, 600, "CPU  /  MEMORY  /  USER", MUTED);
    fill(x, r.y + 157, w, 1, LINE);
    int capacity = (r.h - 202) / ROW_H;
    for (int j = 0; j < capacity; j++) {
        int i = j + m->scroll;
        if (i >= (int)m->count)
            break;
        int y = r.y + 170 + j * ROW_H;
        if (i == m->selected)
            roundbox(x - 5, y - 4, w + 10, 30, 6, 0xe4eee5);
        roundbox(x, y + 7, 6, 6, 3, ACCENT);
        fitted(x + 17, y + 3, w - 225, 13, 600, m->rows[i].label, INK);
        fitted(x + w - 205, y + 3, 205, 13, 400, m->rows[i].value, MUTED);
    }
    fill(x, r.y + r.h - 35, w, 1, LINE);
    text(x, r.y + r.h - 24, 12, 400, "Select an app; Delete twice to restart", MUTED);
}
static void text_content(int id)
{
    rect_t r = windows[id].rect;
    model_t *m = &models[id];
    int x = r.x + 24, w = r.w - 48;
    if (id == TERMINAL) {
        fill(r.x + 1, r.y + BAR + 1, r.w - 2, r.h - BAR - 14, 0x202c31);
        roundbox(r.x + 1, r.y + r.h - 24, r.w - 2, 23, 11, 0x202c31);
        fitted(x, r.y + 62, w, 13, 600, m->header.title, 0xa6c9b3);
        fill(x, r.y + 88, w, 1, 0x354247);
    } else {
        fitted(x, r.y + 65, w, 16, 600, m->header.title, INK);
        fitted(x, r.y + 92, w, 12, 400, m->header.caption, MUTED);
    }
    int top = id == TERMINAL ? 110 : 124;
    int capacity = (r.h - top - 40) / 24;
    for (int j = 0; j < capacity; j++) {
        int i = j + m->scroll;
        if (i >= (int)m->count)
            break;
        char value[64] = "";
        text_copy(value, sizeof(value), m->rows[i].label);
        text_append(value, sizeof(value), m->rows[i].value);
        if (id == PREVIEW) {
            char number[8] = "";
            text_u64(number, sizeof(number), (uint64_t)i + 1);
            text(x, r.y + top + j * 24, 12, 400, number, MUTED);
        }
        fitted(x + (id == PREVIEW ? 28 : 0), r.y + top + j * 24, w - (id == PREVIEW ? 28 : 0), 14,
               400, value, id == TERMINAL ? (i == (int)m->count - 1 ? 0xb9dfb9 : 0xd5dfdd) : INK);
    }
    if (!m->valid)
        text(x, r.y + top, 14, 400,
             id == PREVIEW ? "Open a document in Files." : "Connecting to terminal...", MUTED);
    text(x, r.y + r.h - 24, 12, 400,
         id == TERMINAL ? "help for commands   /   F1 switches windows"
                        : "Read-only preview   /   Up and Down scroll",
         MUTED);
}
static void settings_content(void)
{
    rect_t r = windows[SETTINGS].rect;
    int x = r.x + 28, w = r.w - 56;
    text(x, r.y + 72, 28, 600, "Your workspace", INK);
    text(x, r.y + 114, 14, 400, "Aurora " AURORA_VERSION_STRING, MUTED);
    const char *labels[] = {"Session", "Storage", "Permissions", "Audio"};
    const char *values[] = {account.uid == 0 ? "root / administrator" : account.name,
                            "Temporary memory session", "Apps use explicit grants",
                            "WAV playback + tones"};
    for (int i = 0; i < 4; i++) {
        int y = r.y + 170 + i * 58;
        fill(x, y - 15, w, 1, LINE);
        text(x, y, 14, 600, labels[i], INK);
        text(x + 132, y, 14, 400, values[i], MUTED);
    }
    roundbox(x, r.y + 416, 158, 36, 8, ACCENT);
    text(x + 20, r.y + 427, 13, 600, "Play test sound", 0xffffff);
    roundbox(x + 182, r.y + 416, 158, 36, 8, 0xe9efeb);
    text(x + 210, r.y + 427, 13, 600, "Sign out", ACCENT);
    text(x, r.y + 480, 13, 400, account.home, MUTED);
    text(x, r.y + 505, 13, 400, "Changes disappear when the system restarts.", MUTED);
    text(x, r.y + 530, 13, 400, "WAV: open Music in Files, or use play in Terminal.", MUTED);
}
static void draw_window(int id)
{
    chrome(id);
    rect_t r = windows[id].rect;
    int cx0 = canvas.clip_x0, cy0 = canvas.clip_y0, cx1 = canvas.clip_x1, cy1 = canvas.clip_y1;
    ui_surface_set_clip(&canvas, max(cx0, r.x + 1), max(cy0, r.y + BAR + 1),
                        max(0, min(cx1, r.x + r.w - 1) - max(cx0, r.x + 1)),
                        max(0, min(cy1, r.y + r.h - 1) - max(cy0, r.y + BAR + 1)));
    if (id == FILES)
        files_content();
    else if (id == TASK)
        task_content();
    else if (id == SETTINGS)
        settings_content();
    else
        text_content(id);
    ui_surface_set_clip(&canvas, cx0, cy0, cx1 - cx0, cy1 - cy0);
}
static const char *cursor_shape[CH] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
    "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ",
    "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     X..X   ", "      X..X  ", "      XXXX  "};
/* Save/restore only the offscreen canvas. Never erase a visible cursor while composing. */
static void cursor_restore(void)
{
    if (!cursor_drawn)
        return;
    for (unsigned y = 0; y < CH * display_scale; y++)
        for (unsigned x = 0; x < CW * display_scale; x++)
            canvas.pixels[(cursor_y * display_scale + y) * canvas.stride +
                          cursor_x * display_scale + x] = cursor_saved[y * CW * display_scale + x];
    cursor_drawn = false;
}
static unsigned resize_hint(void)
{
    if (session.uid == AURORA_NO_USER)
        return 0;
    if (resizing >= 0)
        return resize_edges;
    for (int layer = WINDOWS - 1; layer >= 0; layer--) {
        int id = zorder[layer];
        rect_t r = windows[id].rect;
        if (!windows[id].visible || !contains(r, cursor_x, cursor_y))
            continue;
        if (windows[id].maximized)
            return 0;
        return (cursor_x < r.x + 7 ? RESIZE_LEFT : 0) |
               (cursor_x >= r.x + r.w - 7 ? RESIZE_RIGHT : 0) |
               (cursor_y < r.y + 7 ? RESIZE_TOP : 0) |
               (cursor_y >= r.y + r.h - 7 ? RESIZE_BOTTOM : 0);
    }
    return 0;
}
static bool resize_pixel(unsigned hint, int x, int y)
{
    bool horizontal = (hint & (RESIZE_LEFT | RESIZE_RIGHT)) != 0;
    bool vertical = (hint & (RESIZE_TOP | RESIZE_BOTTOM)) != 0;
    if (horizontal && vertical) {
        bool reverse =
            (hint == (RESIZE_LEFT | RESIZE_BOTTOM) || hint == (RESIZE_RIGHT | RESIZE_TOP));
        if (reverse)
            x = CW - 1 - x;
        return (x >= 1 && x <= 10 && y == x + 3) || (x >= 1 && x <= 5 && y == 4) ||
               (x == 1 && y >= 4 && y <= 8) || (x >= 6 && x <= 10 && y == 13) ||
               (x == 10 && y >= 9 && y <= 13);
    }
    if (horizontal)
        return (y == 8 && x >= 1 && x <= 10) ||
               (x >= 1 && x <= 4 && (y == 8 - (x - 1) || y == 8 + (x - 1))) ||
               (x >= 7 && x <= 10 && (y == 8 - (10 - x) || y == 8 + (10 - x)));
    return (x == 5 && y >= 2 && y <= 15) ||
           (y >= 2 && y <= 5 && (x == 5 - (y - 2) || x == 5 + (y - 2))) ||
           (y >= 12 && y <= 15 && (x == 5 - (15 - y) || x == 5 + (15 - y)));
}
static void cursor_draw(void)
{
    unsigned hint = resize_hint();
    for (unsigned y = 0; y < CH * display_scale; y++)
        for (unsigned x = 0; x < CW * display_scale; x++)
            cursor_saved[y * CW * display_scale + x] =
                canvas.pixels[(cursor_y * display_scale + y) * canvas.stride +
                              cursor_x * display_scale + x];
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            if (hint) {
                bool ink = resize_pixel(hint, x, y), outline = false;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        outline |= resize_pixel(hint, x + dx, y + dy);
                if (outline)
                    ui_put_pixel(&canvas, cursor_x + x, cursor_y + y,
                                 ink ? ui_color(&canvas, 28, 42, 38)
                                     : ui_color(&canvas, 255, 255, 255));
            } else if (cursor_shape[y][x] != ' ')
                ui_put_pixel(&canvas, cursor_x + x, cursor_y + y,
                             cursor_shape[y][x] == 'X' ? ui_color(&canvas, 28, 42, 38)
                                                       : ui_color(&canvas, 255, 255, 255));
        }
    cursor_drawn = true;
}
bool desktop_render_pending(void)
{
    return dirty || cursor_dirty;
}
void desktop_render(void)
{
    if (!dirty && !cursor_dirty)
        return;
    rect_t d = damage;
    if (dirty) {
        for (int y = d.y * (int)display_scale; y < (d.y + d.h) * (int)display_scale; y++)
            memcpy(pixels + y * canvas.stride + d.x * display_scale,
                   wallpaper + y * canvas.stride + d.x * display_scale,
                   (size_t)d.w * display_scale * 4);
        ui_surface_set_clip(&canvas, d.x, d.y, d.w, d.h);
        if (session.uid != AURORA_NO_USER) {
            for (int i = 0; i < WINDOWS; i++) {
                int id = zorder[i];
                rect_t r = windows[id].rect;
                r.x -= 24;
                r.y -= 24;
                r.w += 48;
                r.h += 56;
                if (windows[id].visible && intersects(r, d))
                    draw_window(id);
            }
            draw_shell();
        } else
            login_draw(&canvas, &font);
        ui_surface_reset_clip(&canvas);
    }
    /* Cursor is part of every submitted region, including overlapping old/new positions. */
    cursor_draw();
    if (dirty)
        ui_present_region(&fb, &canvas, d.x, d.y, d.w, d.h);
    ui_present_region(&fb, &canvas, presented_x, presented_y, CW, CH);
    ui_present_region(&fb, &canvas, cursor_x, cursor_y, CW, CH);
    presented_x = cursor_x;
    presented_y = cursor_y;
    cursor_restore();
    dirty = false;
    cursor_dirty = false;
}
static void raise(int id)
{
    int at = 0;
    for (int i = 0; i < WINDOWS; i++)
        if (zorder[i] == id)
            at = i;
    for (int i = at; i < WINDOWS - 1; i++)
        zorder[i] = zorder[i + 1];
    zorder[WINDOWS - 1] = id;
    windows[id].visible = true;
    focus = id;
    all();
}
static void event(int view, uint64_t type, uint64_t value, const char *path)
{
    if (view < 0 || view >= VIEW_COUNT || !models[view].owner)
        return;
    ipc_message_t msg = {.type = type, .arg0 = (uint64_t)view, .arg1 = value};
    if (path)
        text_copy((char *)msg.data, sizeof(msg.data), path);
    (void)user_send((int64_t)models[view].owner, &msg);
}
static int rows_top(int id)
{
    return id == FILES ? ROW_TOP : id == TASK ? 170 : id == TERMINAL ? 110 : 124;
}
static int row_height(int id)
{
    return id == FILES || id == TASK ? ROW_H : 24;
}
static int visible_rows(int id)
{
    return max(1, (windows[id].rect.h - rows_top(id) - 42) / row_height(id));
}
static void select_row(int id, int row)
{
    if (id >= VIEW_COUNT || !models[id].count)
        return;
    model_t *m = &models[id];
    m->selected = clamp(row, 0, (int)m->count - 1);
    int count = visible_rows(id);
    if (m->selected < m->scroll)
        m->scroll = m->selected;
    if (m->selected >= m->scroll + count)
        m->scroll = m->selected - count + 1;
    event(id, MSG_VIEW_SELECT, (uint64_t)m->selected, NULL);
    mark_window(id);
}
static void press(void)
{
    if (session.uid == AURORA_NO_USER) {
        login_click(cursor_x, cursor_y, width, height);
        all();
        return;
    }
    rect_t d = dock_rect();
    if (contains(d, cursor_x, cursor_y)) {
        int i = (cursor_x - d.x - 8) / 144;
        if (i >= 0 && i < 4)
            raise(dock_order[i]);
        return;
    }
    int id = -1;
    for (int i = WINDOWS - 1; i >= 0; i--)
        if (windows[zorder[i]].visible && contains(windows[zorder[i]].rect, cursor_x, cursor_y)) {
            id = zorder[i];
            break;
        }
    if (id < 0)
        return;
    raise(id);
    rect_t r = windows[id].rect;
    int x = cursor_x - r.x, y = cursor_y - r.y;
    unsigned edges = (x < 7 ? RESIZE_LEFT : 0) | (x >= r.w - 7 ? RESIZE_RIGHT : 0) |
                     (y < 7 ? RESIZE_TOP : 0) | (y >= r.h - 7 ? RESIZE_BOTTOM : 0);
    if (edges && !windows[id].maximized) {
        resizing = id;
        resize_edges = edges;
        resize_start = r;
        resize_x = cursor_x;
        resize_y = cursor_y;
        return;
    }
    if (y < BAR) {
        if (x >= r.w - 108) {
            int control = (x - (r.w - 108)) / 32;
            if (control == 1) {
                window_t *w = &windows[id];
                if (w->maximized)
                    w->rect = w->restore;
                else {
                    w->restore = w->rect;
                    w->rect = (rect_t){12, TOP + 12, width - 24, height - TOP - 104};
                }
                w->maximized = !w->maximized;
            } else {
                windows[id].visible = false;
                focus = -1;
                for (int i = WINDOWS - 1; i >= 0; i--)
                    if (windows[zorder[i]].visible) {
                        focus = zorder[i];
                        break;
                    }
            }
            all();
            return;
        }
        if (!windows[id].maximized) {
            drag = id;
            drag_x = x;
            drag_y = y;
        }
        return;
    }
    if (id == SETTINGS) {
        if (contains((rect_t){28, 416, 158, 36}, x, y)) {
            ipc_message_t msg = {.type = MSG_AUDIO_PLAY, .arg0 = 1, .arg1 = 660, .arg2 = 20};
            (void)user_send(sys_service(IMAGE_AUDIO), &msg);
        } else if (contains((rect_t){210, 416, 158, 36}, x, y)) {
            ipc_message_t msg = {.type = MSG_AUTH_LOGOUT};
            (void)user_send(sys_service(IMAGE_AUTH), &msg);
        }
        return;
    }
    if (id == FILES) {
        if (x < SIDEBAR && y >= 93 && y < 93 + 7 * 38) {
            int p = (y - 93) / 38;
            event(FILES, MSG_VIEW_PATH, 0, paths[p]);
            return;
        }
        if (y >= 61 && y <= 91) {
            if (x >= SIDEBAR + 22 && x < SIDEBAR + 52)
                event(FILES, MSG_VIEW_BACK, 0, NULL);
            else if (x > r.w - 58)
                event(FILES, MSG_VIEW_REFRESH, 0, NULL);
            return;
        }
        if (x < SIDEBAR)
            return;
    }
    if (id == TERMINAL || id >= VIEW_COUNT)
        return;
    int row = (y - rows_top(id)) / row_height(id) + models[id].scroll;
    if (y >= rows_top(id) && y < r.h - 40 && row < (int)models[id].count) {
        select_row(id, row);
        if (click_view == id && click_row == row && ticks - last_click < 40)
            event(id, MSG_VIEW_ACTIVATE, (uint64_t)row, NULL);
        click_view = id;
        click_row = row;
        last_click = ticks;
    }
}
static void pointer(int x, int y, uint8_t new_buttons)
{
    int old_x = cursor_x, old_y = cursor_y;
    if (x == old_x && y == old_y && new_buttons == buttons)
        return;
    cursor_x = clamp(x, 0, width - CW);
    cursor_y = clamp(y, 0, height - CH);
    bool down = (new_buttons & 1) != 0, was = (buttons & 1) != 0;
    buttons = new_buttons;
    if (down && !was)
        press();
    else if (down && resizing >= 0) {
        mark_window(resizing);
        int min_w = resizing == FILES ? 640 : resizing == SETTINGS ? 520 : 450;
        int min_h = resizing == FILES ? 520 : resizing == SETTINGS ? 590 : 360;
        windows[resizing].rect =
            aurora_resize(resize_start, resize_edges, cursor_x - resize_x, cursor_y - resize_y,
                          (rect_t){8, TOP + 8, width - 16, height - TOP - 96}, min_w, min_h);
        if (resizing < VIEW_COUNT)
            models[resizing].scroll =
                clamp(models[resizing].scroll, 0,
                      max(0, (int)models[resizing].count - visible_rows(resizing)));
        mark_window(resizing);
    } else if (down && drag >= 0) {
        mark_window(drag);
        rect_t *r = &windows[drag].rect;
        r->x = clamp(cursor_x - drag_x, 8, width - r->w - 8);
        r->y = clamp(cursor_y - drag_y, TOP + 8, height - 88 - r->h);
        mark_window(drag);
    }
    if (!down) {
        if (resizing >= 0)
            user_log("display: window resized");
        drag = resizing = -1;
    }
    cursor_dirty = true;
}
void desktop_pointer(int32_t dx, int32_t dy, uint8_t b)
{
    pointer(cursor_x + dx, cursor_y - dy, b);
}
void desktop_pointer_absolute(uint16_t x, uint16_t y, uint8_t b)
{
    pointer((int)(((uint64_t)x * (width - 1) + 32767) / 65535),
            (int)(((uint64_t)y * (height - 1) + 32767) / 65535), b);
}
void desktop_key(uint8_t scan)
{
    if (scan == 0xe0) {
        extended = true;
        return;
    }
    bool ext = extended;
    extended = false;
    if (session.uid == AURORA_NO_USER) {
        login_key((uint16_t)(scan | (ext ? 0x100 : 0)));
        all();
        return;
    }
    uint8_t code = scan & 0x7f;
    if (scan == 0x3b) {
        for (int i = 1; i <= WINDOWS; i++) {
            int id = (focus + i + WINDOWS) % WINDOWS;
            if (windows[id].visible) {
                raise(id);
                break;
            }
        }
        user_log("display: F1 focus");
        return;
    }
    if (scan == 0x3c) {
        raise(TERMINAL);
        return;
    }
    if (scan == 0x3d) {
        raise(FILES);
        return;
    }
    if (scan == 0x3e) {
        raise(TASK);
        return;
    }
    if (focus < 0 || focus >= VIEW_COUNT)
        return;
    if (focus == TERMINAL) {
        event(focus, MSG_VIEW_KEY, scan | (ext ? 0x100u : 0), NULL);
        return;
    }
    if (scan & 0x80)
        return;
    if (ext && (code == 0x48 || code == 0x50))
        select_row(focus, models[focus].selected + (code == 0x48 ? -1 : 1));
    else if (code == 0x1c)
        event(focus, MSG_VIEW_ACTIVATE, (uint64_t)max(0, models[focus].selected), NULL);
    else if (code == 0x0e)
        event(focus, MSG_VIEW_BACK, 0, NULL);
    else if (code == 0x3f)
        event(focus, MSG_VIEW_REFRESH, 0, NULL);
    else
        event(focus, MSG_VIEW_KEY, scan | (ext ? 0x100u : 0), NULL);
}
void desktop_tick(uint64_t now)
{
    if (now / 6000 != ticks / 6000)
        mark((rect_t){width - 110, 0, 110, TOP});
    ticks = now;
}
void desktop_model(const ipc_message_t *msg)
{
    if (session.uid == AURORA_NO_USER || msg->arg0 >= VIEW_COUNT)
        return;
    int id = (int)msg->arg0;
    model_t *m = &models[id];
    if (msg->type == MSG_TABLE_BEGIN) {
        if (msg->arg1 > TABLE_MAX_ROWS)
            return;
        m->owner = msg->from;
        m->sequence = msg->arg3;
        m->expected = (uint32_t)msg->arg1;
        m->mask = 0;
        m->staging = true;
        memcpy(&m->pending_header, msg->data, sizeof(m->pending_header));
        m->pending_header.title[TABLE_TITLE_BYTES] = 0;
        m->pending_header.caption[TABLE_CAPTION_BYTES] = 0;
    } else if (m->staging && m->owner == msg->from && m->sequence == msg->arg3) {
        if (msg->type == MSG_TABLE_ROW && msg->arg1 < m->expected) {
            unsigned i = (unsigned)msg->arg1;
            memcpy(&m->pending[i], msg->data, sizeof(table_row_t));
            m->pending[i].label[TABLE_LABEL_BYTES] = 0;
            m->pending[i].value[TABLE_VALUE_BYTES] = 0;
            m->mask |= 1u << i;
        } else if (msg->type == MSG_TABLE_END) {
            m->staging = false;
            if (m->mask != (1u << m->expected) - 1 || msg->arg1 != m->expected)
                return;
            bool first = !m->valid, changed = strcmp(m->header.title, m->pending_header.title) != 0;
            bool same = m->valid && m->count == m->expected &&
                        !memcmp(&m->header, &m->pending_header, sizeof(m->header)) &&
                        !memcmp(m->rows, m->pending, m->expected * sizeof(table_row_t));
            if (same && id != PREVIEW)
                return;
            m->header = m->pending_header;
            m->count = m->expected;
            memcpy(m->rows, m->pending, sizeof(m->rows));
            m->valid = true;
            m->selected = clamp(m->selected, 0, max(0, (int)m->count - 1));
            if (changed) {
                m->scroll = 0;
                m->selected = 0;
            }
            if (id == TERMINAL)
                m->scroll = max(0, (int)m->count - visible_rows(id));
            if (first) {
                user_log(id == FILES     ? "display: files view online"
                         : id == TASK    ? "display: task view online"
                         : id == PREVIEW ? "display: preview view online"
                                         : "display: terminal view online");
            }
            /* Periodic updates never undo a user's minimize/close action. */
            if (id == PREVIEW)
                raise(PREVIEW);
            if (windows[id].visible)
                mark_window(id);
        }
    }
}
static void reset_windows(void)
{
    int left = 32, top = 82, fw = min(834, width - 64), fh = min(626, height - 180);
    windows[FILES] = (window_t){.rect = {left, top, fw, fh}, .visible = true};
    windows[TASK] =
        (window_t){.rect = {width - 544, top + 38, 512, min(488, height - 220)}, .visible = true};
    windows[TERMINAL] = (window_t){
        .rect = {max(40, width - 760), top + 84, min(700, width - 80), min(550, height - 220)}};
    windows[PREVIEW] = (window_t){
        .rect = {max(70, width - 735), top + 54, min(670, width - 100), min(580, height - 210)}};
    windows[SETTINGS] =
        (window_t){.rect = {(width - 580) / 2, top + 10, 580, min(590, height - 190)}};
    for (int i = 0; i < WINDOWS; i++)
        zorder[i] = i;
    /* Files first, monitor alongside on the 1440x900 reference desktop. */
    zorder[0] = PREVIEW;
    zorder[1] = TERMINAL;
    zorder[2] = SETTINGS;
    zorder[3] = TASK;
    zorder[4] = FILES;
}

void desktop_init(const framebuffer_info_t *info)
{
    display_scale = info->width >= 2560 && info->height >= 1400 ? 2 : 1;
    width = (int)(info->width / display_scale);
    height = (int)(info->height / display_scale);
    if (width < 1024 || height < 700 || info->width > MAX_W || info->height > MAX_H ||
        info->bpp != 32 || info->pitch < info->width * 4) {
        user_log("display: unsupported framebuffer dimensions");
        sys_exit(1);
    }
    fb = (ui_surface_t){.pixels = (uint32_t *)(uintptr_t)info->vaddr,
                        .width = (uint32_t)width,
                        .height = (uint32_t)height,
                        .scale = display_scale,
                        .stride = info->pitch / 4,
                        .red_shift = info->red_shift,
                        .green_shift = info->green_shift,
                        .blue_shift = info->blue_shift};
    canvas = fb;
    canvas.pixels = pixels;
    canvas.stride = info->width;
    ui_surface_reset_clip(&fb);
    ui_surface_reset_clip(&canvas);
    extern const uint8_t _binary_ui_font_bin_start[], _binary_ui_font_bin_end[];
    if (!ui_font_init(&font, _binary_ui_font_bin_start,
                      (size_t)(_binary_ui_font_bin_end - _binary_ui_font_bin_start)))
        sys_exit(1);
    font.scale = display_scale;
    for (int y = 0; y < (int)info->height; y++)
        for (int x = 0; x < (int)info->width; x++) {
            int dx = x / (int)display_scale - width / 3, dy = y / (int)display_scale - height / 3;
            int distance = dx * dx + dy * dy;
            int glow = max(0, 100 - distance / 9000);
            wallpaper[y * info->width + x] = ui_color(
                &canvas, (uint8_t)(220 + glow / 12 - y * 9 / (int)info->height),
                (uint8_t)(229 + glow / 16 - y * 7 / (int)info->height), (uint8_t)(225 + glow / 14));
        }
    char mode[96] = "display: mode ";
    text_u64(mode, sizeof(mode), info->width);
    text_append(mode, sizeof(mode), "x");
    text_u64(mode, sizeof(mode), info->height);
    text_append(mode, sizeof(mode), " scale=");
    text_u64(mode, sizeof(mode), display_scale);
    user_log(mode);
    reset_windows();
    cursor_x = width / 2;
    cursor_y = height - 105;
    all();
    if (!mode_transition)
        desktop_render();
}

void desktop_session(const credentials_t *next)
{
    session = *next;
    account = aurora_account(next->uid);
    user_erase(models, sizeof(models));
    reset_windows();
    drag = resizing = -1;
    extended = false;
    buttons = 0;
    click_view = click_row = -1;
    login_reset();
    for (unsigned i = 0; i < WINDOWS; i++)
        windows[i].visible = false;
    focus = FILES;
    if (session.uid != AURORA_NO_USER) {
        for (unsigned i = 0; i < 4; i++) {
            text_copy(home_paths[i], sizeof(home_paths[i]), account.home);
            if (i) {
                text_append(home_paths[i], sizeof(home_paths[i]), "/");
                text_append(home_paths[i], sizeof(home_paths[i]), places[i]);
            }
            paths[i] = home_paths[i];
        }
        windows[FILES].visible = windows[TASK].visible = true;
        user_log("display: authenticated workspace");
    } else
        user_log("display: login screen");
    all();
}
void desktop_auth_result(const ipc_message_t *msg)
{
    if ((int64_t)msg->arg1 < 0) {
        login_result(msg);
        all();
    }
}

void desktop_resize(uint32_t pixel_width, uint32_t pixel_height)
{
    if (pixel_width == canvas.width * display_scale &&
        pixel_height == canvas.height * display_scale)
        return;
    framebuffer_info_t info;
    if (sys_display_mode(pixel_width, pixel_height, &info) < 0) {
        user_log("display: mode request rejected");
        return;
    }
    window_t saved[WINDOWS];
    memcpy(saved, windows, sizeof(saved));
    int old_focus = focus, order[WINDOWS];
    memcpy(order, zorder, sizeof(order));
    drag = resizing = -1;
    buttons = 0;
    cursor_drawn = false;
    mode_transition = true;
    desktop_init(&info);
    mode_transition = false;
    for (unsigned i = 0; i < WINDOWS; i++) {
        windows[i].visible = saved[i].visible;
        /* Reflow using new logical geometry; keep visibility and model contents. */
    }
    focus = old_focus;
    memcpy(zorder, order, sizeof(order));
    for (unsigned i = 0; i < VIEW_COUNT; i++)
        models[i].scroll =
            clamp(models[i].scroll, 0, max(0, (int)models[i].count - visible_rows((int)i)));
    all();
    user_log("display: resolution adapted");
}
