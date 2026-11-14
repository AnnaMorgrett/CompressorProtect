#include "ui.h"
#include "tm1657.h"
#include "params.h"
#include "sensors.h"
#include <stddef.h>

static const uint8_t MSG_SCP[]  = {LTR_S, LTR_C, LTR_P, BLANK};
static const uint8_t MSG_PRO[]  = {LTR_P, LTR_r, LTR_O, BLANK};
static const uint8_t MSG_SETT[] = {LTR_S, LTR_E, LTR_t, LTR_t};
static const uint8_t MSG_SAVE[] = {LTR_S, LTR_A, LTR_U, LTR_E};
static const uint8_t MSG_OK[]   = {LTR_O, LTR_K, BLANK, BLANK};
static const uint8_t MSG_RST[]  = {LTR_r, LTR_S, LTR_t, BLANK};
static const uint8_t MSG_CAL[]  = {LTR_C, LTR_A, LTR_L, BLANK};
static const uint8_t MSG_URT[]  = {LTR_U, SEG_G, LTR_r, LTR_t};
static const uint8_t MSG_ART[]  = {LTR_A, SEG_G, LTR_r, LTR_t};
static const uint8_t MSG_TRT[]  = {LTR_t, SEG_G, LTR_r, LTR_t};
static const uint8_t MSG_C1[]   = {LTR_C, SEG_G, BLANK, 0x06};
static const uint8_t MSG_C2[]   = {LTR_C, SEG_G, BLANK, 0x5B};
static const uint8_t MSG_C3[]   = {LTR_C, SEG_G, BLANK, 0x4F};
static const uint8_t MSG_PC[]   = {BLANK, LTR_P, LTR_C, BLANK};

const param_def_t menu_table[20] = {
    {offsetof(system_params_t, time_delay_mins),           1,   2,   10,   1,  1, false},
    {offsetof(system_params_t, ovp_limit_div2),            1, 230,  260,  10,  2, false},
    {offsetof(system_params_t, uvp_limit),                 1, 150,  200,  10,  1, false},
    {offsetof(system_params_t, ovp_uvp_delay),             1,   3,   10,   1,  1, false},
    {offsetof(system_params_t, voltage_hysteresis),        1,  10,   40,   1,  1, false},
    {offsetof(system_params_t, lra_limit_ma),              2, 800, 5000, 100,  1, false},
    {offsetof(system_params_t, lra_time),                  1,   1,    5,   1,  1, false},
    {offsetof(system_params_t, lra_retry_limit),           1,   2,    5,   1,  1, false},
    {offsetof(system_params_t, ocp_limit_ma),              2, 300, 4500,  50,  1, false},
    {offsetof(system_params_t, ocp_delay),                 1,   2,   10,   1,  1, false},
    {offsetof(system_params_t, ucp_limit_x10),             1, 200, 1000,  50, 10, true },
    {offsetof(system_params_t, min_current_threshold_x10), 1, 150,  300,  50, 10, true },
    {0,                                                    0,   0,    0,   1,  1, false},
    {offsetof(system_params_t, otp_limit),                 1,  70,   90,   1,  1, false},
    {offsetof(system_params_t, otp_hysteresis),            1,   5,   20,   1,  1, false},
    {0,                                                    0,   0,    0,   1,  1, false},
    {offsetof(system_params_t, ref_flow_delay_mins),       1,   2,   10,   1,  1, false},
    {offsetof(system_params_t, flow_fault_retry_limit),    1,   2,    5,   1,  1, false},
    {offsetof(system_params_t, orp_time_limit_hours),      1,   2,   12,   1,  1, false},
    {offsetof(system_params_t, orp_rest_delay_mins),       1,  20,   60,   1,  1, false}
};

int32_t ui_get_param_val(uint8_t idx) {
    if (menu_table[idx].size == 0) return 0;
    const uint8_t *p = (const uint8_t *)&params + menu_table[idx].offset;
    int32_t raw = (menu_table[idx].size == 1) ? *(const uint8_t *)p : *(const uint16_t *)p;
    return raw * menu_table[idx].scale;
}

