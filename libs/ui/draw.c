/* Aurora v2 - clipped software rasterizer for compositor and widgets. */
#include <ui/draw.h>
#include <stddef.h>

uint32_t ui_color(const ui_surface_t *surface, uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)red << surface->red_shift) | ((uint32_t)green << surface->green_shift) |
           ((uint32_t)blue << surface->blue_shift);
}

void ui_surface_reset_clip(ui_surface_t *surface)
{
    if (surface == NULL) {
        return;
    }
    surface->clip_x0 = 0;
    surface->clip_y0 = 0;
    surface->clip_x1 = (int)surface->width;
    surface->clip_y1 = (int)surface->height;
}

void ui_surface_set_clip(ui_surface_t *surface, int x, int y, int width, int height)
{
    if (surface == NULL) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > (int)surface->width) {
        x1 = (int)surface->width;
    }
    if (y1 > (int)surface->height) {
        y1 = (int)surface->height;
    }
    if (x0 >= x1 || y0 >= y1) {
        surface->clip_x0 = 0;
        surface->clip_y0 = 0;
        surface->clip_x1 = 0;
        surface->clip_y1 = 0;
        return;
    }
    surface->clip_x0 = x0;
    surface->clip_y0 = y0;
    surface->clip_x1 = x1;
    surface->clip_y1 = y1;
}

void ui_put_pixel(const ui_surface_t *surface, int x, int y, uint32_t color)
{
    if (ui_scale(surface) == 2) {
        ui_surface_t p = ui_physical(surface);
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
                ui_put_pixel(&p, x * 2 + i, y * 2 + j, color);
        return;
    }
    if (x < surface->clip_x0 || y < surface->clip_y0 || x >= surface->clip_x1 ||
        y >= surface->clip_y1) {
        return;
    }
    surface->pixels[(uint32_t)y * surface->stride + (uint32_t)x] = color;
}

void ui_blend_pixel(const ui_surface_t *surface, int x, int y, uint8_t red, uint8_t green,
                    uint8_t blue, uint8_t alpha)
{
    if (ui_scale(surface) == 2) {
        ui_surface_t p = ui_physical(surface);
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
                ui_blend_pixel(&p, x * 2 + i, y * 2 + j, red, green, blue, alpha);
        return;
    }
    if (x < surface->clip_x0 || y < surface->clip_y0 || x >= surface->clip_x1 ||
        y >= surface->clip_y1 || alpha == 0) {
        return;
    }
    uint32_t *pixel = &surface->pixels[(uint32_t)y * surface->stride + (uint32_t)x];
    uint32_t destination = *pixel;
    uint8_t destination_red = (uint8_t)((destination >> surface->red_shift) & 0xFF);
    uint8_t destination_green = (uint8_t)((destination >> surface->green_shift) & 0xFF);
    uint8_t destination_blue = (uint8_t)((destination >> surface->blue_shift) & 0xFF);
    uint32_t inverse = 255u - alpha;
    uint8_t mixed_red = (uint8_t)((red * alpha + destination_red * inverse) / 255u);
    uint8_t mixed_green = (uint8_t)((green * alpha + destination_green * inverse) / 255u);
    uint8_t mixed_blue = (uint8_t)((blue * alpha + destination_blue * inverse) / 255u);
    *pixel = ui_color(surface, mixed_red, mixed_green, mixed_blue);
}

