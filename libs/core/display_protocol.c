#include <aurora/display_protocol.h>
bool display_mode_valid(uint32_t w, uint32_t h)
{
    unsigned scale = w >= 2560 && h >= 1400 ? 2 : 1;
    return w >= 1024 * scale && h >= 700 * scale && w <= 3840 && h <= 2160 && !(w % 8) &&
           !(h % scale) && !(w >= 2560 && h < 1400);
}
bool display_packet_byte(display_parser_t *p, uint8_t byte, uint16_t *w, uint16_t *h)
{
    static const uint8_t magic[4] = {'A', 'R', 'Z', '1'};
    if (p->used < 4 && byte != magic[p->used]) {
        p->used = byte == magic[0] ? 1 : 0;
        return false;
    }
    p->bytes[p->used++] = byte;
    if (p->used < DISPLAY_PACKET_SIZE)
        return false;
    p->used = 0;
    uint16_t width = (uint16_t)(p->bytes[4] | p->bytes[5] << 8),
             height = (uint16_t)(p->bytes[6] | p->bytes[7] << 8);
    uint16_t seq = (uint16_t)(p->bytes[8] | p->bytes[9] << 8),
             check = (uint16_t)(p->bytes[10] | p->bytes[11] << 8);
    if (check != (uint16_t)(width ^ height ^ seq ^ 0xa75a) || !display_mode_valid(width, height))
        return false;
    *w = width;
    *h = height;
    return true;
}
