/* Aurora v2 - software drawing primitives for 32-bit RGB surfaces. */
#ifndef AURORA_UI_DRAW_H
#define AURORA_UI_DRAW_H

#include <stdint.h>
#include <ui/surface.h>

uint32_t ui_color(const ui_surface_t *surface, uint8_t red, uint8_t green,
                  uint8_t blue);
void ui_surface_reset_clip(ui_surface_t *surface);
void ui_surface_set_clip(ui_surface_t *surface, int x, int y, int width,
                         int height);
void ui_put_pixel(const ui_surface_t *surface, int x, int y,
                  uint32_t color);
void ui_blend_pixel(const ui_surface_t *surface, int x, int y,
                    uint8_t red, uint8_t green, uint8_t blue,
                    uint8_t alpha);
void ui_fill_rect(const ui_surface_t *surface, int x, int y, int width,
                  int height, uint8_t red, uint8_t green, uint8_t blue,
                  uint8_t alpha);
void ui_hline(const ui_surface_t *surface, int x, int y, int width,
              uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
void ui_vline(const ui_surface_t *surface, int x, int y, int height,
              uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
void ui_fill_round_rect(const ui_surface_t *surface, int x, int y,
                        int width, int height, int radius, uint8_t red,
                        uint8_t green, uint8_t blue, uint8_t alpha);
void ui_fill_circle(const ui_surface_t *surface, int center_x,
                    int center_y, int radius, uint8_t red, uint8_t green,
                    uint8_t blue, uint8_t alpha);
void ui_present(const ui_surface_t *surface, const uint32_t *source);
void ui_present_region(const ui_surface_t *destination,
                       const ui_surface_t *source, int x, int y,
                       int width, int height);

#endif /* AURORA_UI_DRAW_H */
