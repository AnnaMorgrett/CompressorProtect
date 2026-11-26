/* ============================ BEGINNER GUIDE: funconfig.h ============================
 * WHAT THIS FILE IS: the settings page for the ch32fun library (the "toolbox" that gives us SystemInit,
 * Delay_Ms, funDigitalWrite...). ch32fun reads this file when it is built. Rarely needs editing.
 * ======================================================================================= */

#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

/* Tells the ch32fun library which chip family we use. */
#define CH32V003 1

/* CLOCK = how fast the chip "ticks". HSI is the chip's own built-in 24 MHz oscillator (no crystal needed). The PLL
 * doubles it to 48 MHz. We do NOT use an external crystal because its two pins are needed for the ADC sensors. */
/* Clock: 24 MHz HSI x PLL(2) = 48 MHz.
 * Keep HSI active (and HSE off) so PA1/PA2 stay available for ADC and SWIO (PD1) works. */
#define FUNCONF_USE_HSI           1
#define FUNCONF_USE_HSE           0
#define FUNCONF_USE_PLL           1
#define FUNCONF_HSITRIM           0x10       /* Factory midpoint calibration trim for accurate 48 MHz */
#define FUNCONF_SYSTEM_CORE_CLOCK 48000000
#define FUNCONF_USE_CLK_SEC       0          /* Disable HSE Clock Security System (no external crystal) */
#define FUNCONF_HSE_BYPASS        0          /* External crystal bypass disabled */

/* Options that make interrupts and delays behave exactly as hal_sys.c expects. Do not change unless you know why. */
/* Interrupt & Timing Stability (must match hal_sys.c configuration) */
#define FUNCONF_SYSTICK_USE_HCLK  1          /* Match hal_sys.c ST_STCLK (48 MHz) so Delay_Us/Ms are exact */
#define FUNCONF_ENABLE_HPE        0          /* Disable Hardware Prologue/Epilogue; use standard GCC ISR ABI */
#define FUNCONF_TINYVECTOR        0          /* Keep full NVIC vector table for DMA1_Channel1 & SysTick IRQs */
#define FUNCONF_CUSTOM_MTVEC      0          /* Use standard ch32v003fun interrupt vector table */
#define FUNCONF_DEBUG_HARDFAULT   0          /* Do not hang waiting for debugger on fault; let IWDG reset */

/* Debug printing over the single programming wire (SWIO). Off by default. UART printing must stay off because
 * that pin (PD5) drives the status LED. */
/* Debug Logging (set FUNCONF_USE_DEBUGPRINTF=1 & FUNCONF_NULL_PRINTF=0 when ENABLE_SWIO_LOG=1) */
#define FUNCONF_USE_DEBUGPRINTF   0
#define FUNCONF_USE_UARTPRINTF    0          /* Keep 0: UART1_TX shares PD5 with Status LED */
#define FUNCONF_NULL_PRINTF       1          /* Stub printf safely when SWIO debug is disabled */

#endif /* _FUNCONFIG_H */