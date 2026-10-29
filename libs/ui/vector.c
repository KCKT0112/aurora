/* Supersampled SVG stroke renderer. Cache covers hot-path icon sizes; no allocation. */
#include <ui/vector.h>
#include <ui/draw.h>
#include <stddef.h>
typedef struct {
    int16_t x0, y0, x1, y1;
} vector_segment_t;
typedef struct {
    const vector_segment_t *segments;
    size_t count;
} vector_icon_t;
#include <vector_icons.h>
#define ICON_MAX 48
static struct {
    unsigned size;
    uint8_t alpha[ICON_MAX * ICON_MAX];
} cache[UI_ICON_COUNT];
static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
static bool covers(const vector_segment_t *s, int x, int y, int radius)
{
    int64_t dx = s->x1 - s->x0, dy = s->y1 - s->y0, px = x - s->x0, py = y - s->y0;
    int64_t length = dx * dx + dy * dy, projection = px * dx + py * dy;
    if (projection < 0)
        projection = 0;
    if (projection > length)
        projection = length;
    if (length) {
        px -= dx * projection / length;
        py -= dy * projection / length;
    }
    return px * px + py * py <= (int64_t)radius * radius;
}
static void raster(unsigned id, unsigned size)
{
    const vector_icon_t *icon = &icons[id];
    for (unsigned y = 0; y < size; y++)
        for (unsigned x = 0; x < size; x++) {
            unsigned coverage = 0;
            for (unsigned sy = 0; sy < 4; sy++)
                for (unsigned sx = 0; sx < 4; sx++) {
                    int px = (int)((x * 8 + sx * 2 + 1) * 24 * 256 / (size * 8)),
                        py = (int)((y * 8 + sy * 2 + 1) * 24 * 256 / (size * 8));
                    for (size_t i = 0; i < icon->count; i++)
                        if (covers(&icon->segments[i], px, py, 211)) {
                            coverage++;
                            break;
                        }
                }
            cache[id].alpha[y * size + x] = (uint8_t)((coverage * 255 + 8) / 16);
        }
    cache[id].size = size;
}
void ui_icon_draw(ui_surface_t *s, unsigned id, int x, int y, unsigned size, uint32_t rgb)
{
    if (ui_scale(s) == 2) {
        ui_surface_t p = ui_physical(s);
        ui_icon_draw(&p, id, x * 2, y * 2, size * 2, rgb);
        return;
    }
    if (id >= UI_ICON_COUNT || !size || size > ICON_MAX)
        return;
    if (x + (int)size <= s->clip_x0 || y + (int)size <= s->clip_y0 || x >= s->clip_x1 ||
        y >= s->clip_y1)
        return;
    if (cache[id].size != size)
        raster(id, size);
    for (unsigned j = 0; j < size; j++)
        for (unsigned i = 0; i < size; i++) {
            uint8_t alpha = cache[id].alpha[j * size + i];
            if (alpha)
                ui_blend_pixel(s, x + (int)i, y + (int)j, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8),
                               (uint8_t)rgb, alpha);
        }
}
/* Rounded rectangular distance field with a smooth quadratic falloff.
 * Skip the opaque body; only the small perimeter requires blending. */
void ui_shadow(ui_surface_t *s, int x, int y, int w, int h, int r, bool focused)
{
    unsigned scale = ui_scale(s);
    ui_surface_t physical = ui_physical(s);
    s = &physical;
    x *= (int)scale;
    y *= (int)scale;
    w *= (int)scale;
    h *= (int)scale;
    r *= (int)scale;
    int spread = 22 * (int)scale, offset = 5 * (int)scale;
    int left = clamp(x - spread, s->clip_x0, s->clip_x1),
        right = clamp(x + w + spread, s->clip_x0, s->clip_x1);
    int top = clamp(y - spread + offset, s->clip_y0, s->clip_y1),
        bottom = clamp(y + h + spread + offset, s->clip_y0, s->clip_y1);
    for (int py = top; py < bottom; py++)
        for (int px = left; px < right; px++) {
            if (px >= x && px < x + w && py >= y + r && py < y + h - r) {
                px = x + w - 1; /* jump over opaque spans, not every interior pixel */
                continue;
            }
            if (px >= x + r && px < x + w - r && py >= y && py < y + h) {
                px = x + w - r - 1;
                continue;
            }
            int cx = clamp(px, x + r, x + w - r - 1),
                cy = clamp(py, y + offset + r, y + offset + h - r - 1);
            int dx = px - cx, dy = py - cy, d2 = dx * dx + dy * dy;
            int edge = r * r, limit = (r + spread) * (r + spread);
            if (d2 >= limit)
                continue;
            int falloff = d2 <= edge ? 255 : (limit - d2) * 255 / (limit - edge);
            uint8_t alpha = (uint8_t)(falloff * falloff * (focused ? 28 : 18) / (255 * 255));
            if (alpha)
                ui_blend_pixel(s, px, py, 12, 22, 27, alpha);
        }
}
