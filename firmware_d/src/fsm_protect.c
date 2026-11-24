/* [#08] fsm_protect.c - Mode 1: normal protection. */
#include "system.h"

typedef enum { ST_DELAY, ST_RUN, ST_TRIP, ST_LOCKOUT } ProtState_t;

static ProtState_t st;
static FaultCode_t trip_fault;
static volatile FaultCode_t fault_isr;
static volatile uint32_t run_ms, oc_acc_ms;

static uint16_t lim_ov_dv, lim_uv_dv, lim_oc_ca, lim_inrush_ca;
static uint16_t lim_start_lo_dv, lim_start_hi_dv;
static uint32_t lim_tin_ms, lim_toc_ms;

static uint16_t delay_left_s;
static uint32_t t_enter_ms, t_tick_ms, t_beep_ms, run_start_ms;

void fsm_refresh_limits(void)
{
    lim_ov_dv       = g_cfg.ov_v * 10U;
    lim_uv_dv       = g_cfg.uv_v * 10U;
    lim_start_lo_dv = (g_cfg.uv_v + g_cfg.hys_v) * 10U;
    lim_start_hi_dv = (g_cfg.ov_v - g_cfg.hys_v) * 10U;
    lim_oc_ca       = g_cfg.oc_ca;
    lim_inrush_ca   = (uint16_t)(((uint32_t)g_cfg.oc_ca * g_cfg.inr_x10) / 10UL);
    lim_tin_ms      = (uint32_t)g_cfg.tin_ds * 100UL;
    lim_toc_ms      = (uint32_t)g_cfg.toc_ds * 100UL;
}

const char *fsm_fault_text(FaultCode_t f)
{
    switch (f) {
        case FAULT_UV: return "E-UV";
        case FAULT_OV: return "E-OV";
        case FAULT_OC: return "E-OC";
        case FAULT_OT: return "E-Ot";
        default:       return "E---";
    }
}

static void isr_trip(FaultCode_t f)
{
    hal_relay_set(false);
    if (fault_isr == FAULT_NONE) fault_isr = f;
}

void fsm_on_block(const SensorData_t *s)
{
    if (!hal_relay_is_on()) return;

    uint16_t v = s->vac_raw_dv;
    if (v < lim_uv_dv) { isr_trip(FAULT_UV); return; }
    if (v > lim_ov_dv) { isr_trip(FAULT_OV); return; }

#if ENABLE_CT
    run_ms += BLOCK_MS;
    uint16_t i = s->i_raw_ca;
    if (run_ms <= lim_tin_ms) {
        if (i > lim_inrush_ca) isr_trip(FAULT_OC);
        oc_acc_ms = 0;
    } else if (i > lim_oc_ca) {
        oc_acc_ms += BLOCK_MS;
        if (oc_acc_ms >= lim_toc_ms) isr_trip(FAULT_OC);
    } else {
        oc_acc_ms = 0;
    }
#endif
}

void fsm_relay_on(void)
{
    run_ms    = 0;
    oc_acc_ms = 0;
    fault_isr = FAULT_NONE;
    hal_relay_set(true);
}

FaultCode_t fsm_fault(void) { return fault_isr; }

bool fsm_voltage_ok_to_start(void)
{
    uint16_t v = g_sens.vac_raw_dv;
    return g_sens.blocks > 0UL && v >= lim_start_lo_dv && v <= lim_start_hi_dv;
}

static void enter_delay(void)
{
    hal_relay_set(false);
    alerts_set_led(LED_SLOW);
    delay_left_s = g_cfg.delay_min * 60U;
    t_tick_ms    = g_ms;
    st           = ST_DELAY;
}

static void enter_run(void)
{
    fsm_relay_on();
    run_start_ms = g_ms;
    alerts_set_led(LED_ON);
    alerts_beep(BEEP_CONFIRM);
    st = ST_RUN;
}

#if ENABLE_CT
static void enter_lockout(void)
{
    hal_relay_set(false);
    alerts_set_led(LED_DOUBLE);
    alerts_beep(BEEP_LOCK);
    t_beep_ms = g_ms;
    display_text("LOC3");
    st = ST_LOCKOUT;
}
#endif

static void enter_trip(FaultCode_t f)
{
    hal_relay_set(false);
    trip_fault = f;
    fault_isr  = FAULT_NONE;
    alerts_set_led(LED_FAST);
    alerts_beep(BEEP_ALERT);
    t_enter_ms = g_ms;
    st         = ST_TRIP;
    display_text(fsm_fault_text(f));
    if (f == FAULT_OC) {
        g_cfg.oc_fail++;
        (void)nvm_save();
    }
}

void fsm_init(void)
{
    fsm_refresh_limits();
    fault_isr = FAULT_NONE;
#if ENABLE_CT
    if (g_cfg.oc_fail >= g_cfg.ret_cnt) { enter_lockout(); return; }
#endif
    enter_delay();
}

void fsm_step(void)
{
    KeyEvent_t ev = input_get_event();

    switch (st) {
    case ST_DELAY: {
        if (g_sens.blocks == 0UL) { display_text("----"); break; }
        uint16_t v = g_sens.vac_raw_dv;
        bool low = v < lim_start_lo_dv, high = v > lim_start_hi_dv;
        if (low || high) {
            delay_left_s = g_cfg.delay_min * 60U;
            t_tick_ms    = g_ms;
        } else if ((uint32_t)(g_ms - t_tick_ms) >= 1000UL) {
            t_tick_ms += 1000UL;
            if (delay_left_s) delay_left_s--;
        }
        if (!low && !high && delay_left_s == 0U) { enter_run(); break; }
        if ((low || high) && ((g_ms / 1000UL) % 2UL)) display_text(low ? "E-UV" : "E-OV");
        else                                          display_mmss(delay_left_s);
        break;
    }
    case ST_RUN: {
        if (fault_isr != FAULT_NONE) { enter_trip(fault_isr); break; }
#if ENABLE_CT
        if (g_cfg.oc_fail && (uint32_t)(g_ms - run_start_ms) >= HEALTHY_CLEAR_MS) nvm_clear_fail();
        if (((g_ms - run_start_ms) / 2000UL) % 2UL) display_labeled('A', g_sens.i_disp_ca);
        else
#endif
        display_uint((g_sens.vac_disp_dv + 5U) / 10U);
        break;
    }
    case ST_TRIP:
        if ((uint32_t)(g_ms - t_enter_ms) >= TRIP_SHOW_MS) {
#if ENABLE_CT
            if (g_cfg.oc_fail >= g_cfg.ret_cnt) { enter_lockout(); break; }
#endif
            enter_delay();
        }
        break;
    case ST_LOCKOUT:
        if ((uint32_t)(g_ms - t_beep_ms) >= LOCKOUT_BEEP_MS) {
            alerts_beep(BEEP_LOCK);
            t_beep_ms = g_ms;
        }
        if (ev == KEY_EVT_OK_LONG) {
            nvm_clear_fail();
            enter_delay();
        }
        break;
    }
}