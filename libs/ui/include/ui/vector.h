#ifndef AURORA_UI_VECTOR_H
#define AURORA_UI_VECTOR_H
#include <ui/surface.h>
#include <stdbool.h>
enum ui_icon {
    UI_ICON_FOLDER,
    UI_ICON_TERMINAL,
    UI_ICON_MONITOR,
    UI_ICON_SETTINGS,
    UI_ICON_DOCUMENT,
    UI_ICON_REFRESH,
    UI_ICON_BACK,
    UI_ICON_COUNT
};
void ui_icon_draw(ui_surface_t *surface, unsigned icon, int x, int y, unsigned size, uint32_t rgb);
void ui_shadow(ui_surface_t *surface, int x, int y, int width, int height, int radius,
               bool focused);
#endif
