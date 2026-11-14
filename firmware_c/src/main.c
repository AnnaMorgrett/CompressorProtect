#include "board.h"
#include "sys_time.h"
#include "params.h"
#include "sensors.h"
#include "ui.h"
#include "serial_cli.h"

// ==========================================
// GLOBAL SYSTEM STATE
// ==========================================
operating_mode_t current_mode  = MODE_NORMAL_PROTECTION;
system_state_t   current_state = STATE_STARTUP_DELAY;
fault_type_t     current_fault = FAULT_NONE;

uint16_t sys_volt_dv = 0; // Decivolts (0.1 V resolution, e.g., 2205 = 220.5 V)
uint16_t sys_amp_ma  = 0; // Milliamps (1 mA resolution)
int16_t  sys_temp1   = 0;
int16_t  sys_temp2   = 0;

uint32_t state_timer            = 0;
uint32_t running_timer          = 0;
uint32_t orp_timer              = 0;
uint32_t fault_condition_timer  = 0;
uint32_t serial_telemetry_timer = 0;

uint8_t lra_retry_count   = 0;
uint8_t flow_retry_count  = 0;
int16_t initial_temp_ntc2 = 0;
bool    is_flow_checked   = false;

// ==========================================
// WATCHDOG & SYSTEM HELPERS
// ==========================================
static inline void watchdog_kick(void) {
    IWDG->CTLR = 0xAAAA;
}

static void watchdog_init(void) {
    IWDG->CTLR = 0x5555;
    IWDG->PSCR = 4;    // /64 -> 625 Hz
    IWDG->RLDR = 1249; // ~2.0 s hardware watchdog timeout
    IWDG->CTLR = 0xAAAA;
    IWDG->CTLR = 0xCCCC;
}

static void beep_ms(uint16_t ms) {
#if BUZZER_PASSIVE
    const uint16_t half_period_us = 500000UL / BUZZER_FREQ_HZ;
    uint32_t end = millis() + ms;
    while ((int32_t)(millis() - end) < 0) {
        funDigitalWrite(PIN_BUZZER, FUN_HIGH);
        Delay_Us(half_period_us);
        funDigitalWrite(PIN_BUZZER, FUN_LOW);
        Delay_Us(half_period_us);
        watchdog_kick();
    }
#else
    funDigitalWrite(PIN_BUZZER, FUN_HIGH);
    uint32_t end = millis() + ms;
    while ((int32_t)(millis() - end) < 0) {
        watchdog_kick();
        Delay_Ms(5);
    }
#endif
    funDigitalWrite(PIN_BUZZER, FUN_LOW);
}

void system_button_chirp(void) {
    beep_ms(40);
}

void system_reboot(void) {
    serial_cli_log_str("[Reboot]");
    Delay_Ms(100);
    IWDG->CTLR = 0x5555;
    IWDG->RLDR = 0;
    IWDG->CTLR = 0xAAAA;
    while (1) {}
}

static void trigger_fault(fault_type_t fault) {
    if (current_state == STATE_FAULT && current_fault == fault) return;
    current_fault = fault;
    current_state = STATE_FAULT;
    funDigitalWrite(PIN_RELAY, FUN_LOW);
    fault_condition_timer = millis();
    serial_cli_log_fault((int32_t)fault);
}

static void check_fault_delay(fault_type_t fault, uint8_t delay_seconds) {
    if (millis() - fault_condition_timer >= (delay_seconds * 1000UL)) {
        trigger_fault(fault);
    }
}

static bool is_voltage_safe(uint16_t v_dv) {
    return (v_dv > (params.uvp_limit * 10U) && v_dv < (OVP_LIMIT * 10U));
}

