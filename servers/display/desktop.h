#ifndef AURORA_DISPLAY_DESKTOP_H
#define AURORA_DISPLAY_DESKTOP_H
#include <aurora/abi.h>
#include <stdbool.h>
void desktop_session(const credentials_t *session);
void desktop_auth_result(const ipc_message_t *message);
void desktop_resize(uint32_t width,uint32_t height);
void desktop_init(const framebuffer_info_t *info);
void desktop_pointer(int32_t dx, int32_t dy, uint8_t buttons);
void desktop_pointer_absolute(uint16_t x, uint16_t y, uint8_t buttons);
void desktop_key(uint8_t scancode);
void desktop_tick(uint64_t ticks);
void desktop_model(const ipc_message_t *message);
bool desktop_render_pending(void);
void desktop_render(void);
#endif
