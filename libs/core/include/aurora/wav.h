#ifndef AURORA_WAV_H
#define AURORA_WAV_H
#include <stdint.h>
#include <stdbool.h>
typedef int64_t (*wav_read_fn)(void *context,void *buffer,uint32_t count,uint64_t offset);
typedef struct {
    uint32_t sample_rate,channels,bits,block_align;
    uint64_t data_offset,data_bytes,frames;
} wav_info_t;
/* Little-endian integer PCM only. Metadata is skipped, never interpreted as audio. */
bool wav_parse(wav_read_fn read,void *context,uint64_t file_size,wav_info_t *info);
void wav_frame_s16(const wav_info_t *info,const uint8_t *frame,int16_t output[2]);
#endif
