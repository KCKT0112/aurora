/* Aurora v2 - Limine framebuffer setup.
 *
 * The current v2 bring-up supports the common 32-bit RGB layout provided by
 * Limine on QEMU/UEFI. Channel masks are still honoured instead of assuming
 * BGRA ordering.
 */
#include <arch/boot.h>
#include <arch/fb.h>
#include <arch/io.h>
#include <arch/pci.h>
#include <aurora/display_protocol.h>
#include <kernel/log.h>

static framebuffer_t g_framebuffer;
static uint64_t g_physical;
static uint64_t g_size;
static uint64_t g_aperture;
static bool g_modes;
static uint16_t vbe_read(uint16_t index)
{
    outw(0x1ce, index);
    return inw(0x1cf);
}
static void vbe_write(uint16_t index, uint16_t value)
{
    outw(0x1ce, index);
    outw(0x1cf, value);
}

bool fb_init(const struct limine_framebuffer *framebuffer)
{
    if (framebuffer == NULL || framebuffer->bpp != 32 ||
        framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB) {
        return false;
    }
    if (framebuffer->red_mask_size != 8 || framebuffer->green_mask_size != 8 ||
        framebuffer->blue_mask_size != 8) {
        return false;
    }

    g_framebuffer.pixels = (volatile uint32_t *)framebuffer->address;
    g_framebuffer.width = (uint32_t)framebuffer->width;
    g_framebuffer.height = (uint32_t)framebuffer->height;
    g_framebuffer.pitch_pixels = (uint32_t)(framebuffer->pitch / 4);
    g_framebuffer.red_shift = framebuffer->red_mask_shift;
    g_framebuffer.green_shift = framebuffer->green_mask_shift;
    g_framebuffer.blue_shift = framebuffer->blue_mask_shift;

    uint64_t address = (uint64_t)(uintptr_t)framebuffer->address;
    uint64_t hhdm = boot_hhdm_offset();
    g_physical = address >= hhdm ? address - hhdm : address;
    g_size = framebuffer->pitch * framebuffer->height;
    g_aperture = (g_size + 4095) & ~4095ull;
    pci_address_t device;
    if (pci_find_device(0x1234, 0x1111, &device) && pci_bar_address(&device, 0) == g_physical &&
        vbe_read(0) >= 0xb0c0 && vbe_read(0) <= 0xb0c5) {
        uint64_t aperture = (uint64_t)vbe_read(10) * 65536;
        if (aperture >= g_size && aperture <= 64 * 1024 * 1024) {
            g_aperture = aperture;
            g_modes = true;
        }
    }
    return true;
}

uint64_t fb_physical_address(void)
{
    return g_physical;
}

uint64_t fb_size_bytes(void)
{
    return g_size;
}

const framebuffer_t *fb_get(void)
{
    return &g_framebuffer;
}

uint32_t fb_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)red << g_framebuffer.red_shift) |
           ((uint32_t)green << g_framebuffer.green_shift) |
           ((uint32_t)blue << g_framebuffer.blue_shift);
}

void fb_put_pixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || (uint32_t)x >= g_framebuffer.width ||
        (uint32_t)y >= g_framebuffer.height) {
        return;
    }
    g_framebuffer.pixels[(uint32_t)y * g_framebuffer.pitch_pixels + (uint32_t)x] = color;
}

void fb_hline(int x, int y, int width, uint32_t color)
{
    if (y < 0 || (uint32_t)y >= g_framebuffer.height) {
        return;
    }
    int start = x < 0 ? 0 : x;
    int end = x + width;
    if (end > (int)g_framebuffer.width) {
        end = (int)g_framebuffer.width;
    }
    if (start >= end) {
        return;
    }
    volatile uint32_t *row = g_framebuffer.pixels + (uint32_t)y * g_framebuffer.pitch_pixels;
    for (int column = start; column < end; column++) {
        row[column] = color;
    }
}

void fb_fill_rect(int x, int y, int width, int height, uint32_t color)
{
    for (int row = 0; row < height; row++) {
        fb_hline(x, y + row, width, color);
    }
}

void fb_clear(uint32_t color)
{
    fb_fill_rect(0, 0, (int)g_framebuffer.width, (int)g_framebuffer.height, color);
}

uint64_t fb_mapping_bytes(void)
{
    return g_aperture;
}
bool fb_set_mode(uint32_t width, uint32_t height)
{
    if (!g_modes || !display_mode_valid(width, height) || (uint64_t)width * height * 4 > g_aperture)
        return false;
    if (width == g_framebuffer.width && height == g_framebuffer.height)
        return true;
    uint16_t old_w = (uint16_t)g_framebuffer.width, old_h = (uint16_t)g_framebuffer.height;
    vbe_write(4, 0);
    vbe_write(1, (uint16_t)width);
    vbe_write(2, (uint16_t)height);
    vbe_write(3, 32);
    vbe_write(6, (uint16_t)width);
    vbe_write(8, 0);
    vbe_write(9, 0);
    vbe_write(4, 0xc1);
    if (vbe_read(1) != width || vbe_read(2) != height || vbe_read(3) != 32) {
        vbe_write(4, 0);
        vbe_write(1, old_w);
        vbe_write(2, old_h);
        vbe_write(3, 32);
        vbe_write(6, old_w);
        vbe_write(4, 0xc1);
        return false;
    }
    g_framebuffer.width = width;
    g_framebuffer.height = height;
    g_framebuffer.pitch_pixels = width;
    g_framebuffer.red_shift = 16;
    g_framebuffer.green_shift = 8;
    g_framebuffer.blue_shift = 0;
    g_size = (uint64_t)width * height * 4;
    kprintf("[display] mode changed %ux%u\n", width, height);
    return true;
}
