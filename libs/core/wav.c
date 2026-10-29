#include <aurora/wav.h>
#include <string.h>
static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
bool wav_parse(wav_read_fn read, void *ctx, uint64_t size, wav_info_t *info)
{
    uint8_t header[40];
    wav_info_t result = {0};
    bool fmt = false, data = false;
    if (!read || !info || size < 12 || read(ctx, header, 12, 0) != 12 ||
        memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4))
        return false;
    uint64_t end = (uint64_t)u32(header + 4) + 8;
    if (end > size || end < 12)
        return false;
    uint64_t offset = 12;
    unsigned chunks = 0;
    while (offset < end) {
        if (++chunks > 256 || end - offset < 8 || read(ctx, header, 8, offset) != 8)
            return false;
        uint32_t length = u32(header + 4);
        offset += 8;
        uint64_t padded = (uint64_t)length + (length & 1);
        if (padded > end - offset)
            return false;
        if (!memcmp(header, "fmt ", 4)) {
            if (fmt || length < 16)
                return false;
            uint32_t count = length < 40 ? length : 40;
            if (read(ctx, header, count, offset) != count)
                return false;
            uint16_t encoding = u16(header);
            result.channels = u16(header + 2);
            result.sample_rate = u32(header + 4);
            result.block_align = u16(header + 12);
            result.bits = u16(header + 14);
            if (encoding == 0xfffe) {
                if (length < 40)
                    return false;
                static const uint8_t pcm_guid[16] = {1,    0, 0, 0,    0, 0,    0x10, 0,
                                                     0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
                uint32_t mask = u32(header + 20);
                if (length < 40 || u16(header + 16) != 22 || u16(header + 18) != result.bits ||
                    memcmp(header + 24, pcm_guid, 16) ||
                    (mask && mask != (result.channels == 1 ? 4u : 3u)))
                    return false;
                encoding = 1;
            }
            if (encoding != 1 || (result.channels != 1 && result.channels != 2) ||
                (result.bits != 8 && result.bits != 16 && result.bits != 24 && result.bits != 32) ||
                result.sample_rate < 8000 || result.sample_rate > 96000 ||
                result.block_align != result.channels * (result.bits / 8) ||
                u32(header + 8) != result.sample_rate * result.block_align)
                return false;
            fmt = true;
        } else if (!memcmp(header, "data", 4)) {
            if (data || !length)
                return false;
            result.data_offset = offset;
            result.data_bytes = length;
            data = true;
        }
        offset += padded;
    }
    if (!fmt || !data || result.data_bytes % result.block_align)
        return false;
    result.frames = result.data_bytes / result.block_align;
    *info = result;
    return true;
}
static int16_t sample(const uint8_t *p, unsigned bits)
{
    if (bits == 8)
        return (int16_t)(((int)p[0] - 128) * 256);
    if (bits == 16) {
        uint32_t value = u16(p);
        return (int16_t)(value >= 32768 ? (int32_t)value - 65536 : (int32_t)value);
    }
    if (bits == 24) {
        uint32_t value = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
        int32_t signed_value = value >= 0x800000 ? (int32_t)value - 0x1000000 : (int32_t)value;
        return (int16_t)(signed_value / 256);
    }
    uint32_t value = u32(p);
    int64_t signed_value = value >= 0x80000000u ? (int64_t)value - 0x100000000ll : value;
    return (int16_t)(signed_value / 65536);
}
void wav_frame_s16(const wav_info_t *info, const uint8_t *frame, int16_t output[2])
{
    output[0] = sample(frame, info->bits);
    output[1] = info->channels == 2 ? sample(frame + info->bits / 8, info->bits) : output[0];
}
