#include "board.h"
#include "sys_time.h"
#include "params.h"
#include "sensors.h"
#include "ui.h"
#include "serial_cli.h"

// ============================================================================
// PROGRAM UTAMA & MESIN STATUS PROTEKSI KOMPRESOR (SMART COMPRESSOR PROTECTOR)
// ============================================================================
// Mikrokontroler: WCH CH32V003A4M6 (32-bit RISC-V, 48 MHz, 16KB Flash, 2KB RAM)
// Kemasan: SOP-16
// Arsitektur: Non-blocking State Machine dengan Hardware Watchdog (~2.0 detik)
//
// Alur Kerja Sistem:
// 1. Booting & Deteksi Mode (Normal / Setting / Kalibrasi).
// 2. Startup Delay (Tunda aman 2-10 menit untuk menyeimbangkan tekanan freon AC).
// 3. Running State (Kompresor menyala, pemantauan inrush LRA, OVP, UVP, OCP, UCP, ORP).
// 4. Fault Trip (Relay mati instan <80ms saat surge/blackout, alarm bip 5x, flip-flop display).
// 5. Auto Recovery (Pemulihan otomatis dengan histeresis tegangan setelah listrik stabil).
// 6. Lockout (Penguncian permanen jika kegagalan berulang melampaui batas toleransi).
// ============================================================================

// ============================================================================
// VARIABEL STATUS GLOBAL SISTEM
// ============================================================================
operating_mode_t current_mode  = MODE_NORMAL_PROTECTION; // Mode saat ini (Normal/Kalibrasi/Setting)
system_state_t   current_state = STATE_STARTUP_DELAY;     // Status mesin proteksi
fault_type_t     current_fault = FAULT_NONE;              // Jenis gangguan aktif (Err 1..9)

uint16_t sys_volt_dv = 0; // Tegangan AC dalam Decivolt (0.1 V), misal 2205 = 220.5 Volt
uint16_t sys_amp_ma  = 0; // Arus AC beban dalam Milliampere (1 mA), misal 1500 = 1.5 A
int16_t  sys_temp1   = 0; // Suhu NTC1 bodi kompresor (°C)
int16_t  sys_temp2   = 0; // Suhu NTC2 pipa filter dryer (°C)

// Timer pencatat waktu durasi (dalam milidetik via SysTick)
uint32_t state_timer            = 0; // Timer untuk delay start dan istirahat ORP
uint32_t running_timer          = 0; // Timer mencatat durasi sejak kompresor dinyalakan
uint32_t orp_timer              = 0; // Timer akumulasi jam kerja nonstop kompresor
uint32_t fault_condition_timer  = 0; // Timer toleransi gangguan (debounce fault)
uint32_t serial_telemetry_timer = 0; // Timer pengiriman data telemetri serial (setiap 1 detik)

uint8_t lra_retry_count   = 0;       // Penghitung akumulasi kegagalan start rotor macet (LRA)
uint8_t flow_retry_count  = 0;       // Penghitung akumulasi kegagalan sirkulasi freon
int16_t initial_temp_ntc2 = 0;       // Patokan suhu awal pipa kondensor sebelum kompresor menyala
bool    is_flow_checked   = false;   // Status apakah pengujian aliran refrigeran telah selesai

// ============================================================================
// WATCHDOG HARDWARE & FUNGSI BANTU SISTEM
// ============================================================================

/**
 * @brief Memberi makan watchdog (Kick/Reload) agar mikrokontroler tidak me-reset.
 *        Register kunci 0xAAAA dimuat ke CTLR untuk mereset counter IWDG.
 */
static inline void watchdog_kick(void) {
    IWDG->CTLR = 0xAAAA;
}

/**
 * @brief Menginisialisasi Independent Watchdog Timer (IWDG) internal chip.
 *        Clock LSI = ~40 kHz, Prescaler /64 = 625 Hz (1 tick = 1.6 ms).
 *        Reload value = 1249 -> Timeout = 1250 * 1.6 ms = 2.0 detik.
 *        Jika firmware hang/freeze lebih dari 2 detik, sistem otomatis restart mandiri!
 */
