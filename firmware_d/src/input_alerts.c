/* [#07] input_alerts.c - 3-key resistor-ladder scanner (PD6 via ADC), CAL pin (PC6),
 * non-blocking LED (PD5) and active buzzer (PC0) sequencer. */
#include "system.h"

#define KEY_ADC_PLUS_MIN 25U   /* Reject multi-button mash / shorted ladder (< 25 counts) */

typedef enum { K_NONE, K_MINUS, K_MENU, K_PLUS } KeyCode_t;

static KeyEvent_t pending = KEY_EVT_NONE;
static KeyCode_t  stable = K_NONE, candidate = K_NONE;
static uint8_t    same_count = 0;
static uint32_t   last_scan_ms = 0, press_ms = 0, next_repeat_ms = 0;
static bool       long_fired = false, repeating = false;

static KeyCode_t decode_key(uint16_t adc)
{
    if (adc >= KEY_ADC_NONE_MIN)  return K_NONE;
    if (adc >= KEY_ADC_MINUS_MIN) return K_MINUS;
    if (adc >= KEY_ADC_MENU_MIN)  return K_MENU;
    if (adc >= KEY_ADC_PLUS_MIN)  return K_PLUS;
    return K_NONE;
}

static void on_key_change(KeyCode_t now)
{
    stable    = now;
    repeating = false;

    if (now == K_MENU) {
        pending    = KEY_EVT_OK;
        press_ms   = g_ms;
        long_fired = false;
    } else if (now == K_MINUS) {
        pending        = KEY_EVT_MINUS;
        next_repeat_ms = g_ms + KEY_REPEAT_DELAY_MS;
    } else if (now == K_PLUS) {
        pending        = KEY_EVT_PLUS;
        next_repeat_ms = g_ms + KEY_REPEAT_DELAY_MS;
    }
}

void input_init(void)
{
    pending = KEY_EVT_NONE;
    stable = candidate = K_NONE;
}

void input_poll(void)
{
    if ((uint32_t)(g_ms - last_scan_ms) < KEY_SCAN_MS) return;
    last_scan_ms = g_ms;

    KeyCode_t raw = decode_key(adc_read_injected(ADC_CH_KEY));
    if (raw != candidate) {
        candidate  = raw;
        same_count = 0;
    } else if (same_count < 255U) {
        same_count++;
    }

    if (same_count == KEY_DEBOUNCE_SCANS && candidate != stable) {
        on_key_change(candidate);
    }

    if (stable == K_MENU && !long_fired && (uint32_t)(g_ms - press_ms) >= KEY_LONG_MS) {
        pending    = KEY_EVT_OK_LONG;
        long_fired = true;
    }
    if ((stable == K_MINUS || stable == K_PLUS) && (int32_t)(g_ms - next_repeat_ms) >= 0) {
        pending        = (stable == K_MINUS) ? KEY_EVT_MINUS : KEY_EVT_PLUS;
        next_repeat_ms = g_ms + KEY_REPEAT_MS;
        repeating      = true;
    }
}

KeyEvent_t input_get_event(void)
{
    KeyEvent_t e = pending;
    pending = KEY_EVT_NONE;
    return e;
}

bool input_is_repeating(void) { return repeating; }

void input_wait_release(void)
{
    do {
        hal_wdg_kick();
        input_poll();
    } while (stable != K_NONE || decode_key(adc_read_injected(ADC_CH_KEY)) != K_NONE);
    pending = KEY_EVT_NONE;
}

bool input_menu_held_at_boot(void)
{
    for (uint8_t i = 0; i < 5U; i++) {
        Delay_Ms(10);
        hal_wdg_kick();
        if (decode_key(adc_read_injected(ADC_CH_KEY)) != K_MENU) return false;
    }
    return true;
}

bool input_cal_active(void)
{
    for (uint8_t i = 0; i < 5U; i++) {
        Delay_Ms(2);
        if (funDigitalRead(PIN_CAL)) return false;
    }
    return true;
}

/* ---- LED + buzzer (1 ms tick from SysTick) ------------------------------- */
static volatile LedMode_t led_mode = LED_OFF;
static volatile struct {
    uint8_t  count;
    uint16_t on_ms, off_ms, timer;
    bool     on;
} beep;
static bool led_last = false;

void alerts_set_led(LedMode_t m) { led_mode = m; }

void alerts_beep(BeepPattern_t p)
{
    uint8_t n = 0; uint16_t on = 0, off = 0;
    switch (p) {
        case BEEP_CONFIRM: n = 1; on = 150; off = 0;   break;
        case BEEP_ALERT:   n = 3; on = 100; off = 100; break;
        case BEEP_LOCK:    n = 2; on = 400; off = 200; break;
        default: break;
    }
    beep.count = 0;
    beep.on_ms = on;  beep.off_ms = off;
    beep.on = false;  beep.timer = 1;
    beep.count = n;
}

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

    bool on = false;
    uint32_t t = g_ms;
    switch (led_mode) {
        case LED_ON:     on = true;                 break;
        case LED_SLOW:   on = (t % 1000UL) < 500UL; break;
        case LED_FAST:   on = (t % 250UL) < 125UL;  break;
        case LED_DOUBLE: {
            uint32_t p = t % 1000UL;
            on = (p < 100UL) || (p >= 250UL && p < 350UL);
        } break;
        default: break;
    }
    if (on != led_last) {
        led_last = on;
        funDigitalWrite(PIN_LED, on ? FUN_HIGH : FUN_LOW);
    }
}