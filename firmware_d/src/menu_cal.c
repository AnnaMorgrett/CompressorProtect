/* [#09] menu_cal.c - Mode 2 (Sett: P01..P09, SAVE, rSt) and Mode 3 (CAL: C-0, C-V, C-A, SAVE). */
#include "system.h"

typedef enum { FMT_UINT, FMT_MINUTES, FMT_AMPS } ParamFmt_t;
typedef struct {
    const char *code;
    uint16_t   *value;
    uint16_t    min, max, step;
    ParamFmt_t  fmt;
} Param_t;

static const Param_t k_params[] = {
    { "P01", &g_cfg.ov_v,      230U, 270U,  1U, FMT_UINT    },
    { "P02", &g_cfg.uv_v,      140U, 210U,  1U, FMT_UINT    },
    { "P03", &g_cfg.hys_v,       2U,  20U,  1U, FMT_UINT    },
    { "P04", &g_cfg.oc_ca,      50U, 999U,  5U, FMT_AMPS    },
    { "P05", &g_cfg.inr_x10,    15U,  50U,  1U, FMT_UINT    },
    { "P06", &g_cfg.tin_ds,      5U,  50U,  1U, FMT_UINT    },
    { "P07", &g_cfg.toc_ds,      1U,  50U,  1U, FMT_UINT    },
    { "P08", &g_cfg.delay_min,   1U,  10U,  1U, FMT_MINUTES },
    { "P09", &g_cfg.ret_cnt,     1U,   5U,  1U, FMT_UINT    },
};
#define N_PARAMS   (sizeof(k_params) / sizeof(k_params[0]))
#define ITEM_SAVE  N_PARAMS
#define ITEM_RST   (N_PARAMS + 1U)
#define ITEM_TOTAL (N_PARAMS + 2U)

static void ui_idle(void)
{
    hal_wdg_kick();
    adc_service();
    input_poll();
    display_flush();
}

static void halt_done(void)
{
    hal_relay_set(false);
    alerts_set_led(LED_OFF);
    display_text("donE");
    for (;;) {
        ui_idle();
        hal_relay_set(false);
    }
}

static void show_param_value(const Param_t *p)
{
    switch (p->fmt) {
        case FMT_MINUTES: display_mmss(*p->value * 60U); break;
        case FMT_AMPS:    display_labeled('A', *p->value); break;
        default:          display_uint(*p->value); break;
    }
}

static bool window_ok(void)
{
    return (g_cfg.ov_v - g_cfg.hys_v) > (g_cfg.uv_v + g_cfg.hys_v + 5U);
}

static void flash_message(const char *s, uint32_t ms)
{
    display_text(s);
    uint32_t t = g_ms;
    while ((uint32_t)(g_ms - t) < ms) ui_idle();
}

/* ---- Mode 2: 2-Level Settings Menu ---------------------------------------
 * 1. Show "Sett" -> press Center (OK) to enter parameter menu ("P01")
 * 2. Browse P01..P09 -> SAVE -> rSt with '+' and '-' (clamped at both ends)
 * 3. Press Center (OK) on P01..P09 to show/edit value with '+' and '-'
 * 4. Press Center (OK) when done to return to parameter menu
 * 5. Press Center (OK) on SAVE to commit & finish; on rSt to reset defaults */
