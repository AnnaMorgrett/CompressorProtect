/* [#01] config.h - Hardware pin map, ADC/clock constants, feature switches, and defaults.
 * Target MCU: WCH CH32V003A4M6 (SOP16 package, 48 MHz RISC-V core, 16 KB Flash, 2 KB SRAM).
 * Board Revision: EasyEDA V1.0 SOP16 schematic. */
#ifndef CONFIG_H
#define CONFIG_H

#if defined(__has_include) && __has_include("ch32fun.h")
#include "ch32fun.h"
#else
#include "ch32v003fun.h"
#endif
#include <stdint.h>
#include <stdbool.h>

/* ---- Feature Switches --------------------------------------------------- */
#define ENABLE_CT 1       /* 1 = Enable ZHT103 current transformer sensing on PA1 */
#define ENABLE_NTC 0      /* 0 = Disabled: NTC1/NTC2 headers sit on PC2/PC1 (no ADC hardware) */
#define ENABLE_SWIO_LOG 0 /* 1 = Print live telemetry over SWIO debug printf (500 ms interval) */

#if ENABLE_NTC
#error "NTC pins PC1/PC2 have no ADC channel on the CH32V003A4M6. Rewire first."
#endif

/* ---- EasyEDA V1.0 SOP16 Pin Map ----------------------------------------- */
#define PIN_BUZZER PC0 /* Active-high NPN driver (Q1 S8050) for piezo buzzer         */
#define PIN_TM_DIO PC3 /* TM1637 4-digit LED display Data I/O line (push-pull)       */
#define PIN_TM_CLK PC4 /* TM1637 4-digit LED display Clock line (push-pull)          */
#define PIN_CAL PC6    /* Calibration header H1 pin 3: pull to GND at boot = CAL mode*/
#define PIN_RELAY PD4  /* Active-high NPN driver (Q2 S8050) for compressor relay     */
#define PIN_LED PD5    /* Front-panel status indicator LED1 (active high)            */
#define PIN_KEY PD6    /* Analog ADC6: 3-button resistor ladder ('-', 'OK', '+')     */
#define PIN_VAC PA2    /* Analog ADC0: AC mains voltage sense from ZMPT101B          */
#define PIN_CT PA1     /* Analog ADC1: Compressor current sense from ZHT103 CT       */
/* Note: Unused header pins PC1, PC2, and PC7 are configured as internal pull-up inputs.   */

/* ---- ADC Channel Numbers (CH32V003 Hardware Mapping) -------------------- */
#define ADC_CH_VAC 0  /* PA2 -> ADC Channel 0 */
#define ADC_CH_CT 1   /* PA1 -> ADC Channel 1 */
#define ADC_CH_KEY 6  /* PD6 -> ADC Channel 6 */
#define ADC_CH_VREF 8 /* Internal 1.20 V bandgap reference -> ADC Channel 8 */

#if ENABLE_CT
#define ADC_NCH 2 /* Regular DMA scan converts 2 channels per trigger: VAC, then CT */
#else
#define ADC_NCH 1 /* Regular DMA scan converts 1 channel per trigger: VAC only      */
#endif

/* ---- System Clock, Sampling & Watchdog Timing --------------------------- */
#define SYSCLK_HZ 48000000UL                   /* 48 MHz core & peripheral clock (HSI x 2 PLL) */
#define SAMPLE_RATE_HZ 6400UL                  /* 6400 Hz per channel = 128 samples per 50 Hz AC cycle */
#define SAMPLES_PER_BLOCK 128U                 /* 128 samples @ 6400 Hz = exactly 20 ms (1 AC cycle)   */
#define BLOCK_MS 20U                           /* Duration of one DMA half-buffer block in milliseconds*/
#define ADC_SAMPLE_TIME_CODE 4U                /* 4 = 43 ADC clock cycles @ 12 MHz (safe for <= 6k imp)*/
#define ADC_WARMUP_BLOCKS 5U                   /* Ignore first 5 blocks (100 ms) while DC-bias caps settle */
#define SYSTICK_TICKS_1MS (SYSCLK_HZ / 1000UL) /* 48,000 clock ticks per 1 ms SysTick interrupt        */
#define IWDG_PRESCALER_CODE 4U                 /* Independent Watchdog clock: 128 kHz LSI / 64 = 2 kHz */
#define IWDG_RELOAD_TICKS 2000U                /* 2000 ticks @ 2 kHz = 1.0 second watchdog reset timeout */

/* ---- Analog Front-End Hardware Values (Sets Nominal Uncalibrated Gain) --
 * VAC divider (EasyEDA V1.0 BOM): Mains -> R14 (820 kOhm) -> ZMPT101B (1:1) -> Burden R8 (100 Ohm) -> PA2.
 * Note (REQ-06): Do NOT change FE_R14_OHM / FE_R8_OHM until physical PCB resistors are reworked
 *                (e.g., R14 = 220 kOhm 1/2W, R8 = 680 Ohm) and recalibrated via Mode 3 (C-0 / C-V).
 * CT sensor:   Load current -> ZHT103 (2000:1) -> Burden R2 (100 Ohm) -> PA1. */
