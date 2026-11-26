/* ============================ BEGINNER GUIDE: menu_cal.c ============================
 * WHAT THIS FILE IS: the two "service" modes you reach by holding something at power-on.
 *   MODE 2 "Sett"  : change the protection limits (P01..P09).
 *   MODE 3 "C-0 / C-V / C-A" : calibration - teach the chip the TRUE volts and amps.
 * Both modes keep the relay OFF (except while you test the load in C-A) and finish on "donE".
 * ====================================================================================== */

/* [#09] menu_cal.c - Mode 2 Settings Menu ("Sett") and Mode 3 Sensor Calibration ("C-0", "C-V", "C-A").
 * Both modes keep the compressor relay OFF (except when user explicitly enables load in C-A)
 * and end in "donE" halt state requiring a clean power cycle. */
#include "system.h"

/* Safety check for calibration step C-0: if the "zero" reading is bigger than this, mains is probably still connected. */
#define CAL_ZERO_MAX_Q6 300U   /* ~4.7 ADC RMS counts: reject C-0 if live 220V AC is still plugged in */

/* The menu is TABLE DRIVEN: each row of k_params (below) describes one setting: code, which variable, min, max, step,
 * and how to show it. To add a new setting you only add one row - the menu code does not change. */
typedef enum { FMT_UINT, FMT_MINUTES, FMT_AMPS } ParamFmt_t;
typedef struct {
    const char *code;          /* 3-char parameter code shown in Level 1 ("P01".."P09") */
    uint16_t   *value;         /* Pointer to live field in g_cfg */
    uint16_t    min, max, step;/* Allowed range and +/- step increment */
    ParamFmt_t  fmt;           /* Display formatter for Level 2 editing */
} Param_t;

/* Allowed ranges protect you from typing dangerous values (for example an over-voltage limit of 400 V). */
static const Param_t k_params[] = {
    { "P01", &g_cfg.ov_v,      230U, 270U,  1U, FMT_UINT    }, /* Over-voltage trip (V) */
    { "P02", &g_cfg.uv_v,      140U, 210U,  1U, FMT_UINT    }, /* Under-voltage trip (V) */
    { "P03", &g_cfg.hys_v,       2U,  20U,  1U, FMT_UINT    }, /* Voltage hysteresis (V) */
    { "P04", &g_cfg.oc_ca,      50U, 999U,  5U, FMT_AMPS    }, /* Over-current trip (0.01 A) */
    { "P05", &g_cfg.inr_x10,    15U,  50U,  1U, FMT_UINT    }, /* Inrush multiplier (0.1x) */
    { "P06", &g_cfg.tin_ds,      5U,  50U,  1U, FMT_UINT    }, /* Inrush window (0.1 s) */
    { "P07", &g_cfg.toc_ds,      1U,  50U,  1U, FMT_UINT    }, /* Over-current trip delay (0.1 s) */
    { "P08", &g_cfg.delay_min,   1U,  10U,  1U, FMT_MINUTES }, /* Restart delay (minutes) */
    { "P09", &g_cfg.ret_cnt,     1U,   5U,  1U, FMT_UINT    }, /* OC trips before LOC3 lockout */
};
#define N_PARAMS   (sizeof(k_params) / sizeof(k_params[0]))
#define ITEM_SAVE  N_PARAMS
#define ITEM_RST   (N_PARAMS + 1U)
#define ITEM_TOTAL (N_PARAMS + 2U)

/* Service background tasks (watchdog, VDD measurement, keypad, display) inside blocking UI loops */
/* While a menu waits for you, these background jobs must keep running: feed the watchdog, refresh the supply
 * measurement, scan buttons, update the display. Every waiting loop in this file calls ui_idle(). */
static void ui_idle(void)
{
    hal_wdg_kick();
    adc_service();
    input_poll();
    display_flush();
}

/* Safe terminal state after SAVE: relay forced OFF, LED OFF, displays "donE" until unplugged */
/* End of Settings/Calibration: relay OFF forever, display shows "donE". Unplug and re-plug power to continue. */
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

