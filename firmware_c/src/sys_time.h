#ifndef SYS_TIME_H
#define SYS_TIME_H

#include "board.h"

// Safe for short intervals (< 89.4 seconds)
static inline uint32_t micros(void)
{
    return SysTick->CNT / (FUNCONF_SYSTEM_CORE_CLOCK / 1000000UL);
}

// Rollover-safe 32-bit millisecond clock (wraps every 49.7 days)
uint32_t millis(void);

#endif