#define FE_R14_OHM 820000UL /* ZMPT101B primary series resistor (Ohms) */
#define FE_R8_OHM 100UL     /* ZMPT101B secondary burden resistor (Ohms) */
#define FE_CT_RATIO 2000UL  /* ZHT103 current transformer turns ratio (2000:1) */
#define FE_R2_OHM 100UL     /* ZHT103 secondary burden resistor (Ohms) */
#define RMS_AVG_BLOCKS 4U   /* Number of 20 ms AC cycles averaged during C-0 zero-floor capture */

/* Nominal conversion gains in Q8 fixed-point (multiply by 256 for integer precision):
 * - DEF_GAIN_V_Q8 converts pin mV RMS -> 0.1 V units (decivolts, dV)
 * - DEF_GAIN_A_Q8 converts pin mV RMS -> 0.01 A units (centiamps, cA) */
#define DEF_GAIN_V_Q8 ((256UL * FE_R14_OHM) / (FE_R8_OHM * 100UL))
#define DEF_GAIN_A_Q8 ((256UL * FE_CT_RATIO) / (FE_R2_OHM * 10UL))

/* ---- Front-Panel Keypad Thresholds (10-bit ADC on PD6 w/ 10k Pull-Up) ---
 * Expected ADC readings: No key = ~1023, '-' (4.7k) = ~327, 'OK' (2.2k) = ~184, '+' (0..1k) = ~25..93 */
#define KEY_ADC_NONE_MIN 720U     /* >= 720: No button pressed */
#define KEY_ADC_MINUS_MIN 255U    /* 255..719: Left '-' button */
#define KEY_ADC_MENU_MIN 115U     /* 115..254: Center 'OK' / Menu button */
#define KEY_SCAN_MS 5UL           /* Poll keypad every 5 ms (200 Hz scan rate) */
#define KEY_DEBOUNCE_SCANS 2U     /* Require 2 identical scans (10 ms) to accept a key press */
#define KEY_REPEAT_DELAY_MS 400UL /* Hold '-' or '+' for 400 ms before auto-repeat begins */
#define KEY_REPEAT_MS 80UL        /* Auto-repeat rate while held: 1 step every 80 ms (12.5 Hz) */
#define KEY_LONG_MS 3000UL        /* Hold 'OK' for 3.0 seconds to clear LOC3 over-current lockout */

/* ---- Running Display Filter Tuning (Mode 1 Normal Run Only) ------------- */
#define V_SNAP_DV 20          /* 2.0 V: If voltage jumps > 2.0 V, display snaps immediately */
#define V_DEAD_DV 3           /* 0.3 V: Ignore tiny <= 0.3 V fluctuations to prevent flicker */
#define A_SNAP_CA 20          /* 0.20 A: If current jumps > 0.20 A, display snaps immediately */
#define A_DEAD_CA 3           /* 0.03 A: Ignore tiny <= 0.03 A fluctuations to prevent flicker */
#define DISPLAY_BRIGHTNESS 4U /* TM1637 LED brightness level (0 = dimmest, 7 = brightest) */

/* ---- Protection State Machine Timers ------------------------------------ */
#define TRIP_SHOW_MS 3000UL       /* Show fault code ("E-UV", "E-OV", "E-OC") for 3 seconds */
#define HEALTHY_CLEAR_MS 180000UL /* 3 minutes (180,000 ms) of fault-free run resets OC trip counter */
#define LOCKOUT_BEEP_MS 30000UL   /* Repeat warning double-beep every 30 seconds while in LOC3 */

/* ---- Factory Default Protection Settings (Indonesia 220 V / 50 Hz) ------ */
#define DEF_OV_V 245U    /* P01: Over-voltage trip threshold = 245 V */
#define DEF_UV_V 180U    /* P02: Under-voltage trip threshold = 180 V */
#define DEF_HYS_V 10U    /* P03: Recovery hysteresis = 10 V (must reach 190..235 V to start) */
#define DEF_OC_CA 140U   /* P04: Running over-current limit = 1.40 A (140 x 0.01 A) */
#define DEF_INR_X10 25U  /* P05: Startup inrush multiplier = 2.5x (2.5 x 1.40 A = 3.50 A) */
#define DEF_TIN_DS 15U   /* P06: Startup inrush window = 1.5 s (15 x 0.1 s) */
#define DEF_TOC_DS 5U    /* P07: Running over-current trip delay = 0.5 s (5 x 0.1 s) */
#define DEF_DELAY_MIN 5U /* P08: Compressor anti-short-cycle startup delay = 5 minutes */
#define DEF_RET_CNT 3U   /* P09: Consecutive OC trips allowed before permanent LOC3 lockout */

#endif /* CONFIG_H */