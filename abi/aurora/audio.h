#ifndef AURORA_AUDIO_H
#define AURORA_AUDIO_H
#include <stdint.h>
#define PCM_RATE 48000u
#define PCM_MAX_FRAMES 1024u
#define PCM_STOP 0u
#define PCM_STATUS 1u
#define PCM_FINISH 2u
#define PCM_START 3u
typedef struct {
    uint32_t available,queued_frames,running,underruns;
    uint64_t played_frames;
} pcm_status_t;
typedef struct {
    uint32_t playing,sample_rate,channels,bits;
    uint64_t position_frames,total_frames;
    int64_t error;
} audio_info_t;
#endif