/* Render parameter value in Level 2 using its native unit format */
static void show_param_value(const Param_t *p)
{
    switch (p->fmt) {
        case FMT_MINUTES: display_mmss(*p->value * 60U); break;
        case FMT_AMPS:    display_labeled('A', *p->value); break;
        default:          display_uint(*p->value); break;
    }
}

/* Validate that OV - HYS is at least 5 V higher than UV + HYS so a valid start window exists */
/* Reject silly combinations: UV and OV limits must leave a usable start window between them. */
static bool window_ok(void)
{
    return (uint32_t)g_cfg.ov_v > ((uint32_t)g_cfg.uv_v + (2U * (uint32_t)g_cfg.hys_v) + 5U);
}

/* Show a temporary status message ("Err" or "donE") for `ms` milliseconds */
static void flash_message(const char *s, uint32_t ms)
{
    display_text(s);
    uint32_t t = g_ms;
    while ((uint32_t)(g_ms - t) < ms) ui_idle();
}

/* ---- Mode 2: 2-Level Settings Menu ---------------------------------------
 * Level 1: Browse P01..P09 <-> SAVE <-> rSt using '+' and '-' (clamped at ends).
 * Level 2: Press OK on P01..P09 to edit value with '+' and '-', press OK again to return. */
/* HOW TO USE THE SETTINGS MENU:  hold OK while powering on -> "Sett" -> press OK -> use + / - to pick P01..P09, SAVE, rSt.
 *   - On a parameter, press OK to edit its value with + / -, press OK again to go back.
 *   - On SAVE press OK to store everything (shows "donE").  On rSt press OK to restore factory values. */
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
            if (step < N_PARAMS)        display_text(k_params[step].code);
            else if (step == ITEM_SAVE) display_text("SAVE");
            else                        display_text("rSt");

            if (e == KEY_EVT_PLUS) {
                if ((step + 1U) < ITEM_TOTAL) step++;
            } else if (e == KEY_EVT_MINUS) {
                if (step > 0U) step--;
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

/* Calibration display helper: shows a steady number even though the live reading wobbles. */
/* ---- Mode 3: Sensor Calibration (Median-3 + Steady 2 Hz Readout) --------- */
typedef struct {
    uint32_t ema_q8;     /* Calibration EMA accumulator in Q8 fixed-point (alpha = 1/64) */
    uint16_t hist[3];    /* Last 3 raw 20 ms blocks for 3-point median spike rejection */
    uint16_t shown;      /* Last value displayed on TM1637 */
    uint32_t last_ms;    /* Timestamp of last 500 ms (2 Hz) display update */
    uint8_t  count;      /* Number of raw blocks collected (0..3) */
} CalView_t;

/* Return the middle value of 3 samples to reject isolated single-cycle (20 ms) EMI spikes */
/* "Median of 3" = sort three numbers and take the middle one. Throws away a single wild reading. */
static uint16_t median3(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b) { uint16_t t = a; a = b; b = t; }
    if (b > c) { uint16_t t = b; b = c; c = t; }
    if (a > b) { uint16_t t = a; a = b; b = t; }
    return b;
}

/* Reject single-block glitches with a 3-point median, smooth with alpha = 1/64 (~1.28 s tau),
 * and refresh the display at 2 Hz (500 ms) with a 4-LSB (0.4 V / 0.04 A) hysteresis deadband. */
