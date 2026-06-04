#include "stage13.h"

static uint32_t g_ml_timebase_freq;
static uint64_t g_ml_timebase_start;

void ml_init_timebase(void)
{
    timebase_init();
    g_ml_timebase_freq = timebase_freq_hz();
    g_ml_timebase_start = timebase_ticks();

    xnu_log_kv32("ml_timebase_freq", g_ml_timebase_freq);
    xnu_log_kv64("ml_timebase_start", g_ml_timebase_start);
}

uint64_t ml_get_timebase(void)
{
    return timebase_ticks() - g_ml_timebase_start;
}

uint32_t ml_get_timebase_frequency(void)
{
    return g_ml_timebase_freq;
}
