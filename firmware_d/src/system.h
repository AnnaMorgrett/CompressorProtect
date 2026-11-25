/* [#02] system.h - Shared data structures, enums, global state, and module APIs. */
#ifndef SYSTEM_H
#define SYSTEM_H

#include "config.h"

/* Persistent configuration stored in the top 64-byte Flash page (0x08003FC0).
 * Must be exactly 64 bytes so one fast-page erase/write commits the entire struct. */
typedef struct __attribute__((aligned(4))) {
    uint16_t ov_v;        /* P01: Over-voltage trip limit (Volts)                       */
    uint16_t uv_v;        /* P02: Under-voltage trip limit (Volts)                      */
    uint16_t hys_v;       /* P03: Voltage recovery hysteresis (Volts)                   */
    uint16_t oc_ca;       /* P04: Running over-current trip limit (0.01 A units)        */
    uint16_t inr_x10;     /* P05: Startup inrush current multiplier (0.1x units)        */
    uint16_t tin_ds;      /* P06: Startup inrush grace window duration (0.1 s units)    */
    uint16_t toc_ds;      /* P07: Continuous over-current trip delay (0.1 s units)      */
    uint16_t delay_min;   /* P08: Power-on / post-trip restart countdown (minutes)      */
    uint16_t ret_cnt;     /* P09: Max over-current trips before LOC3 lockout (count)    */
    uint16_t rsv_p10;     /* Reserved (formerly P10 NTC over-temperature)               */
    uint16_t rsv_p11;     /* Reserved (formerly P11 NTC hysteresis)                     */
    uint16_t cal_v_q12;   /* AC voltage gain trim factor in Q12 (4096 = 1.0000x)        */
    uint16_t cal_a_q12;   /* AC current gain trim factor in Q12 (4096 = 1.0000x)        */
    uint16_t zero_v_q6;   /* AC voltage zero-input noise floor (ADC RMS counts x 64)    */
    uint16_t zero_a_q6;   /* AC current zero-input noise floor (ADC RMS counts x 64)    */
    uint16_t oc_fail;     /* Persistent consecutive over-current trip counter (0..P09)  */
    uint16_t version;     /* Struct layout version tag (NVM_VERSION)                    */
    uint16_t spare[14];   /* Padding to fill 64-byte Flash page                         */
    uint16_t crc;         /* CCITT-FALSE CRC16 checksum over the first 62 bytes         */
} SystemConfig_t;

_Static_assert(sizeof(SystemConfig_t) == 64, "SystemConfig_t must fill one 64-byte flash page");

/* Live sensor telemetry updated every 20 ms inside the DMA1 Channel 1 interrupt.
 * Marked volatile because values are written in ISR context and read in main(). */
typedef struct {
    volatile uint16_t vac_raw_dv;    /* Unfiltered 20 ms RMS voltage in 0.1 V (used for fast trips)  */
    volatile uint16_t vac_disp_dv;   /* EMA + deadband smoothed voltage in 0.1 V (used for RUN UI)   */
    volatile uint16_t vac_avg_dv;    /* EMA-only voltage in 0.1 V (no deadband)                      */
    volatile uint16_t i_raw_ca;      /* Unfiltered 20 ms RMS current in 0.01 A (used for fast trips) */
    volatile uint16_t i_disp_ca;     /* EMA + deadband smoothed current in 0.01 A (used for RUN UI)  */
    volatile uint16_t i_avg_ca;      /* EMA-only current in 0.01 A (no deadband)                     */
    volatile uint16_t vac_rms_q6;    /* Raw 20 ms AC RMS voltage in ADC counts x 64 (before zero/gain)*/
    volatile uint16_t ct_rms_q6;     /* Raw 20 ms AC RMS current in ADC counts x 64 (before zero/gain)*/
    volatile uint16_t vdd_mv;        /* Measured MCU supply voltage in mV (via internal 1.2V VREF)   */
    volatile uint32_t blocks;        /* Counter incremented every valid 20 ms AC cycle processed     */
} SensorData_t;

/* Fault codes displayed on screen and used to log trips */
typedef enum {
    FAULT_NONE = 0,   /* Normal / no active fault */
    FAULT_UV,         /* Under-voltage ("E-UV") */
    FAULT_OV,         /* Over-voltage ("E-OV") */
    FAULT_OC,         /* Over-current or locked-rotor stall ("E-OC") */
    FAULT_OT          /* Reserved over-temperature ("E-Ot") */
} FaultCode_t;

/* Top-level operating mode selected once at power-up */
typedef enum {
    MODE_PROTECT,     /* Mode 1: Normal compressor protection & countdown */
    MODE_SETTINGS,    /* Mode 2: User parameter menu ("Sett") */
    MODE_CAL          /* Mode 3: Factory sensor calibration ("C-0", "C-V", "C-A") */
} AppMode_t;

