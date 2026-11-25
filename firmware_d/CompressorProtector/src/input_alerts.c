/* [#07] input_alerts.c - 3-key resistor-ladder scanner (PD6), CAL jumper reader (PC6),
 * and non-blocking SysTick-driven LED (PD5) & Buzzer (PC0) patterns. */
#include "system.h"

typedef enum { K_NONE, K_MINUS, K_MENU, K_PLUS } KeyCode_t;

static KeyEvent_t pending = KEY_EVT_NONE;            /* Single-slot event queue consumed by UI */
static KeyCode_t  stable = K_NONE, candidate = K_NONE; /* Debounced key state & raw candidate */
static uint8_t    same_count = 0;                    /* Consecutive identical scans counter */
static uint32_t   last_scan_ms = 0, press_ms = 0, next_repeat_ms = 0;
static bool       long_fired = false, repeating = false;

/* Read 8 injected ADC samples on PD6 and compute a trimmed mean (dropping min & max)
 * so isolated 50 Hz AC or display-switching glitches cannot corrupt keypad detection. */
static uint16_t read_key_adc(void)
{
    uint32_t sum  = 0;
    uint16_t vmin = 1023U;
    uint16_t vmax = 0U;
    for (uint8_t i = 0; i < 8U; i++) {
        uint16_t s = adc_read_injected(ADC_CH_KEY);
        sum += s;
        if (s < vmin) vmin = s;
        if (s > vmax) vmax = s;
    }
    return (uint16_t)((sum - vmin - vmax + 3U) / 6U);
}

/* Convert 10-bit ADC reading on PD6 (10k pull-up) into a single key code:
 * No key >= 720, '-' (4.7k) = 255..719, 'OK' (2.2k) = 115..254, '+' (0..1k) = 0..114. */
static KeyCode_t decode_key(uint16_t adc)
{
    if (adc >= KEY_ADC_NONE_MIN)  return K_NONE;
    if (adc >= KEY_ADC_MINUS_MIN) return K_MINUS;
    if (adc >= KEY_ADC_MENU_MIN)  return K_MENU;
    return K_PLUS;
}

/* Handle a debounced transition into a new key state */
static void on_key_change(KeyCode_t now)
{
    stable    = now;
    repeating = false;

    if (now == K_MENU) {
        pending    = KEY_EVT_OK;                       /* Fire 'OK' immediately on press */
        press_ms   = g_ms;                             /* Start 3.0 s long-press timer */
        long_fired = false;
    } else if (now == K_MINUS) {
        pending        = KEY_EVT_MINUS;
        next_repeat_ms = g_ms + KEY_REPEAT_DELAY_MS;   /* Schedule auto-repeat after 400 ms */
    } else if (now == K_PLUS) {
        pending        = KEY_EVT_PLUS;
        next_repeat_ms = g_ms + KEY_REPEAT_DELAY_MS;   /* Schedule auto-repeat after 400 ms */
    }
}

void input_init(void)
{
    pending = KEY_EVT_NONE;
    stable = candidate = K_NONE;
    same_count = 0U;
}

/* Poll keypad every 5 ms: handles 10 ms debouncing, 3 s long-press, and 12.5 Hz auto-repeat */
void input_poll(void)
{
    if ((uint32_t)(g_ms - last_scan_ms) < KEY_SCAN_MS) return;
    last_scan_ms = g_ms;

    KeyCode_t raw = decode_key(read_key_adc());
    if (raw == stable) {
        candidate  = stable;
        same_count = 0U;
    } else if (raw == candidate) {
        if (same_count < KEY_DEBOUNCE_SCANS) {
            same_count++;
            if (same_count == KEY_DEBOUNCE_SCANS) {
                on_key_change(candidate);
            }
        }
    } else {
        candidate  = raw;
        same_count = 1U;
    }

    /* Fire KEY_EVT_OK_LONG once after 'OK' is held continuously for 3.0 seconds */
    if (stable == K_MENU && !long_fired && (uint32_t)(g_ms - press_ms) >= KEY_LONG_MS) {
        pending    = KEY_EVT_OK_LONG;
        long_fired = true;
    }

    /* Fire repeated +/- events every 80 ms while '-' or '+' is held down */
    if ((stable == K_MINUS || stable == K_PLUS) && (int32_t)(g_ms - next_repeat_ms) >= 0) {
        pending        = (stable == K_MINUS) ? KEY_EVT_MINUS : KEY_EVT_PLUS;
        next_repeat_ms = g_ms + KEY_REPEAT_MS;
        repeating      = true;
    }
}

