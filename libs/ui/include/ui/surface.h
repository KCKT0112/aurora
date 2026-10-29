/* Aurora v2 - 32-bit RGB software surface. */
#ifndef AURORA_UI_SURFACE_H
#define AURORA_UI_SURFACE_H

#include <stdint.h>

typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t scale;  /* 0/1 = native, 2 = HiDPI; width/clip are logical */
    uint32_t stride; /* pixels per row */
    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;
    /* Exclusive upper bounds. All drawing primitives honour this clip. */
    int clip_x0;
    int clip_y0;
    int clip_x1;
    int clip_y1;
} ui_surface_t;

static inline unsigned ui_scale(const ui_surface_t *s)
{
    return s->scale == 2 ? 2 : 1;
}
static inline ui_surface_t ui_physical(const ui_surface_t *s)
{
    ui_surface_t p = *s;
    unsigned scale = ui_scale(s);
    p.width *= scale;
    p.height *= scale;
    p.clip_x0 *= (int)scale;
    p.clip_x1 *= (int)scale;
    p.clip_y0 *= (int)scale;
    p.clip_y1 *= (int)scale;
    p.scale = 1;
    return p;
}
#endif /* AURORA_UI_SURFACE_H */