/* Make the calibration number calm: median of 3, then slow smoothing, then update the screen only twice a second. */
static uint16_t cal_view_update(CalView_t *v, uint16_t sample)
{
    if (v->count < 3U) {
        v->hist[v->count++] = sample;
        uint16_t init_val = (v->count == 3U) ? median3(v->hist[0], v->hist[1], v->hist[2]) : sample;
        v->ema_q8  = (uint32_t)init_val << 8;
        v->shown   = init_val;
        v->last_ms = g_ms;
        return v->shown;
    }

    v->hist[0] = v->hist[1];
    v->hist[1] = v->hist[2];
    v->hist[2] = sample;

    uint16_t med  = median3(v->hist[0], v->hist[1], v->hist[2]);
    uint32_t x_q8 = (uint32_t)med << 8;
    if (x_q8 >= v->ema_q8) v->ema_q8 += (x_q8 - v->ema_q8 + 32U) >> 6;
    else                   v->ema_q8 -= (v->ema_q8 - x_q8 + 32U) >> 6;

    if ((uint32_t)(g_ms - v->last_ms) >= 500UL) {
        v->last_ms = g_ms;
        uint16_t avg  = (uint16_t)((v->ema_q8 + 128U) >> 8);
        uint16_t diff = (avg >= v->shown) ? (avg - v->shown) : (v->shown - avg);
        if (diff >= 4U) v->shown = avg;
    }
    return v->shown;
}

/* Proportionally rescale the filtered calibration state when '+' or '-' changes the gain trim,
 * giving instant visual feedback without resetting to a noisy raw 20 ms block. */
static void cal_view_rescale(CalView_t *v, uint16_t old_trim, uint16_t new_trim)
{
    if (v->count == 0U || old_trim == 0U || old_trim == new_trim) return;
    uint32_t half = (uint32_t)old_trim >> 1;
    v->ema_q8  = (uint32_t)(((uint64_t)v->ema_q8 * (uint64_t)new_trim + half) / (uint64_t)old_trim);
    v->hist[0] = (uint16_t)(((uint32_t)v->hist[0] * new_trim + half) / old_trim);
    v->hist[1] = (uint16_t)(((uint32_t)v->hist[1] * new_trim + half) / old_trim);
    v->hist[2] = (uint16_t)(((uint32_t)v->hist[2] * new_trim + half) / old_trim);
    v->shown   = (uint16_t)((v->ema_q8 + 128U) >> 8);
    v->last_ms = g_ms;
}

/* Display prompt string ("C-0" or "SAVE") and wait for a clean 'OK' press */
static void wait_ok(const char *label)
{
    display_text(label);
    input_wait_release();
    while (input_get_event() != KEY_EVT_OK) ui_idle();
}

/* Adjust Q12 calibration multiplier: fine step (2) on single tap, coarse step (24) when held */
/* The "trim" is a multiplier (4096 = 1.0000x). + / - nudge it so the displayed number matches a reference meter.
 * Tap = tiny step, hold = big step. */
static void adjust_trim(uint16_t *trim_q12, KeyEvent_t e)
{
    int32_t step = input_is_repeating() ? 24 : 2;
    int32_t nv   = (int32_t)*trim_q12 + ((e == KEY_EVT_PLUS) ? step : -step);
    if (nv < 512)   nv = 512;     /* Minimum 0.125x gain trim */
    if (nv > 16383) nv = 16383;   /* Maximum 4.000x gain trim */
    *trim_q12 = (uint16_t)nv;
}

/* HOW TO CALIBRATE (do this once per board, with a trusted multimeter):
 *   1) Short the CAL jumper (PC6 to GND) and power on.
 *   2) "C-0": disconnect mains and load, press OK. The chip measures its own zero/noise level.
 *   3) "C-V": connect mains. Press + / - until the display equals your multimeter (e.g. 2205 = 220.5 V). Press OK.
 *   4) "C-A": press OK to switch the relay on with your load, press + / - until it matches a clamp meter (A140 = 1.40 A). OK again.
 *      (Press + instead to skip current calibration.)
 *   5) "SAVE": press OK. Everything is stored in Flash. Remove the jumper and power-cycle. */