void ui_set_param_val(uint8_t idx, int32_t ui) {
    if (menu_table[idx].size == 0) return;
    uint8_t *p = (uint8_t *)&params + menu_table[idx].offset;
    int32_t raw = ui / menu_table[idx].scale;
    if (menu_table[idx].size == 1) *(uint8_t *)p  = (uint8_t)raw;
    else                           *(uint16_t *)p = (uint16_t)raw;
}

static int32_t step_param_val(uint8_t idx, int32_t value, int8_t direction) {
    const param_def_t *d = &menu_table[idx];
    if (direction > 0) {
        if (d->zero_off && value == 0) value = d->min_ui;
        else value += d->step_ui;
    } else {
        value -= d->step_ui;
        if (d->zero_off && value < d->min_ui) value = 0;
    }
    if (!(d->zero_off && value == 0)) {
        if (value < d->min_ui) value = d->min_ui;
        if (value > d->max_ui) value = d->max_ui;
    }
    return value;
}

char current_display_str[5] = "----";

static void set_display4(char c0, char c1, char c2, char c3) {
    current_display_str[0] = c0;
    current_display_str[1] = c1;
    current_display_str[2] = c2;
    current_display_str[3] = c3;
    current_display_str[4] = '\0';
}

static void set_display_num(int num) {
    if (num < 0) num = 0;
    current_display_str[3] = '0' + (num % 10);
    num /= 10;
    current_display_str[2] = (num > 0) ? ('0' + (num % 10)) : ' ';
    num /= 10;
    current_display_str[1] = (num > 0) ? ('0' + (num % 10)) : ' ';
    num /= 10;
    current_display_str[0] = (num > 0) ? ('0' + (num % 10)) : ' ';
    current_display_str[4] = '\0';
}

static void show_err_display(uint8_t code) {
    uint8_t data_err[4] = {LTR_E, LTR_r, LTR_r, DIGIT_SEG[code]};
    tm1637_show_segments(data_err);
    set_display4('E', 'r', 'r', '0' + code);
}

void ui_init(void) {
    tm1637_init();
    tm1637_set_brightness(5);
    tm1637_show_segments(MSG_SCP);
    Delay_Ms(1500);
    tm1637_show_segments(MSG_PRO);
    Delay_Ms(1000);
    static const uint8_t CLR[4] = {BLANK, BLANK, BLANK, BLANK};
    tm1637_show_segments(CLR);
}

void ui_show_serial_menu_display(void) {
    tm1637_set_brightness(4);
    tm1637_show_segments(MSG_PC);
    set_display4(' ', 'P', 'C', ' ');
}

