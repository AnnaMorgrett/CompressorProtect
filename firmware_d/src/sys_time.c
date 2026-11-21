#include "sys_time.h"
#include "ch32fun.h"

// CH32V003 SysTick runs at SYSCLK = 48 MHz
// CNT increments every clock cycle (48 cycles per microsecond)
static uint32_t overflow_ms = 0;
static uint32_t last_cnt = 0;

void sys_time_init(void) {
    SysTick->CTLR = 1; // Enable counter
    overflow_ms = 0;
    last_cnt = 0;
}

uint32_t millis(void) {
    uint32_t cnt = SysTick->CNT;
    uint32_t delta = cnt - last_cnt;
    if (delta >= 48000) {
        uint32_t inc = delta / 48000;
        overflow_ms += inc;
        last_cnt += inc * 48000;
    }
    return overflow_ms;
}

uint32_t micros(void) {
    return millis() * 1000 + (SysTick->CNT - last_cnt) / 48;
}