void ui_fill_rect(const ui_surface_t *surface, int x, int y, int width, int height, uint8_t red,
                  uint8_t green, uint8_t blue, uint8_t alpha)
{
    if (ui_scale(surface) == 2) {
        ui_surface_t p = ui_physical(surface);
        ui_fill_rect(&p, x * 2, y * 2, width * 2, height * 2, red, green, blue, alpha);
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x0 < surface->clip_x0) {
        x0 = surface->clip_x0;
    }
    if (y0 < surface->clip_y0) {
        y0 = surface->clip_y0;
    }
    if (x1 > surface->clip_x1) {
        x1 = surface->clip_x1;
    }
    if (y1 > surface->clip_y1) {
        y1 = surface->clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    if (alpha == 255) {
        uint32_t color = ui_color(surface, red, green, blue);
        for (int row = y0; row < y1; row++) {
            uint32_t *pixels = surface->pixels + (uint32_t)row * surface->stride;
            for (int column = x0; column < x1; column++) {
                pixels[column] = color;
            }
        }
        return;
    }

    for (int row = y0; row < y1; row++) {
        for (int column = x0; column < x1; column++) {
            ui_blend_pixel(surface, column, row, red, green, blue, alpha);
        }
    }
}

void ui_hline(const ui_surface_t *surface, int x, int y, int width, uint8_t red, uint8_t green,
              uint8_t blue, uint8_t alpha)
{
    ui_fill_rect(surface, x, y, width, 1, red, green, blue, alpha);
}

void ui_vline(const ui_surface_t *surface, int x, int y, int height, uint8_t red, uint8_t green,
              uint8_t blue, uint8_t alpha)
{
    ui_fill_rect(surface, x, y, 1, height, red, green, blue, alpha);
}

static int integer_sqrt(int value)
{
    int root = value;
    while (root > 0 && root * root > value) {
        root--;
    }
    return root;
}

/* Analytic inside/outside bounds; supersample only boundary pixels. */
void ui_fill_round_rect(const ui_surface_t *surface, int x, int y, int width, int height,
                        int radius, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    if (ui_scale(surface) == 2) {
        ui_surface_t p = ui_physical(surface);
        ui_fill_round_rect(&p, x * 2, y * 2, width * 2, height * 2, radius * 2, red, green, blue,
                           alpha);
        return;
    }
    if (width <= 0 || height <= 0)
        return;
    if (radius < 1) {
        ui_fill_rect(surface, x, y, width, height, red, green, blue, alpha);
        return;
    }
    if (radius > width / 2)
        radius = width / 2;
    if (radius > height / 2)
        radius = height / 2;
    int top = y > surface->clip_y0 ? y : surface->clip_y0,
        bottom = y + height < surface->clip_y1 ? y + height : surface->clip_y1;
    for (int py = top; py < bottom; py++) {
        int dy = py - y;
        if (dy >= radius && dy < height - radius) {
            ui_fill_rect(surface, x, py, width, 1, red, green, blue, alpha);
            continue;
        }
        ui_fill_rect(surface, x + radius, py, width - 2 * radius, 1, red, green, blue, alpha);
        int cy = dy < radius ? radius : height - radius;
        for (int px = 0; px < radius; px++) {
            int cx = radius, dx = px - cx, yy = dy - cy;
            unsigned coverage = 0;
            int nearx = dx + 1, neary = yy < 0 ? yy + 1 : yy;
            int farx = dx, fary = yy < 0 ? yy : yy + 1;
            if (nearx * nearx + neary * neary >= radius * radius)
                continue;
            if (farx * farx + fary * fary <= radius * radius)
                coverage = 16;
            else
                for (int sy = 0; sy < 4; sy++)
                    for (int sx = 0; sx < 4; sx++) {
                        int a = dx * 8 + sx * 2 + 1, b = yy * 8 + sy * 2 + 1;
                        if (a * a + b * b <= radius * radius * 64)
                            coverage++;
                    }
            uint8_t opacity = (uint8_t)((coverage * alpha + 8) / 16);
            ui_blend_pixel(surface, x + px, py, red, green, blue, opacity);
            ui_blend_pixel(surface, x + width - 1 - px, py, red, green, blue, opacity);
        }
    }
}

void ui_fill_circle(const ui_surface_t *surface, int center_x, int center_y, int radius,
                    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    if (ui_scale(surface) == 2) {
        ui_surface_t p = ui_physical(surface);
        ui_fill_circle(&p, center_x * 2, center_y * 2, radius * 2, red, green, blue, alpha);
        return;
    }
    int radius_squared = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        int span = integer_sqrt(radius_squared - dy * dy);
        ui_fill_rect(surface, center_x - span, center_y + dy, span * 2 + 1, 1, red, green, blue,
                     alpha);
    }
}

void ui_present(const ui_surface_t *surface, const uint32_t *source)
{
    if (surface == NULL || source == NULL) {
        return;
    }
    const uint32_t words = surface->width / 2;
    for (uint32_t row = 0; row < surface->height; row++) {
        uint64_t *destination = (uint64_t *)(void *)(surface->pixels + row * surface->stride);
        const uint64_t *pixels = (const uint64_t *)(const void *)(source + row * surface->width);
        for (uint32_t index = 0; index < words; index++) {
            destination[index] = pixels[index];
        }
        if ((surface->width & 1u) != 0) {
            surface->pixels[row * surface->stride + surface->width - 1] =
                source[row * surface->width + surface->width - 1];
        }
    }
}

void ui_present_region(const ui_surface_t *destination, const ui_surface_t *source, int x, int y,
                       int width, int height)
{
    if (destination == NULL || source == NULL || width <= 0 || height <= 0) {
        return;
    }
    if (ui_scale(source) == 2 && ui_scale(destination) == 2) {
        ui_surface_t from = ui_physical(source), to = ui_physical(destination);
        ui_present_region(&to, &from, x * 2, y * 2, width * 2, height * 2);
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > (int)destination->width) {
        x1 = (int)destination->width;
    }
    if (y1 > (int)destination->height) {
        y1 = (int)destination->height;
    }
    if (x1 > (int)source->width) {
        x1 = (int)source->width;
    }
    if (y1 > (int)source->height) {
        y1 = (int)source->height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    for (int row = y0; row < y1; row++) {
        uint32_t *out = destination->pixels + (uint32_t)row * destination->stride;
        const uint32_t *in = source->pixels + (uint32_t)row * source->stride;
        for (int column = x0; column < x1; column++) {
            out[column] = in[column];
        }
    }
}
