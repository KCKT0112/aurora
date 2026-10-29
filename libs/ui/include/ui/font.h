/* Aurora v2 - anti-aliased bitmap font renderer. */
#ifndef AURORA_UI_FONT_H
#define AURORA_UI_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ui/surface.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    unsigned scale;
} ui_font_t;

bool ui_font_init(ui_font_t *font, const void *data, size_t size);
uint32_t ui_font_text_width(const ui_font_t *font, uint32_t size,
                            uint16_t weight, const char *text);
uint32_t ui_font_line_height(const ui_font_t *font, uint32_t size,
                             uint16_t weight);
void ui_font_draw(const ui_font_t *font, const ui_surface_t *surface,
                  uint32_t size, uint16_t weight, int x, int y,
                  const char *text, uint8_t red, uint8_t green,
                  uint8_t blue, uint8_t alpha);

#endif /* AURORA_UI_FONT_H */