void ui_update_display(uint16_t current_v, uint16_t current_ma) {
    static uint32_t bright_timer       = 0;
    static bool     bright_init        = false;
    static fault_type_t   last_fault   = FAULT_NONE;
    static system_state_t last_state   = STATE_STARTUP_DELAY;

    uint32_t now = millis();
    if (!bright_init) {
        bright_timer = now;
        bright_init = true;
    }

    if (current_fault != last_fault) {
        last_fault = current_fault;
        if (current_fault != FAULT_NONE) bright_timer = now;
    }
    if (current_state != last_state) {
        last_state = current_state;
        if (current_state == STATE_STARTUP_DELAY) bright_timer = now;
    }

    tm1637_set_brightness((now - bright_timer <= 10000UL) ? 4 : 1);

    bool is_alt_screen = ((now / 2000) % 2) == 1;

    int32_t remaining_sec = 0;
    if (current_state == STATE_STARTUP_DELAY) {
        remaining_sec = (int32_t)(params.time_delay_mins * 60000UL) - (int32_t)(now - state_timer);
    } else if (current_state == STATE_ORP_REST) {
        remaining_sec = (int32_t)(params.orp_rest_delay_mins * 60000UL) - (int32_t)(now - state_timer);
    }
    if (remaining_sec < 0) remaining_sec = 0;
    remaining_sec /= 1000;

    if (current_state == STATE_RUNNING) {
        if (is_alt_screen) {
            int v = current_v;
            uint8_t data_v[4] = {
                v > 99 ? DIGIT_SEG[v / 100] : BLANK,
                DIGIT_SEG[(v / 10) % 10],
                DIGIT_SEG[v % 10],
                LTR_U
            };
            tm1637_show_segments(data_v);
            set_display4((v >= 100) ? ('0' + v / 100) : ' ', '0' + (v / 10) % 10, '0' + v % 10, 'U');
        } else {
            tm1637_show_number(current_ma);
            set_display_num(current_ma);
        }
    } else if (current_state == STATE_FAULT) {
        show_err_display(current_fault);
    } else if (current_state == STATE_STARTUP_DELAY || current_state == STATE_ORP_REST) {
        if (remaining_sec > 60) {
            if (is_alt_screen && current_fault != FAULT_NONE) {
                show_err_display(current_fault);
            } else {
                int m = (remaining_sec / 60) + 1;
                tm1637_show_number(m);
                set_display4(' ', 'd', (m >= 10) ? ('0' + m / 10) : ' ', '0' + m % 10);
            }
        } else {
            tm1637_show_number(remaining_sec);
            int s = remaining_sec;
            set_display4(' ', 's', (s >= 10) ? ('0' + s / 10) : ' ', '0' + s % 10);
        }
    } else if (current_state == STATE_LOCKOUT) {
        if (is_alt_screen) {
            show_err_display(current_fault);
        } else {
            uint8_t loc_reason = (current_fault == FAULT_LRA) ? 1 : 2;
            uint8_t data_loc[4] = {LTR_L, LTR_O, LTR_C, DIGIT_SEG[loc_reason]};
            tm1637_show_segments(data_loc);
            set_display4('L', 'O', 'C', '0' + loc_reason);
        }
    }
}

typedef struct {
    uint8_t raw_btn;
    uint8_t click_event;
    bool    long_press_event;
} button_events_t;

static button_events_t read_buttons(uint8_t *last_btn, uint32_t *press_time, bool *long_handled) {
    button_events_t e = {0, 0, false};
    uint16_t adc = sensors_read_adc(PIN_BUTTONS);
    if (adc < 240)                     e.raw_btn = 1;
    else if (adc >= 240 && adc <= 420) e.raw_btn = 2;
    else if (adc >= 420 && adc <= 650) e.raw_btn = 3;

    uint32_t now = millis();
    if (e.raw_btn != *last_btn) {
        if (e.raw_btn != 0) {
            *press_time = now;
            *long_handled = false;
            system_button_chirp();
        } else if (!(*long_handled) && (now - *press_time > 50)) {
            e.click_event = *last_btn;
        }
        *last_btn = e.raw_btn;
    } else if (e.raw_btn == 2 && !(*long_handled) && (now - *press_time > 1000)) {
        e.long_press_event = true;
        *long_handled = true;
    }
    return e;
}

void ui_handle_setting_mode(void) {
    tm1637_set_brightness(4);
    static uint8_t  menu_state   = 0;
    static int8_t   current_menu = 1;
    static int32_t  edit_value   = 0;
    static uint8_t  last_btn     = 0;
    static uint32_t press_time   = 0;
    static bool     long_handled = false;

    button_events_t btn = read_buttons(&last_btn, &press_time, &long_handled);

    switch (menu_state) {
    case 0:
        tm1637_show_segments(MSG_SETT);
        if (btn.raw_btn == 0) menu_state = 1;
        break;
    case 1:
        tm1637_show_segments(MSG_SETT);
        if (btn.click_event == 2) menu_state = 2;
        break;
    case 2:
        if (current_menu <= 20) {
            uint8_t data_s[4] = {
                LTR_S, SEG_G,
                (current_menu >= 10) ? DIGIT_SEG[current_menu / 10] : BLANK,
                DIGIT_SEG[current_menu % 10]
            };
            tm1637_show_segments(data_s);
        } else if (current_menu == 21) tm1637_show_segments(MSG_SAVE);
        else if (current_menu == 22)   tm1637_show_segments(MSG_RST);

        if (btn.click_event == 1 && current_menu < 22) current_menu++;
        if (btn.click_event == 3 && current_menu > 1)  current_menu--;

        if (btn.click_event == 2) {
            if (current_menu == 21 || current_menu == 22) {
                tm1637_show_segments(MSG_OK);
                if (current_menu == 21) params_save();
                else                    params_reset_defaults();
                Delay_Ms(1000);
                system_reboot();
            } else {
                edit_value = ui_get_param_val(current_menu - 1);
                menu_state = 3;
            }
        }
        break;
    case 3: {
        tm1637_show_number(edit_value);
        uint8_t idx = current_menu - 1;
        if (btn.click_event == 1) edit_value = step_param_val(idx, edit_value, +1);
        if (btn.click_event == 3) edit_value = step_param_val(idx, edit_value, -1);
        if (btn.click_event == 2 || btn.long_press_event) {
            ui_set_param_val(idx, edit_value);
            menu_state = 2;
        }
        break;
    }
    }
}