/* Read and clear the latest button event */
KeyEvent_t input_get_event(void)
{
    KeyEvent_t e = pending;
    pending = KEY_EVT_NONE;
    return e;
}

/* Returns true when '-' or '+' is currently auto-repeating (used to accelerate CAL trim steps) */
bool input_is_repeating(void) { return repeating; }

/* Block until all buttons are released (with 1.5 s EMI safety bound) so held keys do not leak */
void input_wait_release(void)
{
    uint32_t t0 = g_ms;
    while ((uint32_t)(g_ms - t0) < 1500UL) {
        hal_wdg_kick();
        input_poll();
        if (stable == K_NONE && decode_key(read_key_adc()) == K_NONE) break;
    }
    pending = KEY_EVT_NONE;
}

/* Check at power-up if 'OK' (Menu) is held down for 50 ms to enter Mode 2 Settings ("Sett") */
bool input_menu_held_at_boot(void)
{
    for (uint8_t i = 0; i < 5U; i++) {
        Delay_Ms(10);
        hal_wdg_kick();
        if (decode_key(read_key_adc()) != K_MENU) return false;
    }
    return true;
}

/* Check at power-up if CAL pin (PC6 / H1 pin 3) is shorted to GND for 10 ms to enter Mode 3 CAL */
bool input_cal_active(void)
{
    for (uint8_t i = 0; i < 5U; i++) {
        Delay_Ms(2);
        if (funDigitalRead(PIN_CAL)) return false;
    }
    return true;
}

/* ---- Non-Blocking Status LED & Active Buzzer Sequencer (1 ms SysTick) --- */
static volatile LedMode_t led_mode = LED_OFF;
static volatile struct {
    uint8_t  count;              /* Remaining beep pulses to play */
    uint16_t on_ms, off_ms, timer;
    bool     on;
} beep;
static bool led_last = false;
static uint16_t ph1000 = 0, ph250 = 0;   /* LED phase counters: no software division inside the 1 ms ISR */

void alerts_set_led(LedMode_t m) { led_mode = m; }

/* Queue a buzzer alert pattern without blocking CPU execution */
void alerts_beep(BeepPattern_t p)
{
    uint8_t n = 0; uint16_t on = 0, off = 0;
    switch (p) {
        case BEEP_CONFIRM: n = 1; on = 150; off = 0;   break; /* 1 x 150 ms beep */
        case BEEP_ALERT:   n = 3; on = 100; off = 100; break; /* 3 x 100 ms rapid beeps */
        case BEEP_LOCK:    n = 2; on = 400; off = 200; break; /* 2 x 400 ms alarm beeps */
        default: break;
    }
    beep.count = 0;
    beep.on_ms = on;  beep.off_ms = off;
    beep.on = false;  beep.timer = 1;
    beep.count = n;
}

/* Called every 1 ms from SysTick_Handler to step buzzer pulses and blink status LED */
void alerts_tick_1ms(void)
{
    if (beep.count) {
        if (beep.timer) beep.timer--;
        if (beep.timer == 0U) {
            if (beep.on) {
                funDigitalWrite(PIN_BUZZER, FUN_LOW);
                beep.on = false;
                beep.count--;
                beep.timer = beep.off_ms ? beep.off_ms : 1U;
            } else {
                funDigitalWrite(PIN_BUZZER, FUN_HIGH);
                beep.on = true;
                beep.timer = beep.on_ms;
            }
        }
    }

    if (++ph1000 >= 1000U) ph1000 = 0U;
    if (++ph250  >= 250U)  ph250  = 0U;

    bool on = false;
    switch (led_mode) {
        case LED_ON:     on = true;                                     break; /* Solid ON */
        case LED_SLOW:   on = ph1000 < 500U;                            break; /* 1 Hz */
        case LED_FAST:   on = ph250 < 125U;                             break; /* 4 Hz */
        case LED_DOUBLE: on = (ph1000 < 100U) ||
                              (ph1000 >= 250U && ph1000 < 350U);        break; /* Two flashes per second */
        default: break;
    }
    if (on != led_last) {
        led_last = on;
        funDigitalWrite(PIN_LED, on ? FUN_HIGH : FUN_LOW);
    }
}