/* ============================ BEGINNER GUIDE: fsm_protect.c ============================
 * WHAT THIS FILE IS: the BRAIN. It decides when the relay may be ON or must be OFF.
 * "FSM" = Finite State Machine: a fancy name for "the program is always in exactly ONE situation (state),
 * and specific events move it to another state". See the flowchart in HOW_IT_WORKS.html.
 *
 * TWO PARTS:
 *   fsm_on_block() - FAST part, runs inside the 20 ms interrupt: instantly cuts the relay when unsafe.
 *   fsm_step()     - SLOW part, runs in the main loop: countdown, display text, beeps, LED, state changes.
 * ======================================================================================== */

/* [#08] fsm_protect.c - Mode 1 Compressor Protection Finite State Machine.
 * States:
 *   ST_DELAY   -> Anti-short-cycle startup countdown (relay OFF, checks UV+HYS..OV-HYS)
 *   ST_RUN     -> Normal operation (relay ON, alternates Voltage & Amps every 2 s)
 *   ST_TRIP    -> 3-second fault display ("E-UV", "E-OV", "E-OC") after relay cuts off
 *   ST_LOCKOUT -> Permanent lockout ("LOC3") after P09 consecutive OC trips (hold OK 3 s to clear) */
#include "system.h"

/* The 4 "states" (situations) the protector can be in. Exactly one is active at any time.
 *   ST_DELAY   : relay OFF, counting down the safety wait (e.g. 5:00). Starts at power-on and after every trip.
 *   ST_RUN     : relay ON, compressor running, display shows volts / amps.
 *   ST_TRIP    : a fault just happened, relay OFF, fault code shown for 3 s.
 *   ST_LOCKOUT : too many over-current trips -> "LOC3", stays OFF until the user holds OK for 3 s.
 *
 * Transitions:   power-on -> DELAY -> (countdown done & voltage OK) -> RUN -> (fault) -> TRIP -> DELAY ...
 *                                                                            (too many OC trips) -> LOCKOUT */
typedef enum { ST_DELAY, ST_RUN, ST_TRIP, ST_LOCKOUT } ProtState_t;

/* "static" = private to this file. "volatile" = may be changed by an interrupt at any moment. */
static ProtState_t st;
static FaultCode_t trip_fault;
/* The 20 ms interrupt writes the fault here; the main loop later reads it and shows the code on the display. */
static volatile FaultCode_t fault_isr;          /* Fault flag set inside 20 ms DMA interrupt */
static volatile uint32_t run_ms, oc_acc_ms;     /* Relay-on elapsed time & sustained OC timer */
static volatile uint8_t  uv_cnt, ov_cnt;        /* Consecutive-block counters for UV/OV debounce */

/* The user types "245" volts, but the measurement is in tenths of a volt. We convert the limits ONCE
 * (fsm_refresh_limits) so the interrupt only has to compare two whole numbers - fast and simple. */
/* Pre-scaled thresholds in 0.1 V (dV), 0.01 A (cA), and ms for fast integer ISR comparison */
static uint16_t lim_ov_dv, lim_uv_dv, lim_oc_ca, lim_inrush_ca;
static uint16_t lim_start_lo_dv, lim_start_hi_dv;
static uint32_t lim_tin_ms, lim_toc_ms;

static uint16_t delay_left_s;
static uint32_t t_enter_ms, t_tick_ms, t_beep_ms, run_start_ms, t_disp_ms;
static bool show_amps;   /* RUN display toggles between voltage and current every 2 s */

/* Convert user settings (P01..P07) into pre-scaled integer thresholds */
/* Hysteresis idea: to START we demand a slightly safer voltage (UV+10 V ... OV-10 V) than the voltage that
 * would TRIP us (UV ... OV). That gap prevents the relay from chattering on/off when the voltage hovers at the limit. */
void fsm_refresh_limits(void)
{
    lim_ov_dv       = g_cfg.ov_v * 10U;
    lim_uv_dv       = g_cfg.uv_v * 10U;
    lim_start_lo_dv = (g_cfg.uv_v + g_cfg.hys_v) * 10U; /* e.g. 180 + 10 = 190.0 V */
    lim_start_hi_dv = (g_cfg.ov_v - g_cfg.hys_v) * 10U; /* e.g. 245 - 10 = 235.0 V */
    lim_oc_ca       = g_cfg.oc_ca;
    lim_inrush_ca   = (uint16_t)(((uint32_t)g_cfg.oc_ca * g_cfg.inr_x10) / 10UL);
    lim_tin_ms      = (uint32_t)g_cfg.tin_ds * 100UL;
    lim_toc_ms      = (uint32_t)g_cfg.toc_ds * 100UL;
}

