/* Aurora v2 - framebuffer interface. */
#ifndef AURORA_ARCH_FB_H
#define AURORA_ARCH_FB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

typedef struct {
    volatile uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;
} framebuffer_t;

bool fb_init(const struct limine_framebuffer *framebuffer);
const framebuffer_t *fb_get(void);
uint32_t fb_rgb(uint8_t red, uint8_t green, uint8_t blue);
uint64_t fb_physical_address(void);
uint64_t fb_size_bytes(void);
uint64_t fb_mapping_bytes(void);
bool fb_set_mode(uint32_t width,uint32_t height);
void fb_clear(uint32_t color);
void fb_put_pixel(int x, int y, uint32_t color);
void fb_fill_rect(int x, int y, int width, int height, uint32_t color);
void fb_hline(int x, int y, int width, uint32_t color);

#endif /* AURORA_ARCH_FB_H */
