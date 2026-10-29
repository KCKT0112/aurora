#include <aurora/wav.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    const uint8_t *bytes;
    size_t size;
} source_t;
static int64_t read_at(void *ctx, void *buffer, uint32_t n, uint64_t at)
{
    source_t *s = ctx;
    if (at > s->size || n > s->size - at)
        return -1;
    memcpy(buffer, s->bytes + at, n);
    return n;
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static void verify_file(const char *name)
{
    FILE *f = fopen(name, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    rewind(f);
    uint8_t *data = malloc(n);
    assert(data && fread(data, 1, n, f) == n);
    fclose(f);
    source_t s = {data, n};
    wav_info_t info;
    assert(wav_parse(read_at, &s, n, &info));
    assert(info.frames > 0 && info.data_offset + info.data_bytes <= n);
    printf("WAV verified: %u Hz %u-bit %u channels, %llu frames\n", info.sample_rate, info.bits,
           info.channels, (unsigned long long)info.frames);
    free(data);
}
int main(int argc, char **argv)
{
    uint8_t bytes[52] = {'R', 'I', 'F', 'F',  44, 0, 0, 0,    'W',  'A',  'V', 'E',  'f',
                         'm', 't', ' ', 16,   0,  0, 0, 1,    0,    2,    0,   0x80, 0xbb,
                         0,   0,   0,   0x65, 4,  0, 6, 0,    24,   0,    'd', 'a',  't',
                         'a', 6,   0,   0,    0,  0, 0, 0x80, 0xff, 0xff, 0x7f};
    /* Exact 50-byte PCM file: two 24-bit samples in one frame. */
    put32(bytes + 4, 42);
    source_t source = {bytes, 50};
    wav_info_t info;
    assert(wav_parse(read_at, &source, 50, &info));
    assert(info.frames == 1);
    int16_t sample[2];
    wav_frame_s16(&info, bytes + 44, sample);
    assert(sample[0] == -32768 && sample[1] == 32767);
    bytes[20] = 3;
    assert(!wav_parse(read_at, &source, 50, &info));
    bytes[20] = 1;
    put32(bytes + 40, UINT32_MAX);
    assert(!wav_parse(read_at, &source, 50, &info));
    put32(bytes + 40, 6);
    bytes[32] = 4;
    assert(!wav_parse(read_at, &source, 50, &info));
    bytes[32] = 6;
    bytes[28] = 0;
    bytes[29] = 0;
    assert(!wav_parse(read_at, &source, 50, &info));
    bytes[29] = 0x65;
    for (size_t i = 0; i < 50; i++)
        assert(!wav_parse(read_at, &source, i, &info));
    uint32_t random = 7;
    for (unsigned i = 0; i < 5000; i++) {
        uint8_t copy[50];
        memcpy(copy, bytes, 50);
        random = random * 1664525 + 1013904223;
        copy[random % 44] ^= (uint8_t)(1u << (random % 8));
        source_t fuzz = {copy, 50};
        (void)wav_parse(read_at, &fuzz, 50, &info);
    }
    wav_info_t mono = {.channels = 1, .bits = 8};
    uint8_t silence = 128;
    wav_frame_s16(&mono, &silence, sample);
    assert(!sample[0] && !sample[1]);
    mono.bits = 32;
    uint8_t min32[] = {0, 0, 0, 0x80};
    wav_frame_s16(&mono, min32, sample);
    assert(sample[0] == -32768);
    for (int i = 1; i < argc; i++)
        verify_file(argv[i]);
    puts("WAV parser: bounds, format rejection, conversion and mutations passed");
}
