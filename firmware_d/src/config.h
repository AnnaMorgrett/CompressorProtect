/* [#01] config.h - EasyEDA V1.0 SOP16 pins, clock/ADC constants, feature switches, defaults. */
#ifndef CONFIG_H
#define CONFIG_H

#if defined(__has_include) && __has_include("ch32fun.h")
  #include "ch32fun.h"
#else
  #include "ch32v003fun.h"
#endif
#include <stdint.h>
#include <stdbool.h>

/* ---- feature switches --------------------------------------------------- */
#define ENABLE_CT        1   /* ZHT103 current sensing on PA1 */
#define ENABLE_NTC       0   /* Eliminated: NTC1/NTC2 headers sit on PC2/PC1 (no ADC) */
#define ENABLE_SWIO_LOG  0   /* Also set FUNCONF_USE_DEBUGPRINTF 1 in funconfig.h */

#if ENABLE_NTC
  #error "NTC pins PC1/PC2 have no ADC channel on the CH32V003A4M6. Rewire first."
#endif

/* ---- EasyEDA V1.0 SOP16 pin map ----------------------------------------- */
#define PIN_BUZZER   PC0     /* Q1 S8050, active high              */
#define PIN_TM_DIO   PC3     /* TM1637 DIO                         */
#define PIN_TM_CLK   PC4     /* TM1637 CLK                         */
#define PIN_CAL      PC6     /* H1 pin 3, short to GND = CAL mode  */
#define PIN_RELAY    PD4     /* Q2 S8050, active high              */
#define PIN_LED      PD5     /* LED1, active high                  */
#define PIN_KEY      PD6     /* Resistor-ladder keys (analog A6)   */
#define PIN_VAC      PA2     /* ZMPT101B output (analog A0)        */
#define PIN_CT       PA1     /* ZHT103 output   (analog A1)        */
/* PC1, PC2 (NTC headers) and PC7 (TX header): configured as pull-up inputs */

#define ADC_CH_VAC    0
#define ADC_CH_CT     1
#define ADC_CH_KEY    6
#define ADC_CH_VREF   8      /* Internal 1.20 V reference */

#if ENABLE_CT
  #define ADC_NCH     2      /* Interleaved samples: VAC, CT */
#else
  #define ADC_NCH     1
#endif

/* ---- clock / ADC / timing ----------------------------------------------- */
#define SYSCLK_HZ            48000000UL
#define SAMPLE_RATE_HZ       6400UL                 /* Per channel: 128 samples per 50 Hz cycle */
#define SAMPLES_PER_BLOCK    128U
#define BLOCK_MS             20U
#define ADC_SAMPLE_TIME_CODE 4U                     /* 4 = 43 cycles @ 12 MHz (R_AIN <= 6 kOhm) */
#define ADC_WARMUP_BLOCKS    5U                     /* Drop blocks while AC-coupling caps charge */
#define SYSTICK_TICKS_1MS    (SYSCLK_HZ / 1000UL)   /* 48,000 */
#define IWDG_PRESCALER_CODE  4U                     /* LSI 128 kHz / 64 = 2 kHz */
#define IWDG_RELOAD_TICKS    2000U                  /* 1.0 s timeout */

/* ---- analog front-end (sets DEFAULT gain, calibration trims it) ---------
 * Stock: R14 = 820k, R8 = 100R (~27 mV RMS at 220 V -> ~5 ADC counts RMS; use RMS_AVG_BLOCKS 4).
 * Upgraded: R14 = 220k, R8 = 1k (~1 V RMS -> set RMS_AVG_BLOCKS 1 for 20 ms cutoff). */
#define FE_R14_OHM     820000UL
#define FE_R8_OHM      100UL
#define FE_CT_RATIO    2000UL
#define FE_R2_OHM      100UL
#define RMS_AVG_BLOCKS 4U

/* Pin mV RMS -> 0.1 V / 0.01 A, in Q8 */
#define DEF_GAIN_V_Q8  ((256UL * FE_R14_OHM) / (FE_R8_OHM * 100UL))
#define DEF_GAIN_A_Q8  ((256UL * FE_CT_RATIO) / (FE_R2_OHM * 10UL))

/* ---- keys (resistor ladder on PD6 w/ 10k pull-up, 10-bit ADC at VDD) ----
 * None ~1023, Left '-' (4k7) ~327, Center 'OK' (2k2) ~184, Right '+' (0R..1k) ~0..93 */
#define KEY_ADC_NONE_MIN    720U
#define KEY_ADC_MINUS_MIN   255U
#define KEY_ADC_MENU_MIN    115U
#define KEY_SCAN_MS         5UL    /* 200 Hz polling rate for crisp response */
#define KEY_DEBOUNCE_SCANS  2U     /* 10 ms debounce */
#define KEY_REPEAT_DELAY_MS 400UL
#define KEY_REPEAT_MS       80UL   /* 12.5 Hz smooth auto-repeat */
#define KEY_LONG_MS         3000UL

/* ---- display filter ----------------------------------------------------- */
#define V_SNAP_DV           20     /* 2.0 V: EMA jumps straight to new value */
#define V_DEAD_DV           3      /* 0.3 V: display deadband */
#define A_SNAP_CA           20     /* 0.20 A */
#define A_DEAD_CA           3      /* 0.03 A */
#define DISPLAY_BRIGHTNESS  4U     /* 0..7 */

/* ---- protection timing -------------------------------------------------- */
#define TRIP_SHOW_MS        3000UL
#define HEALTHY_CLEAR_MS    180000UL   /* 3 min running OK clears OC fail counter */
#define LOCKOUT_BEEP_MS     30000UL

/* ---- factory defaults (Indonesia 220 V / 50 Hz) ------------------------- */
#define DEF_OV_V            245U
#define DEF_UV_V            180U
#define DEF_HYS_V           10U
#define DEF_OC_CA           140U   /* 1.40 A */
#define DEF_INR_X10         25U    /* 2.5 x  */
#define DEF_TIN_DS          15U    /* 1.5 s  */
#define DEF_TOC_DS          5U     /* 0.5 s  */
#define DEF_DELAY_MIN       5U     /* 5 min  */
#define DEF_RET_CNT         3U     /* 3 trips */

#endif /* CONFIG_H */