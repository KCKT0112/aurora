#ifndef AURORA_ARCH_AUDIO_H
#define AURORA_ARCH_AUDIO_H
#include <stdint.h>
#include <aurora/audio.h>
int audio_tone(uint32_t frequency, uint32_t duration);
void pcm_init(void);
int pcm_write(const int16_t *samples,uint32_t count);
int pcm_control(uint32_t action,pcm_status_t *status);
void pcm_tick(void);
void audio_tick(uint64_t ticks);
#endif
