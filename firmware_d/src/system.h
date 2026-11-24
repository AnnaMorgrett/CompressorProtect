/* [#02] system.h - shared types and inter-module prototypes. */
#ifndef SYSTEM_H
#define SYSTEM_H

#include "config.h"

/* Everything stored in the top 64-byte flash page (0x08003FC0, last u16 = CRC16). */
typedef struct __attribute__((aligned(4))) {
    uint16_t ov_v;        /* P01  over-voltage trip, V                 */
    uint16_t uv_v;        /* P02  under-voltage trip, V                */
    uint16_t hys_v;       /* P03  hysteresis, V                        */
    uint16_t oc_ca;       /* P04  over-current, 0.01 A                 */
    uint16_t inr_x10;     /* P05  inrush multiplier, 0.1 x             */
    uint16_t tin_ds;      /* P06  inrush window, 0.1 s                 */
    uint16_t toc_ds;      /* P07  over-current time, 0.1 s             */
    uint16_t delay_min;   /* P08  restart delay, minutes               */
    uint16_t ret_cnt;     /* P09  OC trips before lockout              */
    uint16_t rsv_p10;     /* reserved (was NTC over-temp)              */
    uint16_t rsv_p11;
    uint16_t cal_v_q12;   /* VAC trim, 4096 = 1.000                    */
    uint16_t cal_a_q12;   /* CT trim,  4096 = 1.000                    */
    uint16_t zero_v_q6;   /* VAC noise floor, RMS counts x 64          */
    uint16_t zero_a_q6;   /* CT noise floor                            */
    uint16_t oc_fail;     /* OC_Fail_Count (LOC3)                      */
    uint16_t version;
    uint16_t spare[14];
    uint16_t crc;
} SystemConfig_t;

_Static_assert(sizeof(SystemConfig_t) == 64, "SystemConfig_t must fill one 64-byte flash page");

/* Written by the DMA interrupt (adc_dsp.c), read by everyone else. */
typedef struct {
    volatile uint16_t vac_raw_dv;    /* last 20 ms block, 0.1 V        (fast trip path) */
    volatile uint16_t vac_disp_dv;   /* EMA + deadband, 0.1 V          (stable display) */
    volatile uint16_t vac_avg_dv;    /* EMA only, 0.1 V                (calibration)    */
    volatile uint16_t i_raw_ca;      /* 0.01 A */
    volatile uint16_t i_disp_ca;
    volatile uint16_t i_avg_ca;
    volatile uint16_t vac_rms_q6;    /* raw ADC RMS counts x 64, before zero/gain */
    volatile uint16_t ct_rms_q6;
    volatile uint16_t vdd_mv;        /* from VREFINT */
    volatile uint32_t blocks;        /* completed valid 20 ms blocks */
} SensorData_t;

typedef enum { FAULT_NONE = 0, FAULT_UV, FAULT_OV, FAULT_OC, FAULT_OT } FaultCode_t;
typedef enum { MODE_PROTECT, MODE_SETTINGS, MODE_CAL } AppMode_t;
typedef enum { KEY_EVT_NONE = 0, KEY_EVT_MINUS, KEY_EVT_PLUS, KEY_EVT_OK, KEY_EVT_OK_LONG } KeyEvent_t;
typedef enum { LED_OFF = 0, LED_ON, LED_SLOW, LED_FAST, LED_DOUBLE } LedMode_t;
typedef enum { BEEP_NONE = 0, BEEP_CONFIRM, BEEP_ALERT, BEEP_LOCK } BeepPattern_t;

extern SystemConfig_t g_cfg;
extern SensorData_t   g_sens;
extern volatile uint32_t g_ms;

/* [#03] hal_sys.c */
void     hal_init(void);
void     hal_wdg_kick(void);
void     hal_relay_set(bool on);
bool     hal_relay_is_on(void);
void     hal_log(void);

/* [#04] nvm_flash.c */
void     nvm_params_defaults(SystemConfig_t *c);
void     nvm_load(void);
bool     nvm_save(void);
void     nvm_clear_fail(void);

/* [#05] adc_dsp.c */
void     adc_init(void);
void     adc_start(void);
void     adc_service(void);                  /* call from main loop: refreshes VDD */
uint16_t adc_read_injected(uint8_t channel); /* main loop only */
uint16_t adc_measure_noise_q6(uint8_t which);/* which: ADC_CH_VAC or ADC_CH_CT */

/* [#06] display_tm1637.c */
void     display_init(void);
void     display_text(const char *s);                /* up to 4 chars */
void     display_uint(uint16_t v);                   /* right aligned */
void     display_uint_pad4(uint16_t v);              /* 0000..9999    */
void     display_labeled(char label, uint16_t v);    /* e.g. A140     */
void     display_mmss(uint16_t seconds);             /* MM:SS         */
void     display_flush(void);

/* [#07] input_alerts.c */
void       input_init(void);
void       input_poll(void);
KeyEvent_t input_get_event(void);
bool       input_is_repeating(void);
void       input_wait_release(void);
bool       input_menu_held_at_boot(void);
bool       input_cal_active(void);
void       alerts_set_led(LedMode_t m);
void       alerts_beep(BeepPattern_t p);
void       alerts_tick_1ms(void);            /* from SysTick ISR */

/* [#08] fsm_protect.c */
void        fsm_init(void);
void        fsm_step(void);
void        fsm_on_block(const SensorData_t *s);   /* from DMA ISR, 20 ms */
void        fsm_refresh_limits(void);
void        fsm_relay_on(void);
FaultCode_t fsm_fault(void);
bool        fsm_voltage_ok_to_start(void);
const char *fsm_fault_text(FaultCode_t f);

/* [#09] menu_cal.c */
void     mode_settings(void);
void     mode_cal(void);

#endif /* SYSTEM_H */