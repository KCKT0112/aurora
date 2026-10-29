/* Aurora v2 - boot splash renderer. */
#include <arch/fb.h>
#include <kernel/log.h>
#include <kernel/splash.h>
#include <stdint.h>
#include <stdbool.h>
#include <ui/draw.h>
#include <ui/font.h>
#include <ui/surface.h>

extern const uint8_t _binary_ui_font_bin_start[];
extern const uint8_t _binary_ui_font_bin_end[];

static ui_font_t g_font;
static bool g_font_ready;
static bool g_font_attempted;

static uint8_t clamp_channel(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (uint8_t)value;
}

static void blend_pixel(const framebuffer_t *fb, int x, int y,
                        uint8_t red, uint8_t green, uint8_t blue,
                        uint8_t alpha)
{
    if (x < 0 || y < 0 || (uint32_t)x >= fb->width ||
        (uint32_t)y >= fb->height || alpha == 0) {
        return;
    }

    uint32_t *pixel = (uint32_t *)&fb->pixels[(uint32_t)y *
                                                       fb->pitch_pixels +
                                                   (uint32_t)x];
    uint32_t destination = *pixel;
    uint8_t destination_red = (uint8_t)((destination >> fb->red_shift) & 0xFF);
    uint8_t destination_green =
        (uint8_t)((destination >> fb->green_shift) & 0xFF);
    uint8_t destination_blue =
        (uint8_t)((destination >> fb->blue_shift) & 0xFF);
    unsigned inverse = 255u - alpha;

    uint8_t mixed_red = (uint8_t)((red * alpha +
                                   destination_red * inverse) / 255u);
    uint8_t mixed_green = (uint8_t)((green * alpha +
                                     destination_green * inverse) / 255u);
    uint8_t mixed_blue = (uint8_t)((blue * alpha +
                                    destination_blue * inverse) / 255u);
    *pixel = fb_rgb(mixed_red, mixed_green, mixed_blue);
}

