/* Bounded PC-speaker HAL. Policy and requests live in audiod. */
#include <arch/audio.h>
#include <arch/io.h>
#include <arch/pit.h>
#include <aurora/security.h>
static uint64_t stop_tick;
int audio_tone(uint32_t frequency, uint32_t duration)
{
    if (frequency < 40 || frequency > 8000 || !duration || duration > 200)
        return -AURORA_EINVAL;
    if (stop_tick)
        return -AURORA_EBUSY;
    uint32_t divisor = 1193182u / frequency;
    outb(0x43, 0xb6);
    outb(0x42, (uint8_t)divisor);
    outb(0x42, (uint8_t)(divisor >> 8));
    outb(0x61, inb(0x61) | 3u);
    stop_tick = pit_ticks() + duration;
    return 0;
}
void audio_tick(uint64_t ticks)
{
    pcm_tick();
    if (stop_tick && ticks >= stop_tick) {
        outb(0x61, inb(0x61) & (uint8_t)~3u);
        stop_tick = 0;
    }
}
