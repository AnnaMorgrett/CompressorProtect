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

uint16_t sys_volt_dv = 0; // Decivolts (0.1 V resolution, e.g., 2200 = 220.0 V)
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
    static uint32_t last_led_timer   = 0;
    static uint8_t  blink_count      = 0;
    static bool     is_blinking      = false;
    static bool     led_state        = false;

    static uint32_t last_buzzer_ms   = 0;
    static bool     buzzer_init      = false;
    static uint32_t last_beep_toggle = 0;
    static uint8_t  beep_count       = 0;
    static bool     is_beeping       = false;
    static bool     buzzer_state     = false;

    uint32_t now = millis();

    // 1. LED STATUS
    if (current_state == STATE_RUNNING) {
        funDigitalWrite(PIN_LED_STATUS, FUN_HIGH); // Solid ON when running
        is_blinking = false;
    } else if (current_state == STATE_STARTUP_DELAY) {
        // Slow blink: 500 ms toggle
        if (now - last_led_timer >= 500) {
            last_led_timer = now;
            led_state = !led_state;
            funDigitalWrite(PIN_LED_STATUS, led_state ? FUN_HIGH : FUN_LOW);
        }
        is_blinking = false;
    } else if (current_state == STATE_FAULT || current_state == STATE_LOCKOUT || current_state == STATE_ORP_REST) {
        uint8_t max_blinks = (current_state == STATE_ORP_REST) ? 10 : (uint8_t)current_fault;
        if (max_blinks == 0) max_blinks = 1;

        if (!is_blinking) {
            if (now - last_led_timer >= 1500) {
                is_blinking = true;
                blink_count = 0;
                last_led_timer = now;
            }
            funDigitalWrite(PIN_LED_STATUS, FUN_LOW);
        } else {
            uint32_t dur = led_state ? 200 : 500;
            if (now - last_led_timer >= dur) {
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

    // 2. ACTIVE BUZZER (Fault: 30 min, Lockout: 15 min)
    if (current_state == STATE_FAULT || current_state == STATE_LOCKOUT) {
        uint32_t interval = (current_state == STATE_LOCKOUT) ? 900000UL : 1800000UL;

        if (!buzzer_init) {
            last_buzzer_ms = now - interval; // Immediate first alarm
            buzzer_init = true;
        }

        if (!is_beeping && (now - last_buzzer_ms >= interval)) {
            is_beeping       = true;
            beep_count       = 0;
            last_beep_toggle = now;
            buzzer_state     = true;
            funDigitalWrite(PIN_BUZZER, FUN_HIGH);
        }

        if (is_beeping) {
            if (now - last_beep_toggle >= 300) {
                last_beep_toggle = now;
                buzzer_state = !buzzer_state;
                funDigitalWrite(PIN_BUZZER, buzzer_state ? FUN_HIGH : FUN_LOW);
                if (!buzzer_state) {
                    beep_count++;
                    if (beep_count >= 5) {
                        is_beeping     = false;
                        last_buzzer_ms = now;
                    }
                }
            }
        }
    } else {
        funDigitalWrite(PIN_BUZZER, FUN_LOW);
        is_beeping  = false;
        buzzer_init = false;
    }
}

// ==========================================
// MAIN ENTRYPOINT
// ==========================================
int main(void) {
    SystemInit();
    sys_time_init();
    funGpioInitAll();

    serial_cli_init();
    sensors_init();

    funPinMode(PIN_RELAY,      GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_LED_STATUS, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_BUZZER,     GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);

    funDigitalWrite(PIN_RELAY,      FUN_LOW);
    funDigitalWrite(PIN_LED_STATUS, FUN_LOW);
    funDigitalWrite(PIN_BUZZER,     FUN_LOW);

    funPinMode(PIN_CALIBRATION, GPIO_CNF_IN_PUPD);
    funDigitalWrite(PIN_CALIBRATION, FUN_HIGH); // Pull-up
    Delay_Ms(50);

    ui_init();
    if (!params_load()) {
        serial_cli_log_str("[WARN] EEPROM CRC mismatch -> defaults restored");
        beep_ms(120);
    }

    // Power-on mode check
    if (funDigitalRead(PIN_CALIBRATION) == FUN_LOW) {
        current_mode = MODE_CALIBRATION;
        funDigitalWrite(PIN_RELAY, FUN_HIGH); // Force relay ON for live calibration load
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
            sys_temp1 = sensors_read_temp1();
            sys_temp2 = sensors_read_temp2();
        }

        if (rx_interrupted || serial_menu_active) continue;

        // Telemetry every 1 second
        if (millis() - serial_telemetry_timer >= 1000) {
            serial_telemetry_timer = millis();
            serial_cli_send_telemetry();
        }

        if (current_mode == MODE_CALIBRATION) {
            ui_handle_calibration_mode();
            continue;
        } else if (current_mode == MODE_SETTING) {
            ui_handle_setting_mode();
            continue;
        }

        // ==========================================
        // NORMAL SYSTEM PROTECTION
        // ==========================================

        // Instant Surge & Blackout (2 consecutive 40ms samples)
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

        // State Machine
        switch (current_state) {
        case STATE_STARTUP_DELAY: {
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            if (!is_voltage_safe(sys_volt_dv)) {
                state_timer = millis(); // Hold delay if voltage unsafe
            } else if (millis() - state_timer >= (params.time_delay_mins * 60000UL)) {
                current_fault     = FAULT_NONE;
                initial_temp_ntc2 = sys_temp2;
                is_flow_checked   = false;
                running_timer     = millis();
                orp_timer         = millis();

                current_state         = STATE_RUNNING;
                fault_condition_timer = millis();
                funDigitalWrite(PIN_RELAY, FUN_HIGH);
                serial_cli_log_str("[SYSTEM] STARTING COMPRESSOR -> RELAY ON");
            }
            break;
        }

        case STATE_RUNNING: {
            uint32_t run_duration = millis() - running_timer;

            // Voltage monitoring (OVP / UVP)
            if (sys_volt_dv > (OVP_LIMIT * 10U)) {
                check_fault_delay(FAULT_OVP, params.ovp_uvp_delay);
            } else if (sys_volt_dv < (params.uvp_limit * 10U)) {
                check_fault_delay(FAULT_UVP, params.ovp_uvp_delay);
            } else {
                fault_condition_timer = millis();
            }

            // Current monitoring: LRA in startup window vs OCP/UCP
            if (run_duration < (params.lra_time * 1000UL)) {
                if (sys_amp_ma >= params.lra_limit_ma) {
                    if (millis() - fault_condition_timer >= (params.lra_time * 1000UL)) {
                        lra_retry_count++;
                        if (lra_retry_count >= params.lra_retry_limit) {
                            current_fault = FAULT_LRA;
                            current_state = STATE_LOCKOUT;
                            funDigitalWrite(PIN_RELAY, FUN_LOW);
                            serial_cli_log_str("[FATAL] LRA Retry Exceeded -> LOCKOUT");
                        } else {
                            trigger_fault(FAULT_LRA);
                        }
                    }
                } else {
                    fault_condition_timer = millis();
                }
            } else {
                // Post-inrush: OCP & UCP
                if (sys_amp_ma >= params.ocp_limit_ma) {
                    check_fault_delay(FAULT_OCP, params.ocp_delay);
                } else {
                    fault_condition_timer = millis();
                }

                uint16_t ucp_ma = (uint16_t)params.ucp_limit_x10 * 10U;
                uint16_t min_ma = (uint16_t)params.min_current_threshold_x10 * 10U;
                if (ucp_ma > 0 && sys_amp_ma >= min_ma && sys_amp_ma <= ucp_ma) {
                    check_fault_delay(FAULT_UCP, 60);
                }
            }

            // Continuous running timer (ORP)
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
            else if (current_fault != FAULT_OVP && current_fault != FAULT_UVP &&
                     current_fault != FAULT_BLACKOUT && current_fault != FAULT_SURGE && current_fault != FAULT_OTP)
                ready_to_recover = true;

            if (ready_to_recover) {
                current_state = STATE_STARTUP_DELAY;
                current_fault = FAULT_NONE;
                state_timer   = millis();
                serial_cli_log_str("[SYSTEM] Fault Recovered -> Restarting Startup Delay");
            }
            break;
        }

        case STATE_ORP_REST: {
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            if (millis() - state_timer >= (params.orp_rest_delay_mins * 60000UL)) {
                current_state = STATE_STARTUP_DELAY;
                state_timer   = millis();
            }
            break;
        }

        case STATE_LOCKOUT: {
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            static uint32_t lockout_reset_timer = 0;
            uint16_t adc = sensors_read_adc(PIN_BUTTONS);
            // Hold Menu button for 3 seconds -> manual lockout reset (from firmware_d)
            if (adc >= 240 && adc <= 420) {
                if (lockout_reset_timer == 0) lockout_reset_timer = millis();
                else if (millis() - lockout_reset_timer >= 3000UL) {
                    lra_retry_count     = 0;
                    flow_retry_count    = 0;
                    current_fault       = FAULT_NONE;
                    current_state       = STATE_STARTUP_DELAY;
                    state_timer         = millis();
                    lockout_reset_timer = 0;
                    serial_cli_log_str("[SYSTEM] Manual Lockout Reset -> Startup Delay");
                    system_button_chirp();
                }
            } else {
                lockout_reset_timer = 0;
            }
            break;
        }
        }

        ui_update_display((sys_volt_dv + 5) / 10, sys_amp_ma);
        handle_indicators();
    }
}
