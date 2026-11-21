#ifndef SYS_TIME_H
#define SYS_TIME_H

#include <stdint.h>

void     sys_time_init(void);
uint32_t millis(void);
uint32_t micros(void);

#endif