/* Debounced button events produced by input_alerts.c */
typedef enum {
    KEY_EVT_NONE = 0, /* No new button event */
    KEY_EVT_MINUS,    /* '-' pressed or auto-repeating */
    KEY_EVT_PLUS,     /* '+' pressed or auto-repeating */
    KEY_EVT_OK,       /* 'OK' (Menu) pressed */
    KEY_EVT_OK_LONG   /* 'OK' held continuously for 3.0 seconds */
} KeyEvent_t;

/* Status LED blink patterns driven by SysTick 1 ms ISR */
typedef enum {
    LED_OFF = 0,      /* Solid OFF (halted / done) */
    LED_ON,           /* Solid ON (relay closed, compressor running) */
    LED_SLOW,         /* 1 Hz blink (startup countdown delay or Settings mode) */
    LED_FAST,         /* 4 Hz rapid blink (active fault trip or Calibration mode) */
    LED_DOUBLE        /* Double-flash every 1 s (LOC3 over-current lockout) */
} LedMode_t;

/* Non-blocking buzzer tone sequences driven by SysTick 1 ms ISR */
typedef enum {
    BEEP_NONE = 0,    /* Silent */
    BEEP_CONFIRM,     /* Single 150 ms beep (relay engaged or settings saved) */
    BEEP_ALERT,       /* Three 100 ms beeps (protection fault trip) */
    BEEP_LOCK         /* Two long 400 ms beeps (LOC3 lockout alarm) */
} BeepPattern_t;

/* Global state shared across modules */
extern SystemConfig_t g_cfg;
extern SensorData_t   g_sens;
extern volatile uint32_t g_ms;   /* Free-running 1 ms uptime counter */

/* [#03] hal_sys.c - Hardware abstraction, GPIO, SysTick, Watchdog */
void     hal_init(void);
void     hal_wdg_kick(void);
void     hal_relay_set(bool on);
bool     hal_relay_is_on(void);
void     hal_log(void);

/* [#04] nvm_flash.c - Non-volatile Flash storage & CRC16 */
void     nvm_params_defaults(SystemConfig_t *c);
void     nvm_load(void);
bool     nvm_save(void);
void     nvm_clear_fail(void);

/* [#05] adc_dsp.c - TIM1 + ADC1 + DMA1 DSP engine & true-RMS math */
void     adc_init(void);
void     adc_start(void);
void     adc_service(void);                  /* Refreshes real VDD using VREFINT every 500 ms */
uint16_t adc_read_injected(uint8_t channel); /* Reads KEY or VREFINT safely between TIM1 triggers */
uint16_t adc_measure_noise_q6(uint8_t which);/* Averages RMS_AVG_BLOCKS cycles (80 ms) for zero cal */

/* [#06] display_tm1637.c - 4-digit 7-segment LED driver */
void     display_init(void);
void     display_text(const char *s);                /* Write up to 4 ASCII characters */
void     display_uint(uint16_t v);                   /* Right-aligned integer (e.g. " 220") */
void     display_uint_pad4(uint16_t v);              /* Zero-padded 4-digit integer (e.g. "2080") */
void     display_labeled(char label, uint16_t v);    /* Leading char + 3 digits (e.g. "A140") */
void     display_mmss(uint16_t seconds);             /* Minutes:Seconds with colon (e.g. "05:00") */
void     display_flush(void);                        /* Push buffer to TM1637 if changed */

/* [#07] input_alerts.c - Keypad scanner, boot-pin detection, LED & buzzer */
void       input_init(void);
void       input_poll(void);
KeyEvent_t input_get_event(void);
bool       input_is_repeating(void);
void       input_wait_release(void);
bool       input_menu_held_at_boot(void);
bool       input_cal_active(void);
void       alerts_set_led(LedMode_t m);
void       alerts_beep(BeepPattern_t p);
void       alerts_tick_1ms(void);            /* Called inside SysTick_Handler every 1 ms */

/* [#08] fsm_protect.c - Protection state machine & 20 ms ISR trip logic */
void        fsm_init(void);
void        fsm_step(void);
void        fsm_on_block(const SensorData_t *s);   /* Called inside DMA ISR every 20 ms */
void        fsm_refresh_limits(void);
void        fsm_relay_on(void);
FaultCode_t fsm_fault(void);
bool        fsm_voltage_ok_to_start(void);
const char *fsm_fault_text(FaultCode_t f);

/* [#09] menu_cal.c - Settings menu (Mode 2) & Calibration wizard (Mode 3) */
void     mode_settings(void);
void     mode_cal(void);

#endif /* SYSTEM_H */