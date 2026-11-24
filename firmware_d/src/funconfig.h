#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

#define CH32V003 1

/* Clock: 24 MHz HSI x PLL(2) = 48 MHz. HSI stays on so SWIO (PD1) keeps working. */
#define FUNCONF_USE_HSI           1
#define FUNCONF_USE_HSE           0
#define FUNCONF_USE_PLL           1
#define FUNCONF_SYSTEM_CORE_CLOCK 48000000

/* Set to 1 together with ENABLE_SWIO_LOG in config.h for telemetry over SWIO */
#define FUNCONF_USE_DEBUGPRINTF   0
#define FUNCONF_USE_UARTPRINTF    0

#endif