/* Return 4-character display string corresponding to a FaultCode_t */
const char *fsm_fault_text(FaultCode_t f)
{
    switch (f) {
        case FAULT_UV: return "E-UV";
        case FAULT_OV: return "E-OV";
        case FAULT_OC: return "E-OC";
        case FAULT_OT: return "E-Ot";
        case FAULT_SENSOR: return "E-SE";
        default:       return "E---";
    }
}

/* Immediately de-energize relay inside the 20 ms DMA interrupt and latch first fault code */
/* EMERGENCY STOP, called from inside the 20 ms interrupt: relay off right now, remember the FIRST reason. */
static void isr_trip(FaultCode_t f)
{
    hal_relay_set(false);
    if (fault_isr == FAULT_NONE) fault_isr = f;
}

/* FAST PROTECTION PATH: Called inside DMA1_Channel1_IRQHandler every 20 ms (1 AC cycle).
 * Uses unfiltered vac_raw_dv and i_raw_ca so display smoothing never delays fault cutoff. */
/* The SAFETY CHECK. Runs every 20 ms inside the interrupt, ONLY while the relay is ON. Three checks:
 *   1) Voltage too low or too high for 3 waves in a row (60 ms)  -> trip
 *   2) Current too high during the first moments after switch-on  -> trip (loose "inrush" limit, e.g. 3.5 A)
 *   3) Current too high for longer than the delay time after that -> trip (normal limit, e.g. 1.4 A for 0.5 s)
 * "Inrush" = a motor draws a big surge for a moment when it starts; that is normal, so the limit is higher then. */
void fsm_on_block(const SensorData_t *s)
{
    if (!hal_relay_is_on()) return;

    /* We use a small counter so ONE noisy wave cannot cut the compressor, but a REAL problem trips within 60 ms. */
    /* 1. Instant 20 ms Under-Voltage / Over-Voltage trip */
    uint16_t v = s->vac_raw_dv;
    /* Debounced: needs TRIP_V_BLOCKS consecutive bad cycles, so one EMI-corrupted block cannot cut the compressor */
    if (v < lim_uv_dv) { if (++uv_cnt >= TRIP_V_BLOCKS) { isr_trip(FAULT_UV); return; } } else uv_cnt = 0U;
    if (v > lim_ov_dv) { if (++ov_cnt >= TRIP_V_BLOCKS) { isr_trip(FAULT_OV); return; } } else ov_cnt = 0U;

#if ENABLE_CT
    /* 2. Compressor Current Protection:
     *    - During startup inrush window (run_ms <= P06): trip immediately (20 ms) if I > P04 * P05
     *    - After startup window (run_ms > P06): trip if I > P04 continuously for P07 seconds */
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

/* Reset timers/faults and close compressor relay */
/* Clear all timers and counters, then switch the relay ON. */
void fsm_relay_on(void)
{
    run_ms    = 0;
    oc_acc_ms = 0;
    uv_cnt    = 0U;
    ov_cnt    = 0U;
    fault_isr = FAULT_NONE;
    hal_relay_set(true);
}

FaultCode_t fsm_fault(void) { return fault_isr; }

/* Check if AC mains voltage is inside the safe hysteresis start window (UV+HYS .. OV-HYS) */
/* "Is it safe to switch on right now?" = measuring works AND voltage is inside the safe start window. */
bool fsm_voltage_ok_to_start(void)
{
    uint16_t v = g_sens.vac_raw_dv;
    return g_sens.blocks > 0UL && hal_dsp_alive() && v >= lim_start_lo_dv && v <= lim_start_hi_dv;
}

/* Transition into ST_DELAY: relay OFF, 1 Hz slow LED, start P08 minute countdown */
/* The enter_xxx() functions are the "doors" between states: each one sets up everything the new state needs
 * (relay, LED pattern, buzzer, timers) so the state itself can stay simple. */
static void enter_delay(void)
{
    hal_relay_set(false);
    alerts_set_led(LED_SLOW);
    delay_left_s = g_cfg.delay_min * 60U;
    t_tick_ms    = g_ms;
    st           = ST_DELAY;
}

/* Transition into ST_RUN: energize relay, solid LED, 1 confirmation beep */
static void enter_run(void)
{
    fsm_relay_on();
    run_start_ms = g_ms;
    t_disp_ms    = g_ms;
    show_amps    = false;
    alerts_set_led(LED_ON);
    alerts_beep(BEEP_CONFIRM);
    st = ST_RUN;
}

#if ENABLE_CT
/* Transition into ST_LOCKOUT ("LOC3"): relay locked OFF until user holds OK for 3 s */
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

/* Transition into ST_TRIP: show fault text for 3 s, sound 3 beeps, increment OC fail counter */
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
        (void)nvm_save();   /* Save trip count in Flash so power-cycling cannot cheat LOC3 */
    }
}

