#include "sys_time.h"

uint32_t millis(void) {
    static uint32_t last_cnt     = 0;
    static uint32_t sub_ms_ticks = 0;
    static uint32_t total_ms     = 0;
    const uint32_t ticks_per_ms  = FUNCONF_SYSTEM_CORE_CLOCK / 1000UL;

    uint32_t now_cnt = SysTick->CNT;
    uint32_t elapsed = now_cnt - last_cnt;
    last_cnt = now_cnt;

    sub_ms_ticks += elapsed;
    if (sub_ms_ticks >= ticks_per_ms) {
        uint32_t inc_ms = sub_ms_ticks / ticks_per_ms;
        total_ms += inc_ms;
        sub_ms_ticks -= inc_ms * ticks_per_ms;
    }
    return total_ms;
}