void ui_handle_calibration_mode(void) {
    tm1637_set_brightness(4);
    static uint8_t  menu_state   = 0;
    static int8_t   current_menu = 1;
    static int32_t  edit_value   = 0;
    static uint8_t  last_btn     = 0;
    static uint32_t press_time   = 0;
    static bool     long_handled = false;

    button_events_t btn = read_buttons(&last_btn, &press_time, &long_handled);

    switch (menu_state) {
    case 0:
        tm1637_show_segments(MSG_CAL);
        if (btn.raw_btn == 0) menu_state = 1;
        break;
    case 1:
        tm1637_show_segments(MSG_CAL);
        if (btn.click_event == 2) menu_state = 2;
        break;
    case 2:
        if      (current_menu == 1) tm1637_show_segments(MSG_URT);
        else if (current_menu == 2) tm1637_show_segments(MSG_ART);
        else if (current_menu == 3) tm1637_show_segments(MSG_TRT);
        else if (current_menu == 4) tm1637_show_segments(MSG_C1);
        else if (current_menu == 5) tm1637_show_segments(MSG_C2);
        else if (current_menu == 6) tm1637_show_segments(MSG_C3);
        else if (current_menu == 7) tm1637_show_segments(MSG_SAVE);

        if (btn.click_event == 1 && current_menu < 7) current_menu++;
        if (btn.click_event == 3 && current_menu > 1) current_menu--;

        if (btn.click_event == 2) {
            if (current_menu == 7) {
                tm1637_show_segments(MSG_OK);
                params_save();
                Delay_Ms(1000);
                menu_state = 2;
            } else {
                if      (current_menu == 4) edit_value = params.voltage_multiplier_x1000;
                else if (current_menu == 5) edit_value = params.current_multiplier_x1000;
                menu_state = 3;
            }
        }
        break;
    case 3: {
        static uint32_t last_live_update = 0;
        static int displayed_live_val = 0;
        if (millis() - last_live_update >= 250) {
            last_live_update = millis();
            if      (current_menu == 1) displayed_live_val = (sys_volt_dv + 5) / 10;
            else if (current_menu == 2) displayed_live_val = sys_amp_ma;
            else if (current_menu == 3) displayed_live_val = sys_temp1;
            else if (current_menu == 4 || current_menu == 5) displayed_live_val = edit_value;
            else if (current_menu == 6) displayed_live_val = 0;
        }
        tm1637_show_number(displayed_live_val);
        if (current_menu == 4 || current_menu == 5) {
            if (btn.click_event == 1) edit_value += 1;
            if (btn.click_event == 3) edit_value -= 1;
            if (edit_value > 5000) edit_value = 5000;
            if (edit_value < 1)    edit_value = 1;
        }
        if (btn.click_event == 2 || btn.long_press_event) {
            if (current_menu == 4) params.voltage_multiplier_x1000 = (uint16_t)edit_value;
            if (current_menu == 5) params.current_multiplier_x1000 = (uint16_t)edit_value;
            menu_state = 2;
        }
        break;
    }
    }
}