static void handle_indicators(void) {
    static uint32_t last_led_timer = 0;
    static uint8_t  blink_count    = 0;
    static bool     is_blinking    = false;
    static bool     led_state      = false;

    static uint32_t last_buzzer_interval = 0;
    static bool     buzzer_init          = false;
    static uint32_t next_beep_time       = 0;
    static uint8_t  beep_count           = 0;
    static bool     is_beeping           = false;

    uint32_t now = millis();
    if (!buzzer_init) {
        last_buzzer_interval = now - 1800000UL;
        buzzer_init = true;
    }

    if (current_state == STATE_RUNNING) {
        funDigitalWrite(PIN_LED_STATUS, FUN_HIGH);
    } else if (current_state == STATE_STARTUP_DELAY) {
        if (now - last_led_timer >= 500) {
            last_led_timer = now;
            led_state = !led_state;
            funDigitalWrite(PIN_LED_STATUS, led_state ? FUN_HIGH : FUN_LOW);
        }
    } else if (current_state == STATE_FAULT || current_state == STATE_LOCKOUT || current_state == STATE_ORP_REST) {
        uint8_t max_blinks = current_fault;
        if (current_state == STATE_ORP_REST) max_blinks = 10;
        if (current_state == STATE_LOCKOUT)  max_blinks = (current_fault == FAULT_LRA) ? 1 : 5;
        if (max_blinks == 0) max_blinks = 1;

        if (!is_blinking) {
            if (now - last_led_timer >= 1500) {
                is_blinking = true;
                blink_count = 0;
                last_led_timer = now;
            }
            funDigitalWrite(PIN_LED_STATUS, FUN_LOW);
        } else {
            if (now - last_led_timer >= (led_state ? 200U : 500U)) {
                last_led_timer = now;
                led_state = !led_state;
                funDigitalWrite(PIN_LED_STATUS, led_state ? FUN_HIGH : FUN_LOW);
                if (!led_state) {
                    blink_count++;
                    if (blink_count >= max_blinks) is_blinking = false;
                }
            }
        }
    }

    if (current_state == STATE_FAULT || current_state == STATE_LOCKOUT) {
        uint32_t interval = (current_state == STATE_LOCKOUT) ? 900000UL : 1800000UL;
        if (!is_beeping && (now - last_buzzer_interval >= interval)) {
            is_beeping = true;
            beep_count = 0;
            next_beep_time = now;
        }
        if (is_beeping && (int32_t)(now - next_beep_time) >= 0) {
            beep_ms(300);
            beep_count++;
            next_beep_time = millis() + 300;
            if (beep_count >= 5) {
                is_beeping = false;
                last_buzzer_interval = millis();
            }
        }
    } else {
        funDigitalWrite(PIN_BUZZER, FUN_LOW);
        is_beeping = false;
        last_buzzer_interval = now - 1800000UL;
    }
}