static void watchdog_init(void) {
    IWDG->CTLR = 0x5555; // Buka proteksi penulisan register prescaler & reload
    IWDG->PSCR = 4;      // Prescaler divider /64
    IWDG->RLDR = 1249;   // Batas hitung reload: ~2.0 detik
    IWDG->CTLR = 0xAAAA; // Muat ulang nilai counter (Kick)
    IWDG->CTLR = 0xCCCC; // Kunci dan aktifkan IWDG secara permanen
}

/**
 * @brief Menghasilkan bunyi bip pada buzzer selama durasi milidetik tertentu.
 *        Dilengkapi watchdog kick di dalam loop agar tidak memicu reset sistem.
 * @param ms Durasi bunyi dalam milidetik.
 */
static void beep_ms(uint16_t ms) {
#if BUZZER_PASSIVE
    // Jika menggunakan passive piezo buzzer: bangkitkan sinyal PWM software sesuai BUZZER_FREQ_HZ
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
    // Jika menggunakan active buzzer: cukup beri logika HIGH pada pin driver
    funDigitalWrite(PIN_BUZZER, FUN_HIGH);
    uint32_t end = millis() + ms;
    while ((int32_t)(millis() - end) < 0) {
        watchdog_kick();
        Delay_Ms(5);
    }
#endif
    funDigitalWrite(PIN_BUZZER, FUN_LOW);
}

/**
 * @brief Bunyi nada klik/chirp saat tombol ditekan (diatur oleh flag BUTTON_CHIRP di board.h).
 *        Pada Batch 1 bernilai 0 (hening saat tekan tombol, buzzer hanya bunyi saat fault).
 */
void system_button_chirp(void) {
#if BUTTON_CHIRP
    beep_ms(40);
#endif
}

/**
 * @brief Memaksa mikrokontroler restart dengan memicu watchdog reset sengaja.
 */
void system_reboot(void) {
    serial_cli_log_str("[Reboot]");
    Delay_Ms(100);
    IWDG->CTLR = 0x5555;
    IWDG->RLDR = 0;      // Atur reload ke 0 agar langsung timeout
    IWDG->CTLR = 0xAAAA;
    while (1) {}         // Tunggu restart
}

/**
 * @brief Mematikan relay kompresor seketika dan mencatat status gangguan baru.
 * @param fault Kode gangguan (fault_type_t).
 */
static void trigger_fault(fault_type_t fault) {
    if (current_state == STATE_FAULT && current_fault == fault) return;
    current_fault = fault;
    current_state = STATE_FAULT;
    funDigitalWrite(PIN_RELAY, FUN_LOW); // Putus kontak relay instan!
    fault_condition_timer = millis();
    serial_cli_log_fault((int32_t)fault);
}

/**
 * @brief Memeriksa apakah kondisi abnormal telah berlangsung melebihi batas waktu toleransi.
 *        Jika ya, picu pemutusan relay (Trip Fault).
 * @param fault Jenis kesalahan yang sedang diuji.
 * @param delay_seconds Waktu toleransi dalam detik (misal S-04 atau S-10).
 */
static void check_fault_delay(fault_type_t fault, uint8_t delay_seconds) {
    if (millis() - fault_condition_timer >= (delay_seconds * 1000UL)) {
        trigger_fault(fault);
    }
}

/**
 * @brief Memeriksa apakah tegangan listrik berada dalam batas aman (antara UVP dan OVP).
 * @param v_dv Nilai tegangan listrik dalam Decivolt.
 * @return true jika tegangan berada dalam batas toleransi aman.
 */
static bool is_voltage_safe(uint16_t v_dv) {
    return (v_dv > (params.uvp_limit * 10U) && v_dv < (OVP_LIMIT * 10U));
}

