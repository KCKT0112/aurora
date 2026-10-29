#ifndef AURORA_GEOMETRY_H
#define AURORA_GEOMETRY_H
typedef struct {
    int x, y, w, h;
} aurora_rect_t;
enum { RESIZE_LEFT = 1, RESIZE_RIGHT = 2, RESIZE_TOP = 4, RESIZE_BOTTOM = 8 };
aurora_rect_t aurora_resize(aurora_rect_t start, unsigned edges, int dx, int dy,
                            aurora_rect_t bounds, int min_w, int min_h);
#endif