// ==========================================
// MAIN ENTRY POINT
// ==========================================
int main(void) {
    SystemInit();
    funGpioInitAll();

    serial_cli_init();
    sensors_init();

    funPinMode(PIN_RELAY, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_LED_STATUS, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_BUZZER, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);

    funDigitalWrite(PIN_RELAY, FUN_LOW);
    funDigitalWrite(PIN_LED_STATUS, FUN_LOW);
    funDigitalWrite(PIN_BUZZER, FUN_LOW);

    funPinMode(PIN_CALIBRATION, GPIO_CNF_IN_PUPD);
    funDigitalWrite(PIN_CALIBRATION, FUN_HIGH); // Pull-up
    Delay_Ms(50);

    ui_init();
    if (!params_load()) {
        serial_cli_log_str("[WARN] EEPROM CRC mismatch -> defaults restored");
        beep_ms(120);
    }

    if (funDigitalRead(PIN_CALIBRATION) == FUN_LOW) {
        current_mode = MODE_CALIBRATION;
        funDigitalWrite(PIN_RELAY, FUN_HIGH);
        serial_cli_log_str(">>> MODE: KALIBRASI (RELAY ON) <<<");
    } else {
        uint16_t button_adc = sensors_read_adc(PIN_BUTTONS);
        if (button_adc >= 240 && button_adc <= 420) {
            current_mode = MODE_SETTING;
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            serial_cli_log_str(">>> MODE: SETTING <<<");
        } else {
            current_mode = MODE_NORMAL_PROTECTION;
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            state_timer = millis();
            serial_cli_log_str(">>> PROTEKSI AKTIF (ketik 'menu' / '-h') <<<");
        }
        serial_cli_enable_rx();
    }

    watchdog_init();

    uint8_t surge_samples    = 0;
    uint8_t blackout_samples = 0;

    while (1) {
        watchdog_kick();
        serial_cli_poll();

        if (serial_menu_active) {
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            if (serial_menu_stream && (millis() - serial_telemetry_timer >= 1000)) {
                serial_telemetry_timer = millis();
                sys_volt_dv = sensors_read_voltage_dv();
                sys_amp_ma  = sensors_read_current_ma();
                serial_cli_send_telemetry();
            }
            continue;
        }

        bool rx_interrupted = false;
        if (current_mode != MODE_SETTING) {
            sys_volt_dv = sensors_read_voltage_dv();
            if (serial_cli_rx_incoming()) {
                rx_interrupted = true;
                serial_cli_poll();
            } else {
                sys_amp_ma = sensors_read_current_ma();
                if (serial_cli_rx_incoming()) {
                    rx_interrupted = true;
                    serial_cli_poll();
                }
            }
#if NTC1_ENABLED
            sys_temp1 = sensors_read_temp1();
#endif
#if NTC2_ENABLED
            sys_temp2 = sensors_read_temp2();
#endif
        }

        if (serial_menu_active || rx_interrupted) continue;

        if (current_state != STATE_RUNNING && (millis() - serial_telemetry_timer >= 1000)) {
            serial_telemetry_timer = millis();
            serial_cli_send_telemetry();
        }

        if (current_mode == MODE_CALIBRATION) {
            ui_handle_calibration_mode();
            continue;
        }
        if (current_mode == MODE_SETTING) {
            ui_handle_setting_mode();
            continue;
        }

        // 2-sample fast confirmation (~40-60 ms) for severe surge/blackout
        if (sys_volt_dv >= (PARAM_POWER_SURGE_LIMIT * 10U)) {
            if (++surge_samples >= 2) trigger_fault(FAULT_SURGE);
        } else {
            surge_samples = 0;
        }

        if (sys_volt_dv <= (PARAM_BLACKOUT_LIMIT * 10U)) {
            if (++blackout_samples >= 2) trigger_fault(FAULT_BLACKOUT);
        } else {
            blackout_samples = 0;
        }

        switch (current_state) {
        case STATE_STARTUP_DELAY: {
            funDigitalWrite(PIN_RELAY, FUN_LOW);
#if NTC1_ENABLED
            bool compressor_hot = (sys_temp1 > (params.otp_limit - params.otp_hysteresis));
#else
            bool compressor_hot = false;
#endif
            if (!is_voltage_safe(sys_volt_dv) || compressor_hot) {
                state_timer = millis();
            } else if (millis() - state_timer >= (params.time_delay_mins * 60000UL)) {
                current_fault = FAULT_NONE;
#if NTC2_ENABLED
                initial_temp_ntc2 = sys_temp2;
                is_flow_checked = false;
#endif
                running_timer = millis();
                orp_timer     = millis();
                current_state = STATE_RUNNING;
                funDigitalWrite(PIN_RELAY, FUN_HIGH);
                serial_cli_log_str("[SYS] RELAY ON");
            }
            break;
        }

        case STATE_RUNNING: {
            if (sys_volt_dv > (OVP_LIMIT * 10U))             check_fault_delay(FAULT_OVP, params.ovp_uvp_delay);
            else if (sys_volt_dv < (params.uvp_limit * 10U)) check_fault_delay(FAULT_UVP, params.ovp_uvp_delay);
            else                                             fault_condition_timer = millis();

#if NTC1_ENABLED
            if (sys_temp1 >= params.otp_limit) {
                trigger_fault(FAULT_OTP);
            }
#endif

            uint32_t run_duration = millis() - running_timer;
            if (run_duration <= (params.lra_time * 1000UL)) {
                if (sys_amp_ma >= params.lra_limit_ma) {
                    lra_retry_count++;
                    if (lra_retry_count >= params.lra_retry_limit) {
                        current_fault = FAULT_LRA;
                        current_state = STATE_LOCKOUT;
                        funDigitalWrite(PIN_RELAY, FUN_LOW);
                        serial_cli_log_str("[LOCKOUT] LRA");
                    } else {
                        trigger_fault(FAULT_LRA);
                    }
                }
            } else {
                lra_retry_count = 0;
                if (sys_amp_ma >= params.ocp_limit_ma) {
                    check_fault_delay(FAULT_OCP, params.ocp_delay);
                }
                if (params.ucp_limit_x10 > 0 && sys_amp_ma <= (params.ucp_limit_x10 * 10)) {
                    check_fault_delay(FAULT_UCP, 60);
                }
            }

#if NTC2_ENABLED
            if (!is_flow_checked && run_duration >= (params.ref_flow_delay_mins * 60000UL)) {
                is_flow_checked = true;
                if (sys_temp2 < (initial_temp_ntc2 + 5)) {
                    flow_retry_count++;
                    if (flow_retry_count >= params.flow_fault_retry_limit) {
                        current_fault = FAULT_REF_FLOW;
                        current_state = STATE_LOCKOUT;
                        funDigitalWrite(PIN_RELAY, FUN_LOW);
                        serial_cli_log_str("[LOCKOUT] FLOW");
                    } else {
                        trigger_fault(FAULT_REF_FLOW);
                    }
                } else {
                    flow_retry_count = 0;
                }
            }
#endif

            if (sys_amp_ma < (params.min_current_threshold_x10 * 10)) {
                orp_timer = millis();
            } else if (millis() - orp_timer >= (params.orp_time_limit_hours * 3600000UL)) {
                current_state = STATE_ORP_REST;
                state_timer   = millis();
                funDigitalWrite(PIN_RELAY, FUN_LOW);
                serial_cli_log_str("[SYS] ORP REST");
            }
            break;
        }

        case STATE_FAULT: {
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            bool ready_to_recover = false;
            uint16_t ovp_rec_dv = (OVP_LIMIT - params.voltage_hysteresis) * 10U;
            uint16_t uvp_rec_dv = (params.uvp_limit + params.voltage_hysteresis) * 10U;

            if (current_fault == FAULT_OVP && sys_volt_dv <= ovp_rec_dv)
                ready_to_recover = true;
            else if (current_fault == FAULT_UVP && sys_volt_dv >= uvp_rec_dv)
                ready_to_recover = true;
            else if (current_fault == FAULT_BLACKOUT && sys_volt_dv >= uvp_rec_dv)
                ready_to_recover = true;
            else if (current_fault == FAULT_SURGE && sys_volt_dv <= ovp_rec_dv)
                ready_to_recover = true;
#if NTC1_ENABLED
            else if (current_fault == FAULT_OTP && sys_temp1 <= (params.otp_limit - params.otp_hysteresis))
                ready_to_recover = true;
#endif
            else if (current_fault != FAULT_OVP && current_fault != FAULT_UVP &&
                     current_fault != FAULT_BLACKOUT && current_fault != FAULT_SURGE && current_fault != FAULT_OTP)
                ready_to_recover = true;

            if (ready_to_recover) {
                current_state = STATE_STARTUP_DELAY;
                current_fault = FAULT_NONE;
                state_timer   = millis();
            }
            break;
        }

        case STATE_ORP_REST: {
            funDigitalWrite(PIN_RELAY, FUN_LOW);
#if NTC1_ENABLED
            bool temp_cool = (sys_temp1 <= 60);
#else
            bool temp_cool = true;
#endif
            if ((millis() - state_timer >= (params.orp_rest_delay_mins * 60000UL)) && temp_cool) {
                current_state = STATE_STARTUP_DELAY;
                state_timer   = millis();
            }
            break;
        }

        case STATE_LOCKOUT:
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            break;
        }

        ui_update_display((sys_volt_dv + 5) / 10, sys_amp_ma);
        handle_indicators();
    }
}