/**
 * @brief Mengelola pola kedipan LED Indikator dan interval alarm Buzzer.
 *
 * Pola LED Status (PIN_LED_STATUS):
 * - Solid ON (Nyala Terus)       : Kompresor sedang running normal.
 * - Kedip Lambat (500 ms toggle) : Sistem berada dalam Startup Delay (menghitung mundur).
 * - Kedip N-Kali (Fault Code)    : Berkedip sebanyak nomor kode error (misal 3 kali untuk OVP),
 *                                  kemudian jeda padam 1.5 detik, lalu berulang.
 *
 * Pola Buzzer Alarm (PIN_BUZZER):
 * - Berbunyi 5 kali bip (300 ms nyala / 300 ms padam).
 * - Periode pengulangan:
 *   * Setiap 30 Menit pada gangguan normal (STATE_FAULT).
 *   * Setiap 15 Menit pada penguncian permanen (STATE_LOCKOUT).
 *   * Bunyi pertama langsung dibunyikan seketika saat gangguan terjadi!
 */
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

    // ------------------------------------------------------------------------
    // 1. MANAJEMEN LED STATUS
    // ------------------------------------------------------------------------
    if (current_state == STATE_RUNNING) {
        funDigitalWrite(PIN_LED_STATUS, FUN_HIGH); // Solid ON saat running
        is_blinking = false;
    } else if (current_state == STATE_STARTUP_DELAY) {
        // Kedip lambat (500 ms ON / 500 ms OFF) selama hitung mundur delay
        if (now - last_led_timer >= 500) {
            last_led_timer = now;
            led_state = !led_state;
            funDigitalWrite(PIN_LED_STATUS, led_state ? FUN_HIGH : FUN_LOW);
        }
        is_blinking = false;
    } else if (current_state == STATE_FAULT || current_state == STATE_LOCKOUT || current_state == STATE_ORP_REST) {
        // Kedip sejumlah kode kesalahan (Fault Code)
        uint8_t max_blinks = (current_state == STATE_ORP_REST) ? 10 : (uint8_t)current_fault;
        if (max_blinks == 0) max_blinks = 1;

        if (!is_blinking) {
            // Jeda padam 1.5 detik antar siklus kedipan kode
            if (now - last_led_timer >= 1500) {
                is_blinking = true;
                blink_count = 0;
                last_led_timer = now;
            }
            funDigitalWrite(PIN_LED_STATUS, FUN_LOW);
        } else {
            // Pulsa kedip aktif (200 ms ON, 500 ms OFF)
            uint32_t dur = led_state ? 200 : 500;
            if (now - last_led_timer >= dur) {
                last_led_timer = now;
                led_state = !led_state;
                funDigitalWrite(PIN_LED_STATUS, led_state ? FUN_HIGH : FUN_LOW);
                if (!led_state) {
                    blink_count++;
                    if (blink_count >= max_blinks) is_blinking = false; // Selesai 1 siklus
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // 2. MANAJEMEN ALARM ACTIVE BUZZER
    // ------------------------------------------------------------------------
    if (current_state == STATE_FAULT || current_state == STATE_LOCKOUT) {
        // Interval bunyi: Lockout = 15 menit (900.000 ms), Fault biasa = 30 menit (1.800.000 ms)
        uint32_t interval = (current_state == STATE_LOCKOUT) ? 900000UL : 1800000UL;

        if (!buzzer_init) {
            last_buzzer_ms = now - interval; // Picu alarm pertama kali seketika
            buzzer_init = true;
        }

        // Mulai rangkaian 5 kali bip jika interval waktu terpenuhi
        if (!is_beeping && (now - last_buzzer_ms >= interval)) {
            is_beeping       = true;
            beep_count       = 0;
            last_beep_toggle = now;
            buzzer_state     = true;
            funDigitalWrite(PIN_BUZZER, FUN_HIGH);
        }

        // Modulasi on/off pulsa bip 300 ms
        if (is_beeping) {
            if (now - last_beep_toggle >= 300) {
                last_beep_toggle = now;
                buzzer_state = !buzzer_state;
                funDigitalWrite(PIN_BUZZER, buzzer_state ? FUN_HIGH : FUN_LOW);
                if (!buzzer_state) {
                    beep_count++;
                    if (beep_count >= 5) {
                        is_beeping     = false; // Selesai 5 bip
                        last_buzzer_ms = now;
                    }
                }
            }
        }
    } else {
        // Matikan buzzer jika kondisi normal
        funDigitalWrite(PIN_BUZZER, FUN_LOW);
        is_beeping  = false;
        buzzer_init = false;
    }
}

// ============================================================================
// TITIK MASUK UTAMA PROGRAM (MAIN ENTRYPOINT)
// ============================================================================
int main(void) {
    // 1. Inisialisasi clock silikon 48 MHz, SysTick timer, dan port GPIO
    SystemInit();
    sys_time_init();
    funGpioInitAll();

    // 2. Inisialisasi serial CLI bit-bang dan periferal ADC sensor
    serial_cli_init();
    sensors_init();

    // 3. Konfigurasi pin output digital (Relay, LED, Buzzer)
    funPinMode(PIN_RELAY,      GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_LED_STATUS, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_BUZZER,     GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);

    // Pastikan seluruh aktuator dalam kondisi AMAN (Mati) saat awal menyala
    funDigitalWrite(PIN_RELAY,      FUN_LOW);
    funDigitalWrite(PIN_LED_STATUS, FUN_LOW);
    funDigitalWrite(PIN_BUZZER,     FUN_LOW);

    // 4. Konfigurasi pin jumper kalibrasi (PC6) dengan pull-up internal
    funPinMode(PIN_CALIBRATION, GPIO_CNF_IN_PUPD);
    funDigitalWrite(PIN_CALIBRATION, FUN_HIGH);
    Delay_Ms(50); // Stabilisasi jalur input

    // 5. Inisialisasi antarmuka visual 7-segment dan muat parameter dari flash
    ui_init();
    if (!params_load()) {
        // Jika data Option Bytes kosong atau CRC tidak cocok, pulihkan default pabrik
        serial_cli_log_str("[WARN] EEPROM CRC mismatch -> defaults restored");
        beep_ms(120);
    }

    // 6. Evaluasi Mode Saat Booting:
    // a. Mode Kalibrasi: Jika pin PC6 dihubungkan ke GND saat power-on
    if (funDigitalRead(PIN_CALIBRATION) == FUN_LOW) {
        current_mode = MODE_CALIBRATION;
        funDigitalWrite(PIN_RELAY, FUN_HIGH); // Paksa relay ON untuk menyalakan beban kalibrasi
        serial_cli_log_str(">>> MODE: KALIBRASI (RELAY ON) <<<");
    } else {
        // b. Mode Setting: Jika tombol Menu/OK pada panel ditekan saat power-on
        uint16_t button_adc = sensors_read_adc(PIN_BUTTONS);
        if (button_adc >= 240 && button_adc <= 420) {
            current_mode = MODE_SETTING;
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            serial_cli_log_str(">>> MODE: SETTING <<<");
        } else {
            // c. Mode Proteksi Normal (Operasi Pelanggan)
            current_mode = MODE_NORMAL_PROTECTION;
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            state_timer = millis(); // Mulai hitung mundur Startup Delay
            serial_cli_log_str(">>> PROTEKSI AKTIF (ketik 'menu' / '-h') <<<");
        }
        // Aktifkan penerimaan perintah serial RX
        serial_cli_enable_rx();
    }

    // 7. Aktifkan Watchdog Hardware (~2.0 detik)
    watchdog_init();

    // Variabel penghitung sampel gangguan instan
    uint8_t surge_samples    = 0;
    uint8_t blackout_samples = 0;

    // ========================================================================
    // LOOP UTAMA NON-BLOCKING (SUPER LOOP)
    // ========================================================================
    while (1) {
        // Beri makan watchdog setiap putaran loop
        watchdog_kick();

        // Periksa data masuk dari terminal serial CLI
        serial_cli_poll();

        // Jika terminal serial sedang membuka menu interaktif ("menu")
        if (serial_menu_active) {
            funDigitalWrite(PIN_RELAY, FUN_LOW); // Pastikan kompresor mati demi keamanan teknisi
            if (serial_menu_stream && (millis() - serial_telemetry_timer >= 1000)) {
                serial_telemetry_timer = millis();
                sys_volt_dv = sensors_read_voltage_dv();
                sys_amp_ma  = sensors_read_current_ma();
                serial_cli_send_telemetry();
            }
            continue;
        }

        // Pembacaan sensor dengan deteksi interupsi serial non-blocking
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

        // Jika terinterupsi perintah CLI serial, ulangi loop
        if (rx_interrupted || serial_menu_active) continue;

        // Kirim telemetri serial berkala setiap 1 detik
        if (millis() - serial_telemetry_timer >= 1000) {
            serial_telemetry_timer = millis();
            serial_cli_send_telemetry();
        }

        // Percabangan eksekusi sesuai mode aktif
        if (current_mode == MODE_CALIBRATION) {
            ui_handle_calibration_mode();
            continue;
        } else if (current_mode == MODE_SETTING) {
            ui_handle_setting_mode();
            continue;
        }

        // ====================================================================
        // LOGIKA SISTEM PROTEKSI KOMPRESOR NORMAL
        // ====================================================================

        // 1. Proteksi Instan Gelombang Petir / Lonjakan Ekstrem (Power Surge > 275V)
        // Memerlukan 2 sampel True-RMS berturut-turut (~80 ms) untuk memastikan bukan noise sesaat
        if (sys_volt_dv >= (PARAM_POWER_SURGE_LIMIT * 10U)) {
            if (++surge_samples >= 2) trigger_fault(FAULT_SURGE);
        } else {
            surge_samples = 0;
        }

        // 2. Proteksi Instan Pemadaman Listrik / Drop Ekstrem (Blackout < 100V)
        if (sys_volt_dv <= (PARAM_BLACKOUT_LIMIT * 10U)) {
            if (++blackout_samples >= 2) trigger_fault(FAULT_BLACKOUT);
        } else {
            blackout_samples = 0;
        }

        // 3. Mesin Status Kerja Kompresor
        switch (current_state) {
        // --------------------------------------------------------------------
        // STATE 0: STARTUP DELAY (WAKTU TUNDA AMAN SEBELUM START)
        // --------------------------------------------------------------------
        case STATE_STARTUP_DELAY: {
            funDigitalWrite(PIN_RELAY, FUN_LOW); // Pastikan kompresor mati
            // Jika tegangan listrik masih belum aman, tahan timer delay (tunggu tegangan normal)
            if (!is_voltage_safe(sys_volt_dv)) {
                state_timer = millis();
            } else if (millis() - state_timer >= (params.time_delay_mins * 60000UL)) {
                // Waktu delay aman telah tercapai: Hidupkan kompresor!
                current_fault     = FAULT_NONE; // Bersihkan riwayat kode error
                initial_temp_ntc2 = sys_temp2;
                is_flow_checked   = false;
                running_timer     = millis();
                orp_timer         = millis();

                current_state         = STATE_RUNNING;
                fault_condition_timer = millis();
                funDigitalWrite(PIN_RELAY, FUN_HIGH); // Nyalakan kompresor (Relay ON)
                serial_cli_log_str("[SYSTEM] STARTING COMPRESSOR -> RELAY ON");
            }
            break;
        }

        // --------------------------------------------------------------------
        // STATE 1: RUNNING (KOMPRESOR SEDANG BEKERJA NORMAL)
        // --------------------------------------------------------------------
        case STATE_RUNNING: {
            uint32_t run_duration = millis() - running_timer;

            // a. Pemantauan Tegangan Operasi (Over-Voltage OVP & Under-Voltage UVP)
            if (sys_volt_dv > (OVP_LIMIT * 10U)) {
                check_fault_delay(FAULT_OVP, params.ovp_uvp_delay);
            } else if (sys_volt_dv < (params.uvp_limit * 10U)) {
                check_fault_delay(FAULT_UVP, params.ovp_uvp_delay);
            } else {
                fault_condition_timer = millis();
            }

            // b. Pemantauan Arus: Jendela Inrush LRA vs Arus Beban Kerja (OCP/UCP)
            if (run_duration < (params.lra_time * 1000UL)) {
                // Selama jendela waktu start (LRA-Time 1-5 detik): periksa rotor terkunci (LRA)
                if (sys_amp_ma >= params.lra_limit_ma) {
                    if (millis() - fault_condition_timer >= (params.lra_time * 1000UL)) {
                        lra_retry_count++;
                        // Jika kegagalan start melampaui batas toleransi retry -> Kunci Sistem (LOCKOUT)!
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
                // Kompresor berhasil start melampaui jendela inrush: Reset akumulasi retry LRA!
                lra_retry_count = 0;

                // c. Proteksi Beban Lebih (Over-Current Protection - OCP)
                if (sys_amp_ma >= params.ocp_limit_ma) {
                    check_fault_delay(FAULT_OCP, params.ocp_delay);
                } else {
                    fault_condition_timer = millis();
                }

                // d. Proteksi Kebocoran Freon (Under-Current Protection - UCP)
                // Jika arus berada di antara arus lampu kulkas (S-12) dan batas bocor (S-11) selama 60 detik
                uint16_t ucp_ma = (uint16_t)params.ucp_limit_x10 * 10U;
                uint16_t min_ma = (uint16_t)params.min_current_threshold_x10 * 10U;
                if (ucp_ma > 0 && sys_amp_ma >= min_ma && sys_amp_ma <= ucp_ma) {
                    check_fault_delay(FAULT_UCP, 60);
                }
            }

            // e. Proteksi Jam Kerja Nonstop (Over-Run Protection - ORP)
            // Kompresor kulkas/AC yang menyala berjam-jam tanpa henti berisiko overheat atau termostat rusak
            if (sys_amp_ma < (params.min_current_threshold_x10 * 10)) {
                orp_timer = millis(); // Reset timer jika kompresor sedang cut-off oleh termostat
            } else if (millis() - orp_timer >= (params.orp_time_limit_hours * 3600000UL)) {
                // Jam kerja nonstop terlampaui: Paksa kompresor istirahat
                current_state = STATE_ORP_REST;
                state_timer   = millis();
                funDigitalWrite(PIN_RELAY, FUN_LOW);
                serial_cli_log_str("[SYS] ORP REST");
            }
            break;
        }

        // --------------------------------------------------------------------
        // STATE 2: FAULT (TERJADI GANGGUAN / TRIP AKTIF)
        // --------------------------------------------------------------------
        case STATE_FAULT: {
            funDigitalWrite(PIN_RELAY, FUN_LOW); // Pastikan relay selalu terputus
            bool ready_to_recover = false;

            // Batas tegangan pemulihan dengan histeresis (mencegah kompresor on-off cepat akibat ripple)
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

            // Jika kondisi kelistrikan telah pulih normal: Masuk kembali ke Startup Delay
            if (ready_to_recover) {
                current_state = STATE_STARTUP_DELAY;
                // Catatan: Nilai current_fault tetap dipertahankan selama masa delay countdown,
                // sehingga layar TM1637 bergantian menampilkan hitung mundur delay dan kode kesalahan sebelumnya!
                state_timer   = millis();
                serial_cli_log_str("[SYSTEM] Fault Recovered -> Restarting Startup Delay");
            }
            break;
        }

        // --------------------------------------------------------------------
        // STATE 4: ORP REST (WAKTU ISTIRAHAT KOMPRESOR NONSTOP)
        // --------------------------------------------------------------------
        case STATE_ORP_REST: {
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            if (millis() - state_timer >= (params.orp_rest_delay_mins * 60000UL)) {
                current_state = STATE_STARTUP_DELAY;
                state_timer   = millis();
            }
            break;
        }

        // --------------------------------------------------------------------
        // STATE 3: LOCKOUT (PENGUNCIAN SISTEM PERMANEN)
        // --------------------------------------------------------------------
        case STATE_LOCKOUT: {
            funDigitalWrite(PIN_RELAY, FUN_LOW);
            static uint32_t lockout_reset_timer = 0;
            uint16_t adc = sensors_read_adc(PIN_BUTTONS);

            // Buka Kunci Manual: Tahan tombol Menu/OK pada panel fisik selama 3 detik penuh
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

        // Perbarui visualisasi display 7-segment dan indikator LED / Buzzer
        ui_update_display((sys_volt_dv + 5) / 10, sys_amp_ma);
        handle_indicators();
    }
}