void mode_settings(void)
{
    hal_relay_set(false);
    alerts_set_led(LED_SLOW);
    display_text("Sett");
    input_wait_release();
    while (input_get_event() != KEY_EVT_OK) ui_idle();

    uint8_t step    = 0;
    bool    editing = false;

    for (;;) {
        ui_idle();
        KeyEvent_t e = input_get_event();

        if (!editing) {
            /* Level 1: Navigate parameter codes (P01..P09, SAVE, rSt) without wrapping */
            if (step < N_PARAMS)        display_text(k_params[step].code);
            else if (step == ITEM_SAVE) display_text("SAVE");
            else                        display_text("rSt");

            if (e == KEY_EVT_PLUS) {
                if ((step + 1U) < ITEM_TOTAL) {
                    step++;
                }
            } else if (e == KEY_EVT_MINUS) {
                if (step > 0U) {
                    step--;
                }
            } else if (e == KEY_EVT_OK) {
                if (step < N_PARAMS) {
                    editing = true;
                } else if (step == ITEM_SAVE) {
                    if (!window_ok()) {
                        flash_message("Err", 1500UL);
                        continue;
                    }
                    (void)nvm_save();
                    alerts_beep(BEEP_CONFIRM);
                    halt_done();
                } else {
                    nvm_params_defaults(&g_cfg);
                    g_cfg.oc_fail = 0U;
                    (void)nvm_save();
                    alerts_beep(BEEP_CONFIRM);
                    flash_message("donE", 800UL);
                    step = 0U;
                }
            }
        } else {
            /* Level 2: Show & edit active parameter value */
            const Param_t *p = &k_params[step];
            show_param_value(p);

            if (e == KEY_EVT_PLUS || e == KEY_EVT_MINUS) {
                int32_t nv = (int32_t)*p->value + ((e == KEY_EVT_PLUS) ? (int32_t)p->step : -(int32_t)p->step);
                if (nv < (int32_t)p->min) nv = (int32_t)p->min;
                if (nv > (int32_t)p->max) nv = (int32_t)p->max;
                *p->value = (uint16_t)nv;
            } else if (e == KEY_EVT_OK) {
                editing = false;
            }
        }
    }
}

/* ---- Mode 3: Sensor Calibration ------------------------------------------ */
static void wait_ok(const char *label)
{
    display_text(label);
    input_wait_release();
    while (input_get_event() != KEY_EVT_OK) ui_idle();
}

static void adjust_trim(uint16_t *trim_q12, KeyEvent_t e)
{
    int32_t step = input_is_repeating() ? 24 : 2;
    int32_t nv   = (int32_t)*trim_q12 + ((e == KEY_EVT_PLUS) ? step : -step);
    if (nv < 512)   nv = 512;
    if (nv > 16383) nv = 16383;
    *trim_q12 = (uint16_t)nv;
}

void mode_cal(void)
{
    hal_relay_set(false);
    alerts_set_led(LED_FAST);

    /* Step 1: Zero offset baseline (0V AC, 0A) */
    wait_ok("C-0");
    display_text("----");
    display_flush();
    g_cfg.zero_v_q6 = adc_measure_noise_q6(ADC_CH_VAC);
#if ENABLE_CT
    g_cfg.zero_a_q6 = adc_measure_noise_q6(ADC_CH_CT);
#endif

    /* Step 2: AC Voltage gain trim (0.1 V resolution) */
    display_text("C-V");
    input_wait_release();
    for (;;) {
        ui_idle();
        KeyEvent_t e = input_get_event();
        if (e == KEY_EVT_PLUS || e == KEY_EVT_MINUS) adjust_trim(&g_cfg.cal_v_q12, e);
        if (e == KEY_EVT_OK) break;
        display_uint_pad4(g_sens.vac_avg_dv);
    }

#if ENABLE_CT
    /* Step 3: Current gain trim (press OK to energize relay, or '+' to skip to SAVE) */
    fsm_refresh_limits();
    display_text("C-A");
    input_wait_release();
    bool running = false;
    for (;;) {
        ui_idle();
        KeyEvent_t e = input_get_event();
        if (!running) {
            if (e == KEY_EVT_PLUS) break; /* Skip C-A when no 12V / load is attached */
            if (e == KEY_EVT_OK) {
                if (fsm_voltage_ok_to_start()) { fsm_relay_on(); running = true; }
                else flash_message("E-UV", 1000UL);
            }
            display_text("C-A");
            continue;
        }
        if (fsm_fault() != FAULT_NONE) {
            hal_relay_set(false);
            alerts_beep(BEEP_ALERT);
            flash_message(fsm_fault_text(fsm_fault()), 2000UL);
            running = false;
            display_text("C-A");
            input_wait_release();
            continue;
        }
        if (e == KEY_EVT_PLUS || e == KEY_EVT_MINUS) adjust_trim(&g_cfg.cal_a_q12, e);
        if (e == KEY_EVT_OK) { hal_relay_set(false); break; }
        display_labeled('A', g_sens.i_avg_ca);
    }
#endif

    /* Step 4: Save calibration to Flash */
    wait_ok("SAVE");
    (void)nvm_save();
    alerts_beep(BEEP_CONFIRM);
    halt_done();
}