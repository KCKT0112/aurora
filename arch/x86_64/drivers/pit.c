/* Aurora v2 - PIT system tick. */
#include <arch/io.h>
#include <arch/pit.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_BASE_FREQUENCY 1193182u

static uint64_t g_ticks;

void pit_init(void)
{
    uint32_t divisor = PIT_BASE_FREQUENCY / AURORA_TICK_HZ;
    g_ticks = 0;
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

uint64_t pit_on_tick(void)
{
    return ++g_ticks;
}

uint64_t pit_ticks(void)
{
    return g_ticks;
}