/* Initialize state machine on boot (restores LOC3 immediately if oc_fail >= P09) */
/* Called once from main(). If the saved trip counter already reached the limit, power-cycling does NOT help:
 * we go straight back to LOCKOUT. Otherwise start with the safety countdown. */
void fsm_init(void)
{
    fsm_refresh_limits();
    fault_isr = FAULT_NONE;
#if ENABLE_CT
    if (g_cfg.oc_fail >= g_cfg.ret_cnt) { enter_lockout(); return; }
#endif
    enter_delay();
}

/* Main-loop state machine step (runs continuously in superloop) */
/* The main loop calls this thousands of times per second. Each call does a tiny bit of work for the CURRENT state
 * and then returns quickly. It never waits (no delay loops) so the buttons and display stay responsive. */
void fsm_step(void)
{
    KeyEvent_t ev = input_get_event();

    switch (st) {
    /* ---- STATE: DELAY (relay OFF, waiting) ----
     * The countdown only runs while the mains voltage is inside the safe window. If it leaves the window,
     * the countdown restarts from the full time. When it reaches 0:00 the relay switches ON. */
    case ST_DELAY: {
        if (g_sens.blocks == 0UL) { display_text("----"); break; }
        if (!hal_dsp_alive()) {                 /* Sensing dead: never count down, never start */
            delay_left_s = g_cfg.delay_min * 60U;
            t_tick_ms    = g_ms;
            display_text("E-SE");
            break;
        }
        uint16_t v = g_sens.vac_raw_dv;
        bool low = v < lim_start_lo_dv, high = v > lim_start_hi_dv;

        /* If mains is outside safe window, hold countdown at full P08 minutes */
        if (low || high) {
            delay_left_s = g_cfg.delay_min * 60U;
            t_tick_ms    = g_ms;
        } else if ((uint32_t)(g_ms - t_tick_ms) >= 1000UL) {
            t_tick_ms += 1000UL;
            if (delay_left_s) delay_left_s--;
        }

        if (!low && !high && delay_left_s == 0U) { enter_run(); break; }

        /* Alternate "E-UV"/"E-OV" and "MM:SS" every 1 s during abnormal voltage; otherwise show "MM:SS" */
        if ((low || high) && ((g_ms >> 10) & 1UL)) display_text(low ? "E-UV" : "E-OV");
        else                                          display_mmss(delay_left_s);
        break;
    }
    /* ---- STATE: RUN (relay ON) ----
     * The real protection happens in the 20 ms interrupt (fsm_on_block). Here we only react to its result,
     * keep the display updated, and forgive old over-current trips after 3 minutes of healthy running. */
    case ST_RUN: {
        if (fault_isr != FAULT_NONE) { enter_trip(fault_isr); break; }
        if (!hal_dsp_alive()) { enter_trip(FAULT_SENSOR); break; }   /* Relay OFF if sensing stalls */
#if ENABLE_CT
        /* Clear previous OC trip history after 3 minutes of continuous healthy operation */
        if (g_cfg.oc_fail && (uint32_t)(g_ms - run_start_ms) >= HEALTHY_CLEAR_MS) nvm_clear_fail();

        /* Alternate display every 2 seconds between Current ("A140") and Voltage (" 220") */
        if ((uint32_t)(g_ms - t_disp_ms) >= 2000UL) { t_disp_ms += 2000UL; show_amps = !show_amps; }
        if (show_amps) {
            display_labeled('A', g_sens.i_disp_ca);
            break;
        }
#endif
        display_uint((g_sens.vac_disp_dv + 5U) / 10U); /* Round 0.1 V to nearest whole Volt */
        break;
    }
    /* ---- STATE: TRIP (showing the fault code) ---- after 3 s go to LOCKOUT or back to the DELAY countdown. */
    case ST_TRIP:
        if ((uint32_t)(g_ms - t_enter_ms) >= TRIP_SHOW_MS) {
#if ENABLE_CT
            if (g_cfg.oc_fail >= g_cfg.ret_cnt) { enter_lockout(); break; }
#endif
            enter_delay();
        }
        break;
    /* ---- STATE: LOCKOUT ("LOC3") ---- relay stays OFF, buzzer reminds every 30 s, hold OK for 3 s to unlock. */
    case ST_LOCKOUT:
        if ((uint32_t)(g_ms - t_beep_ms) >= LOCKOUT_BEEP_MS) {
            alerts_beep(BEEP_LOCK);
            t_beep_ms = g_ms;
        }
        /* Manual reset: hold 'OK' for 3.0 seconds to clear LOC3 and enter startup delay */
        if (ev == KEY_EVT_OK_LONG) {
            nvm_clear_fail();
            enter_delay();
        }
        break;
    }
}