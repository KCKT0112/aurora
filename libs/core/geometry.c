#include <aurora/geometry.h>
static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
aurora_rect_t aurora_resize(aurora_rect_t s, unsigned edges, int dx, int dy, aurora_rect_t b,
                            int mw, int mh)
{
    if (mw > b.w)
        mw = b.w;
    if (mh > b.h)
        mh = b.h;
    int right = s.x + s.w, bottom = s.y + s.h;
    if (edges & RESIZE_LEFT)
        s.x = clamp(s.x + dx, b.x, right - mw);
    if (edges & RESIZE_RIGHT)
        right = clamp(right + dx, s.x + mw, b.x + b.w);
    if (edges & RESIZE_TOP)
        s.y = clamp(s.y + dy, b.y, bottom - mh);
    if (edges & RESIZE_BOTTOM)
        bottom = clamp(bottom + dy, s.y + mh, b.y + b.h);
    s.w = right - s.x;
    s.h = bottom - s.y;
    return s;
}
