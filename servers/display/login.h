#ifndef AURORA_DISPLAY_LOGIN_H
#define AURORA_DISPLAY_LOGIN_H
#include <ui/font.h>
#include <aurora/abi.h>
void login_reset(void);
void login_draw(ui_surface_t *surface,const ui_font_t *font);
void login_key(uint16_t scan);
void login_click(int x,int y,int width,int height);
void login_result(const ipc_message_t *message);
#endif