void mode_cal(void)
{
    hal_relay_set(false);
    alerts_set_led(LED_FAST);

    /* Step 1 ("C-0"): Measure zero-input ADC noise floor (0 V AC, 0 A).
     * Rejects capture with "Err" if live AC mains is still connected (noise > 300 Q6). */
    for (;;) {
        wait_ok("C-0");
        display_text("----");
        display_flush();
        uint16_t zv = adc_measure_noise_q6(ADC_CH_VAC);
        uint16_t za = 0U;
#if ENABLE_CT
        za = adc_measure_noise_q6(ADC_CH_CT);
#endif
        if (zv <= CAL_ZERO_MAX_Q6 && za <= CAL_ZERO_MAX_Q6) {
            g_cfg.zero_v_q6 = zv;
#if ENABLE_CT
            g_cfg.zero_a_q6 = za;
#endif
            break;
        }
        alerts_beep(BEEP_ALERT);
        flash_message("Err", 1500UL);
    }

    /* Step 2 ("C-V"): Plug in live AC mains and trim cal_v_q12 to match reference voltmeter (0.1 V) */
    display_text("C-V");
    input_wait_release();
    CalView_t cv      = { 0 };
    uint32_t  last_bv = g_sens.blocks;
    for (;;) {
        ui_idle();
        KeyEvent_t e = input_get_event();
        if (e == KEY_EVT_PLUS || e == KEY_EVT_MINUS) {
            uint16_t old_v = g_cfg.cal_v_q12;
            adjust_trim(&g_cfg.cal_v_q12, e);
            cal_view_rescale(&cv, old_v, g_cfg.cal_v_q12);
            if (cv.count > 0U) display_uint_pad4(cv.shown);
        }
        if (e == KEY_EVT_OK) break;
        if (g_sens.blocks != last_bv) {
            last_bv = g_sens.blocks;
            display_uint_pad4(cal_view_update(&cv, g_sens.vac_win_dv));
        }
    }

#if ENABLE_CT
    /* Step 3 ("C-A"): Press OK to close relay and trim current to clamp meter, or '+' to skip */
    fsm_refresh_limits();
    display_text("C-A");
    input_wait_release();
    bool      running = false;
    CalView_t ca      = { 0 };
    uint32_t  last_ba = g_sens.blocks;
    for (;;) {
        ui_idle();
        KeyEvent_t e = input_get_event();
        if (!running) {
            if (e == KEY_EVT_PLUS) break; /* Skip current calibration when no load/12V is attached */
            if (e == KEY_EVT_OK) {
                if (fsm_voltage_ok_to_start()) {
                    fsm_relay_on();
                    running = true;
                    ca      = (CalView_t){ 0 };
                    last_ba = g_sens.blocks;
                } else {
                    bool ov = g_sens.vac_raw_dv > ((g_cfg.ov_v - g_cfg.hys_v) * 10U);
                    flash_message(ov ? "E-OV" : "E-UV", 1000UL);
                }
            }
            display_text("C-A");
            continue;
        }
        /* Keep fast 20 ms OV/UV/OC protection active while testing live load in C-A */
        if (fsm_fault() != FAULT_NONE) {
            hal_relay_set(false);
            alerts_beep(BEEP_ALERT);
            flash_message(fsm_fault_text(fsm_fault()), 2000UL);
            running = false;
            display_text("C-A");
            input_wait_release();
            continue;
        }
        if (e == KEY_EVT_PLUS || e == KEY_EVT_MINUS) {
            uint16_t old_a = g_cfg.cal_a_q12;
            adjust_trim(&g_cfg.cal_a_q12, e);
            cal_view_rescale(&ca, old_a, g_cfg.cal_a_q12);
            if (ca.count > 0U) display_labeled('A', ca.shown);
        }
        if (e == KEY_EVT_OK) { hal_relay_set(false); break; }
        if (g_sens.blocks != last_ba) {
            last_ba = g_sens.blocks;
            display_labeled('A', cal_view_update(&ca, g_sens.i_win_ca));
        }
    }
#endif

    /* Step 4 ("SAVE"): Commit zero offsets and gain trims to Flash, beep, and halt on "donE" */
    wait_ok("SAVE");
    (void)nvm_save();
    alerts_beep(BEEP_CONFIRM);
    halt_done();
}