static uint32_t lcg(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void draw_background(const framebuffer_t *fb)
{
    int width = (int)fb->width;
    int height = (int)fb->height;

    for (int y = 0; y < height; y++) {
        int vertical = (int)((uint32_t)y * 255u /
                             (fb->height > 1 ? fb->height - 1 : 1));
        int base_red;
        int base_green;
        int base_blue;

        if (vertical < 128) {
            int t = vertical * 2;
            base_red = 5 + ((10 - 5) * t) / 255;
            base_green = 9 + ((20 - 9) * t) / 255;
            base_blue = 18 + ((38 - 18) * t) / 255;
        } else {
            int t = (vertical - 128) * 2;
            base_red = 10 + ((4 - 10) * t) / 255;
            base_green = 20 + ((7 - 20) * t) / 255;
            base_blue = 38 + ((16 - 38) * t) / 255;
        }

        for (int x = 0; x < width; x++) {
            int red = base_red;
            int green = base_green;
            int blue = base_blue;

            /* Cool aurora bloom in the upper-right third. */
            int dx = (x * 1000 / width) - 720;
            int dy = (y * 1000 / height) - 300;
            int distance = dx * dx + dy * dy;
            if (distance < 520 * 520) {
                int falloff = (520 * 520 - distance) * 255 /
                              (520 * 520);
                int strength = (falloff * falloff) / 255;
                red += strength * 24 / 255;
                green += strength * 86 / 255;
                blue += strength * 76 / 255;
            }

            /* Violet bloom near the lower-left. */
            dx = (x * 1000 / width) - 230;
            dy = (y * 1000 / height) - 830;
            distance = dx * dx + dy * dy;
            if (distance < 470 * 470) {
                int falloff = (470 * 470 - distance) * 255 /
                              (470 * 470);
                int strength = (falloff * falloff) / 255;
                red += strength * 54 / 255;
                green += strength * 32 / 255;
                blue += strength * 82 / 255;
            }

            uint32_t noise = (uint32_t)x * 73856093u ^
                             (uint32_t)y * 19349663u;
            int dither = (int)((noise >> 24) % 3) - 1;
            fb->pixels[(uint32_t)y * fb->pitch_pixels + (uint32_t)x] =
                fb_rgb(clamp_channel(red + dither),
                       clamp_channel(green + dither),
                       clamp_channel(blue + dither));
        }
    }
}

static void draw_stars(const framebuffer_t *fb)
{
    uint32_t seed = 0x51ED270Bu;

    for (int star = 0; star < 150; star++) {
        int x = 20 + (int)(lcg(&seed) >> 8) % ((int)fb->width - 40);
        int y = 20 + (int)(lcg(&seed) >> 8) % ((int)fb->height * 2 / 3);
        int brightness = 70 + (int)(lcg(&seed) >> 8) % 150;
        uint8_t alpha = (uint8_t)(brightness / 2);
        blend_pixel(fb, x, y, 185, 205, 235, alpha);
        if ((star % 17) == 0) {
            blend_pixel(fb, x - 1, y, 130, 160, 200, alpha / 3);
            blend_pixel(fb, x + 1, y, 130, 160, 200, alpha / 3);
            blend_pixel(fb, x, y - 1, 130, 160, 200, alpha / 3);
            blend_pixel(fb, x, y + 1, 130, 160, 200, alpha / 3);
        }
    }
}

static void draw_emblem(const framebuffer_t *fb)
{
    int center_x = (int)fb->width / 2;
    int center_y = (int)fb->height * 33 / 100;
    int ring_radius = 92;
    int ring_radius_squared = ring_radius * ring_radius;

    /* Thin orbital ring. */
    for (int y = center_y - ring_radius - 2;
         y <= center_y + ring_radius + 2; y++) {
        for (int x = center_x - ring_radius - 2;
             x <= center_x + ring_radius + 2; x++) {
            int dx = x - center_x;
            int dy = y - center_y;
            int distance = dx * dx + dy * dy;
            int delta = distance - ring_radius_squared;
            if (delta > -ring_radius * 2 && delta < ring_radius * 2) {
                int closeness = 255 - (delta < 0 ? -delta : delta) * 255 /
                                          (ring_radius * 2);
                blend_pixel(fb, x, y, 95, 188, 165,
                            (uint8_t)(closeness / 3));
            }
        }
    }

    /* Soft centre glow. */
    for (int y = center_y - 80; y <= center_y + 80; y += 2) {
        for (int x = center_x - 80; x <= center_x + 80; x += 2) {
            int dx = x - center_x;
            int dy = y - center_y;
            int distance = dx * dx + dy * dy;
            if (distance < 80 * 80) {
                int strength = (80 * 80 - distance) * 70 / (80 * 80);
                blend_pixel(fb, x, y, 65, 190, 155,
                            (uint8_t)strength);
            }
        }
    }

    /* Four-point compass star built from a diamond plus tapered rays. */
    for (int dy = -58; dy <= 58; dy++) {
        int half_width = (58 - (dy < 0 ? -dy : dy)) * 42 / 58;
        int distance = dy < 0 ? -dy : dy;
        for (int dx = -half_width; dx <= half_width; dx++) {
            uint8_t alpha = distance > 44 ? 120 : 235;
            uint8_t red = distance < 14 ? 235 : 130;
            uint8_t green = distance < 14 ? 250 : 235;
            uint8_t blue = distance < 14 ? 245 : 190;
            blend_pixel(fb, center_x + dx, center_y + dy, red, green,
                        blue, alpha);
        }
    }

    for (int offset = 0; offset < 72; offset++) {
        int thickness = (72 - offset) / 24;
        uint8_t alpha = (uint8_t)(185 - offset * 2);
        for (int t = -thickness; t <= thickness; t++) {
            blend_pixel(fb, center_x + t, center_y - 62 - offset,
                        130, 240, 200, alpha);
            blend_pixel(fb, center_x + t, center_y + 62 + offset,
                        130, 240, 200, alpha);
            blend_pixel(fb, center_x - 46 - offset, center_y + t,
                        150, 220, 210, alpha);
            blend_pixel(fb, center_x + 46 + offset, center_y + t,
                        150, 220, 210, alpha);
        }
    }

    /* Bright nucleus. */
    for (int dy = -7; dy <= 7; dy++) {
        int half = 7 - (dy < 0 ? -dy : dy);
        for (int dx = -half; dx <= half; dx++) {
            blend_pixel(fb, center_x + dx, center_y + dy,
                        255, 255, 255, 235);
        }
    }

}

static void draw_typography(const framebuffer_t *fb)
{
    if (!g_font_attempted) {
        g_font_attempted = true;
        size_t font_size = (size_t)(_binary_ui_font_bin_end -
                                    _binary_ui_font_bin_start);
        g_font_ready = ui_font_init(&g_font,
                                    _binary_ui_font_bin_start, font_size);
        if (!g_font_ready) {
            kprintf("[splash] warning: UI font asset failed validation\n");
        }
    }
    if (!g_font_ready) {
        return;
    }

    ui_surface_t surface = {
        .pixels = (uint32_t *)fb->pixels,
        .width = fb->width,
        .height = fb->height,
        .stride = fb->pitch_pixels,
        .red_shift = fb->red_shift,
        .green_shift = fb->green_shift,
        .blue_shift = fb->blue_shift,
    };
    ui_surface_reset_clip(&surface);

    int word_y = (int)fb->height * 51 / 100;
    uint32_t word_width =
        ui_font_text_width(&g_font, 64, 600, "AURORA");
    int word_x = ((int)fb->width - (int)word_width) / 2;

    /* Hairline above the wordmark. */
    int line_width = 56;
    blend_pixel(fb, (int)fb->width / 2 - line_width / 2, word_y - 20,
                90, 225, 180, 150);
    for (int x = 0; x < line_width; x++) {
        blend_pixel(fb, (int)fb->width / 2 - line_width / 2 + x,
                    word_y - 20, 90, 225, 180, 170);
    }

    ui_font_draw(&g_font, &surface, 64, 600, word_x, word_y,
                 "AURORA", 238, 244, 255, 250);

    const char *subtitle = "MICROKERNEL / RING-3 SESSION";
    uint32_t subtitle_width =
        ui_font_text_width(&g_font, 18, 400, subtitle);
    ui_font_draw(&g_font, &surface, 18, 400,
                 ((int)fb->width - (int)subtitle_width) / 2,
                 word_y + 86, subtitle, 118, 226, 186, 240);

    const char *spec = "x86_64  ·  1440x900  ·  32-BIT RGB";
    uint32_t spec_width = ui_font_text_width(&g_font, 14, 400, spec);
    ui_font_draw(&g_font, &surface, 14, 400,
                 ((int)fb->width - (int)spec_width) / 2,
                 word_y + 114, spec, 148, 166, 192, 220);

    const char *milestone = "STARTING DESKTOP  /  LIMINE 12.9.6";
    uint32_t milestone_width =
        ui_font_text_width(&g_font, 14, 400, milestone);
    ui_font_draw(&g_font, &surface, 14, 400,
                 ((int)fb->width - (int)milestone_width) / 2,
                 (int)fb->height - 58, milestone, 96, 116, 140, 150);
}

void splash_render(void)
{
    const framebuffer_t *fb = fb_get();

    draw_background(fb);
    draw_stars(fb);
    draw_emblem(fb);
    draw_typography(fb);
}
