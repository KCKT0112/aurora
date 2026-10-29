#include <aurora/geometry.h>
#include <assert.h>
#include <stdio.h>
int main(void)
{
    aurora_rect_t bounds = {8, 56, 1424, 756}, start = {32, 82, 834, 626};
    aurora_rect_t r =
        aurora_resize(start, RESIZE_RIGHT | RESIZE_BOTTOM, -500, -500, bounds, 640, 520);
    assert(r.x == 32 && r.y == 82 && r.w == 640 && r.h == 520);
    r = aurora_resize(start, RESIZE_LEFT | RESIZE_TOP, 2000, 2000, bounds, 640, 520);
    assert(r.x == 226 && r.y == 188 && r.w == 640 && r.h == 520);
    r = aurora_resize(start, RESIZE_RIGHT | RESIZE_BOTTOM, 2000, 2000, bounds, 640, 520);
    assert(r.x + r.w == 1432 && r.y + r.h == 812);
    r = aurora_resize(start, RESIZE_LEFT | RESIZE_TOP, -2000, -2000, bounds, 640, 520);
    assert(r.x == 8 && r.y == 56);
    for (unsigned edges = 1; edges < 16; edges++)
        for (int dx = -1500; dx < 1500; dx += 73) {
            r = aurora_resize(start, edges, dx, -dx, bounds, 640, 520);
            assert(r.x >= bounds.x && r.y >= bounds.y && r.x + r.w <= bounds.x + bounds.w &&
                   r.y + r.h <= bounds.y + bounds.h);
            assert(r.w >= 640 && r.h >= 520);
        }
    puts("geometry: resize bounds, minimum sizes and anchored edges passed");
}
