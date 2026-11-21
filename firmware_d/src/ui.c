#include "ui.h"
#include "tm1657.h"
#include "params.h"
#include "sensors.h"
#include "board.h"
#include "sys_time.h"
#include <stddef.h>

// ============================================================================
// IMPLEMENTASI USER INTERFACE (DISPLAY 7-SEGMENT & TOMBOL PANEL)
// ============================================================================

// Pola segmen pesan visual statis
static const uint8_t MSG_SCP[]  = {LTR_S, LTR_C, LTR_P, BLANK};          // "SCP "
static const uint8_t MSG_PRO[]  = {LTR_P, LTR_r, LTR_O, BLANK};          // "PrO "
static const uint8_t MSG_SETT[] = {LTR_S, LTR_E, LTR_t, LTR_t};          // "SEtt"
static const uint8_t MSG_SAVE[] = {LTR_S, LTR_A, LTR_U, LTR_E};          // "SAUE"
static const uint8_t MSG_OK[]   = {LTR_O, LTR_K, BLANK, BLANK};          // "OK  "
static const uint8_t MSG_RST[]  = {LTR_r, LTR_S, LTR_t, BLANK};          // "rSt "
static const uint8_t MSG_CAL[]  = {LTR_C, LTR_A, LTR_L, BLANK};          // "CAL "
static const uint8_t MSG_URT[]  = {LTR_U, SEG_G, LTR_r, LTR_t};          // "U-rt" (Live Volt)
static const uint8_t MSG_ART[]  = {LTR_A, SEG_G, LTR_r, LTR_t};          // "A-rt" (Live Ampere)
static const uint8_t MSG_TRT[]  = {LTR_t, SEG_G, LTR_r, LTR_t};          // "t-rt" (Live Temp)
static const uint8_t MSG_C1[]   = {LTR_C, SEG_G, BLANK, 0x06};           // "C- 1" (Tegangan)
static const uint8_t MSG_C2[]   = {LTR_C, SEG_G, BLANK, 0x5B};           // "C- 2" (Arus)
static const uint8_t MSG_C3[]   = {LTR_C, SEG_G, BLANK, 0x4F};           // "C- 3" (Suhu)
static const uint8_t MSG_PC[]   = {BLANK, LTR_P, LTR_C, BLANK};          // " PC " (Serial CLI)

// Struktur metadata konfigurasi rentang parameter (Min, Max, Step, dan opsi nilai 0=OFF)
typedef struct {
    int32_t min_ui;   // Nilai batas minimum
    int32_t max_ui;   // Nilai batas maksimum
    int32_t step_ui;  // Nilai kenaikan/penurunan tiap penekanan tombol
    bool    zero_off; // True jika nilai 0 menandakan fitur dinonaktifkan (OFF)
} param_meta_t;

// Tabel batas nilai parameter S-01 s/d S-20 sesuai Panduan Pengguna resmi
static const param_meta_t PARAM_META[20] = {
    {2,    10,   1,   false}, // 0: S-01 Waktu Tunda Start (2 - 10 Menit, Step 1)
    {230,  260,  10,  false}, // 1: S-02 Batas Tegangan Atas OVP (230 - 260 V, Step 10)
    {150,  200,  10,  false}, // 2: S-03 Batas Tegangan Bawah UVP (150 - 200 V, Step 10)
    {3,    10,   1,   false}, // 3: S-04 Toleransi Waktu Tegangan (3 - 10 Detik, Step 1)
    {10,   40,   1,   false}, // 4: S-05 Histeresis Pemulihan Tegangan (10 - 40 V, Step 1)
    {800,  5000, 100, false}, // 5: S-06 Batas Arus Locked Rotor LRA (800 - 5000 mA, Step 100)
    {1,    5,    1,   false}, // 6: S-07 Jendela Inrush LRA (1 - 5 Detik, Step 1)
    {2,    5,    1,   false}, // 7: S-08 Batas Percobaan Start Ulang LRA (2 - 5 Kali, Step 1)
    {300,  4500, 50,  false}, // 8: S-09 Batas Arus Beban Lebih OCP (300 - 4500 mA, Step 50)
    {2,    10,   1,   false}, // 9: S-10 Toleransi Waktu Beban Lebih OCP (2 - 10 Detik, Step 1)
    {200,  1000, 50,  true }, // 10: S-11 Batas Deteksi Bocor Freon UCP (0=OFF, 200 - 1000 mA, Step 50)
    {150,  400,  50,  true }, // 11: S-12 Ambang Arus Minimal / Filter Lampu (0=OFF, 150 - 400 mA, Step 50)
    {0,    0,    1,   false}, // 12: S-13 Sensor NTC1 Bodi Kompresor (Kunci 0 pada SOP-16)
    {70,   90,   1,   false}, // 13: S-14 Batas Panas Bodi OTP (70 - 90 °C, Step 1)
    {5,    20,   1,   false}, // 14: S-15 Histeresis Pemulihan OTP (5 - 20 °C, Step 1)
    {0,    0,    1,   false}, // 15: S-16 Sensor NTC2 Pipa Filter Dryer (Kunci 0 pada SOP-16)
    {2,    10,   1,   false}, // 16: S-17 Waktu Cek Aliran Freon (2 - 10 Menit, Step 1)
    {2,    5,    1,   false}, // 17: S-18 Batas Percobaan Aliran Freon (2 - 5 Kali, Step 1)
    {2,    12,   1,   false}, // 18: S-19 Batas Kerja Kompresor Nonstop ORP (2 - 12 Jam, Step 1)
    {20,   60,   1,   false}  // 19: S-20 Waktu Istirahat Kompresor ORP (20 - 60 Menit, Step 1)
};

/**
 * @brief Mengambil nilai parameter riil dari memori struct sistem berdasarkan indeks menu.
 */
int32_t ui_get_param_val(uint8_t idx) {
    switch (idx) {
        case 0:  return params.time_delay_mins;
        case 1:  return params.ovp_limit_div2 * 2;
        case 2:  return params.uvp_limit;
        case 3:  return params.ovp_uvp_delay;
        case 4:  return params.voltage_hysteresis;
        case 5:  return params.lra_limit_ma;
        case 6:  return params.lra_time;
        case 7:  return params.lra_retry_limit;
        case 8:  return params.ocp_limit_ma;
        case 9:  return params.ocp_delay;
        case 10: return params.ucp_limit_x10 * 10;
        case 11: return params.min_current_threshold_x10 * 10;
        case 12: return 0;
        case 13: return params.otp_limit;
        case 14: return params.otp_hysteresis;
        case 15: return 0;
        case 16: return params.ref_flow_delay_mins;
        case 17: return params.flow_fault_retry_limit;
        case 18: return params.orp_time_limit_hours;
        case 19: return params.orp_rest_delay_mins;
        default: return 0;
    }
}

/**
 * @brief Menyimpan nilai parameter baru ke dalam struct sistem sesuai indeks menu.
 */
void ui_set_param_val(uint8_t idx, int32_t val) {
    switch (idx) {
        case 0:  params.time_delay_mins = (uint8_t)val; break;
        case 1:  params.ovp_limit_div2 = (uint8_t)(val / 2); break;
        case 2:  params.uvp_limit = (uint8_t)val; break;
        case 3:  params.ovp_uvp_delay = (uint8_t)val; break;
        case 4:  params.voltage_hysteresis = (uint8_t)val; break;
        case 5:  params.lra_limit_ma = (uint16_t)val; break;
        case 6:  params.lra_time = (uint8_t)val; break;
        case 7:  params.lra_retry_limit = (uint8_t)val; break;
        case 8:  params.ocp_limit_ma = (uint16_t)val; break;
        case 9:  params.ocp_delay = (uint8_t)val; break;
        case 10: params.ucp_limit_x10 = (uint8_t)(val / 10); break;
        case 11: params.min_current_threshold_x10 = (uint8_t)(val / 10); break;
        case 12: break; // Terkunci hardware
        case 13: params.otp_limit = (uint8_t)val; break;
        case 14: params.otp_hysteresis = (uint8_t)val; break;
        case 15: break; // Terkunci hardware
        case 16: params.ref_flow_delay_mins = (uint8_t)val; break;
        case 17: params.flow_fault_retry_limit = (uint8_t)val; break;
        case 18: params.orp_time_limit_hours = (uint8_t)val; break;
        case 19: params.orp_rest_delay_mins = (uint8_t)val; break;
    }
}

/**
 * @brief Menyesuaikan nilai parameter naik atau turun sesuai arah tombol (+1 / -1)
 *        sambil memastikan batas minimum, maksimum, dan opsi nonaktif (0=OFF).
 */
static int32_t step_param_val(uint8_t idx, int32_t value, int8_t direction) {
    const param_meta_t *d = &PARAM_META[idx];
    if (d->min_ui == d->max_ui) return d->min_ui;

    if (direction > 0) {
        if (d->zero_off && value == 0) value = d->min_ui;
        else value += d->step_ui;
    } else {
        value -= d->step_ui;
        if (d->zero_off && value < d->min_ui) value = 0;
    }
    // Batasi dalam rentang yang valid
    if (!(d->zero_off && value == 0)) {
        if (value < d->min_ui) value = d->min_ui;
        if (value > d->max_ui) value = d->max_ui;
    }
    return value;
}

// Buffer string mirror tampilan aktif untuk dikirim via telemetri serial
char current_display_str[5] = "----";

/**
 * @brief Mengatur 4 karakter mirror tampilan aktif.
 */
static void set_display4(char c0, char c1, char c2, char c3) {
    current_display_str[0] = c0;
    current_display_str[1] = c1;
    current_display_str[2] = c2;
    current_display_str[3] = c3;
    current_display_str[4] = '\0';
}

/**
 * @brief Mengatur string mirror berdasarkan angka integer desimal dengan perataan kanan.
 */
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

/**
 * @brief Menampilkan format kode kesalahan "ErrX" pada layar 7-segment.
 * @param code Nomor kesalahan (1..9).
 */
static void show_err_display(uint8_t code) {
    uint8_t data_err[4] = {LTR_E, LTR_r, LTR_r, DIGIT_SEG[code % 10]};
    tm1637_show_segments(data_err);
    set_display4('E', 'r', 'r', '0' + (code % 10));
}

/**
 * @brief Menampilkan animasi booting selamat datang: "SCP " (1.5 detik) -> "PrO " (1.0 detik).
 */
void ui_init(void) {
    tm1637_init();
    tm1637_set_brightness(5);
    tm1637_show_segments(MSG_SCP); // Tampilkan "SCP "
    Delay_Ms(1500);
    tm1637_show_segments(MSG_PRO); // Tampilkan "PrO "
    Delay_Ms(1000);
    static const uint8_t CLR[4] = {BLANK, BLANK, BLANK, BLANK};
    tm1637_show_segments(CLR);
}

/**
 * @brief Menampilkan teks " PC " saat antarmuka serial CLI mengambil alih kendali.
 */
void ui_show_serial_menu_display(void) {
    tm1637_set_brightness(4);
    tm1637_show_segments(MSG_PC);
    set_display4(' ', 'P', 'C', ' ');
}

/**
 * @brief Mengatur konten tampilan 7-segment berdasarkan status aktif sistem.
 *        - Screen Saver otomatis: Kecerahan 4 selama 10 detik pertama, lalu meredup ke level 1.
 *        - Mode RUNNING: Mendukung 5 mode tampilan yang dapat dipindah dengan tombol Menu.
 *        - Mode FAULT: Layar bergantian (flip-flop setiap 2 detik) antara "ErrX" dan nilai sensor abnormal.
 *        - Mode DELAY: Hitung mundur sisa waktu aman kompresor.
 *        - Mode LOCKOUT: Tampilan "LOC1" atau "LOC2" bergantian dengan kode error.
 */
void ui_update_display(uint16_t current_v, uint16_t current_ma) {
    static uint32_t bright_timer = 0;
    static bool     bright_init  = false;
    static fault_type_t   last_fault = FAULT_NONE;
    static system_state_t last_state = STATE_STARTUP_DELAY;

    uint32_t now = millis();
    if (!bright_init) {
        bright_timer = now;
        bright_init = true;
    }

    // Bangunkan kecerahan penuh (Screen Saver reset) jika terjadi pergantian status/error
    if (current_fault != last_fault) {
        last_fault = current_fault;
        if (current_fault != FAULT_NONE) bright_timer = now;
    }
    if (current_state != last_state) {
        last_state = current_state;
        if (current_state == STATE_STARTUP_DELAY) bright_timer = now;
    }

    // Fitur Screen Saver: Terang (level 4) selama 10 detik, lalu redup (level 1) untuk menghemat daya
    tm1637_set_brightness((now - bright_timer <= 10000UL) ? 4 : 1);

    // Timer flip-flop 2 detik untuk tampilan bergantian (Alternating Screen)
    bool is_alt_screen = ((now / 2000) % 2) == 1;

    // Hitung sisa waktu mundur dalam satuan detik
    int32_t remaining_sec = 0;
    if (current_state == STATE_STARTUP_DELAY) {
        remaining_sec = (int32_t)(params.time_delay_mins * 60000UL) - (int32_t)(now - state_timer);
    } else if (current_state == STATE_ORP_REST) {
        remaining_sec = (int32_t)(params.orp_rest_delay_mins * 60000UL) - (int32_t)(now - state_timer);
    }
    if (remaining_sec < 0) remaining_sec = 0;
    remaining_sec /= 1000;

    // ------------------------------------------------------------------------
    // 1. STATE_RUNNING: 5 Mode Tampilan Pengguna
    // ------------------------------------------------------------------------
    if (current_state == STATE_RUNNING) {
        static uint8_t  last_run_btn = 0;
        static uint32_t run_btn_press_time = 0;

        // Pembacaan tombol Menu saat running untuk mengganti mode tampilan
        uint16_t adc = sensors_read_adc(PIN_BUTTONS);
        uint8_t raw_btn = (adc >= 240 && adc <= 420) ? 2 : 0; // Tombol Menu/OK

        if (raw_btn != last_run_btn) {
            if (raw_btn == 2) {
                run_btn_press_time = now;
            } else if (last_run_btn == 2 && (now - run_btn_press_time > 50)) {
                // Tekan singkat tombol Menu -> siklus ganti mode tampilan (1..5)
                bright_timer = now;
                params.display_mode++;
                if (params.display_mode == 4 && !NTC1_ENABLED) {
                    params.display_mode = 5; // Lewati mode suhu jika NTC tidak aktif
                }
                if (params.display_mode > 5) {
                    params.display_mode = 1;
                }
                params_save(); // Simpan preferensi mode tampilan ke flash
                system_button_chirp();
            }
            last_run_btn = raw_btn;
        }

        uint8_t active_mode = params.display_mode;
        if (active_mode < 1 || active_mode > 5) active_mode = 5;
        if (active_mode == 4 && !NTC1_ENABLED)  active_mode = 5;

        uint8_t view = active_mode;
        // Mode 5: Auto-scroll bergantian tiap 2 detik (Volt -> Arus -> Suhu)
        if (active_mode == 5) {
            uint8_t total_screens = NTC1_ENABLED ? 3 : 2;
            uint8_t step = (now / 2000) % total_screens;
            if (step == 0)      view = 2; // Tampilkan Tegangan
            else if (step == 1) view = 3; // Tampilkan Arus
            else                view = 4; // Tampilkan Suhu
        }

        if (view == 1) {
            // Mode 1: Tampilan teks statis "rUn "
            uint8_t data_run[4] = {LTR_r, LTR_U, LTR_n, BLANK};
            tm1637_show_segments(data_run);
            set_display4('r', 'U', 'n', ' ');
        } else if (view == 2) {
            // Mode 2: Tampilan Tegangan AC "xxxU" (misal "220U")
            int v = current_v;
            uint8_t data_v[4] = {
                v > 99 ? DIGIT_SEG[v / 100] : BLANK,
                v > 9  ? DIGIT_SEG[(v / 10) % 10] : BLANK,
                DIGIT_SEG[v % 10],
                LTR_U
            };
            tm1637_show_segments(data_v);
            set_display4((v >= 100) ? ('0' + v / 100) : ' ', (v >= 10) ? ('0' + (v / 10) % 10) : ' ', '0' + v % 10, 'U');
        } else if (view == 3) {
            // Mode 3: Tampilan Arus Beban dalam satuan Milliampere (misal " 850")
            tm1637_show_number(current_ma);
            set_display_num(current_ma);
        } else if (view == 4) {
            // Mode 4: Tampilan Suhu Kompresor " xxC" (misal " 25C")
            int t = sys_temp1;
            if (t < 0) t = 0;
            uint8_t data_t[4] = {
                t > 99 ? DIGIT_SEG[t / 100] : BLANK,
                t > 9  ? DIGIT_SEG[(t / 10) % 10] : BLANK,
                DIGIT_SEG[t % 10],
                LTR_C
            };
            tm1637_show_segments(data_t);
            set_display4((t >= 100) ? ('0' + t / 100) : ' ', (t >= 10) ? ('0' + (t / 10) % 10) : ' ', '0' + t % 10, 'C');
        }
    }
    // ------------------------------------------------------------------------
    // 2. STATE_FAULT: Tampilan Bergantian antara "ErrX" dan Nilai Sensor
    // ------------------------------------------------------------------------
    else if (current_state == STATE_FAULT) {
        if (is_alt_screen && (current_fault == FAULT_OVP || current_fault == FAULT_UVP ||
                              current_fault == FAULT_SURGE || current_fault == FAULT_BLACKOUT)) {
            // Tampilkan nilai tegangan abnormal penyebab gangguan
            int v = current_v;
            uint8_t data_v[4] = {
                v > 99 ? DIGIT_SEG[v / 100] : BLANK,
                v > 9  ? DIGIT_SEG[(v / 10) % 10] : BLANK,
                DIGIT_SEG[v % 10],
                LTR_U
            };
            tm1637_show_segments(data_v);
            set_display4((v >= 100) ? ('0' + v / 100) : ' ', (v >= 10) ? ('0' + (v / 10) % 10) : ' ', '0' + v % 10, 'U');
        } else if (is_alt_screen && current_fault == FAULT_OTP) {
            // Tampilkan nilai suhu berlebih
            int t = sys_temp1;
            if (t < 0) t = 0;
            uint8_t data_t[4] = {
                t > 99 ? DIGIT_SEG[t / 100] : BLANK,
                t > 9  ? DIGIT_SEG[(t / 10) % 10] : BLANK,
                DIGIT_SEG[t % 10],
                LTR_C
            };
            tm1637_show_segments(data_t);
            set_display4((t >= 100) ? ('0' + t / 100) : ' ', (t >= 10) ? ('0' + (t / 10) % 10) : ' ', '0' + t % 10, 'C');
        } else {
            // Tampilkan kode error (misal "Err3")
            show_err_display(current_fault);
        }
    }
    // ------------------------------------------------------------------------
    // 3. STARTUP DELAY & ORP REST: Hitung Mundur Sisa Waktu Tunda Aman
    // ------------------------------------------------------------------------
    else if (current_state == STATE_STARTUP_DELAY || current_state == STATE_ORP_REST) {
        if (remaining_sec > 60) {
            // Jika sisa waktu > 1 menit: Tampilkan menit, dan jika sedang pulih dari gangguan,
            // bergantian setiap 2 detik dengan kode kesalahan sebelumnya (misal "Err3" <-> "   5")
            if (is_alt_screen && current_fault != FAULT_NONE) {
                show_err_display(current_fault);
            } else {
                int m = (remaining_sec / 60) + 1;
                tm1637_show_number(m);
                set_display4(' ', 'd', (m >= 10) ? ('0' + m / 10) : ' ', '0' + m % 10);
            }
        } else {
            // Jika sisa waktu <= 60 detik: Tampilkan hitung mundur detik presisi
            tm1637_show_number(remaining_sec);
            int s = remaining_sec;
            set_display4(' ', 's', (s >= 10) ? ('0' + s / 10) : ' ', '0' + s % 10);
        }
    }
    // ------------------------------------------------------------------------
    // 4. STATE_LOCKOUT: Penguncian Sistem Permanen
    // ------------------------------------------------------------------------
    else if (current_state == STATE_LOCKOUT) {
        if (is_alt_screen) {
            show_err_display(current_fault);
        } else {
            // LOC1 = Penguncian akibat rotor macet berulang (LRA Retry Exceeded)
            // LOC2 = Penguncian akibat sirkulasi freon buntu berulang (Flow Retry Exceeded)
            uint8_t loc_reason = (current_fault == FAULT_LRA) ? 1 : 2;
            uint8_t data_loc[4] = {LTR_L, LTR_O, LTR_C, DIGIT_SEG[loc_reason]};
            tm1637_show_segments(data_loc);
            set_display4('L', 'O', 'C', '0' + loc_reason);
        }
    }
}

// Struktur event tombol fisik setelah proses debouncing
typedef struct {
    uint8_t raw_btn;          // 0: Lepas, 1: Plus, 2: Menu/OK, 3: Minus
    uint8_t click_event;      // Nomor tombol yang baru dilepas setelah ditekan singkat
    bool    long_press_event; // True jika tombol ditekan lama (> 1000 ms)
} button_events_t;

/**
 * @brief Membaca tangga resistor analog tombol pada pin PD6 (AIN6).
 *        Ambang batas ADC 10-bit:
 *        - Plus  (+)  : ADC < 240
 *        - Menu  (OK) : ADC 240 s/d 420
 *        - Minus (-)  : ADC 420 s/d 650
 *        - Dilepas    : ADC > 650 (~1023 Pull-up)
 */
static button_events_t read_buttons(uint8_t *last_btn, uint32_t *press_time, bool *long_handled) {
    button_events_t e = {0, 0, false};
    uint16_t adc = sensors_read_adc(PIN_BUTTONS);

    if (adc < 240)                     e.raw_btn = 1; // Tombol Plus
    else if (adc >= 240 && adc <= 420) e.raw_btn = 2; // Tombol Menu / OK
    else if (adc >= 420 && adc <= 650) e.raw_btn = 3; // Tombol Minus

    uint32_t now = millis();
    // Transisi penekanan atau pelepasan tombol
    if (e.raw_btn != *last_btn) {
        if (e.raw_btn != 0) {
            *press_time = now;
            *long_handled = false;
            system_button_chirp();
        } else if (!(*long_handled) && (now - *press_time > 50)) {
            // Tombol dilepas setelah penekanan singkat > 50 ms (Debounce Click)
            e.click_event = *last_btn;
        }
        *last_btn = e.raw_btn;
    } else if (e.raw_btn == 2 && !(*long_handled) && (now - *press_time > 1000)) {
        // Tombol Menu ditahan lebih dari 1 detik (Long Press Event)
        e.long_press_event = true;
        *long_handled = true;
    }
    return e;
}

/**
 * @brief Mesin Status Antarmuka Mode Setting (S-01 s/d S-20, SAUE, rSt).
 *        State 0: Tampilkan "SEtt", tunggu tombol Menu dilepas
 *        State 1: Tunggu klik tombol Menu untuk masuk ke daftar parameter
 *        State 2: Navigasi nomor menu S-01..S-20, SAUE, rSt via tombol Plus/Minus
 *        State 3: Pengeditan nilai parameter via tombol Plus/Minus, konfirmasi via Menu
 */
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
        if (btn.raw_btn == 0) menu_state = 1; // Tunggu tombol booting dilepas
        break;
    case 1:
        tm1637_show_segments(MSG_SETT);
        if (btn.click_event == 2) menu_state = 2; // Masuk ke daftar menu
        break;
    case 2:
        // Tampilkan label menu aktif (S-01 .. S-20, SAUE, rSt)
        if (current_menu <= 20) {
            uint8_t data_s[4] = {
                LTR_S, SEG_G,
                (current_menu >= 10) ? DIGIT_SEG[current_menu / 10] : BLANK,
                DIGIT_SEG[current_menu % 10]
            };
            tm1637_show_segments(data_s);
        } else if (current_menu == 21) tm1637_show_segments(MSG_SAVE); // "SAUE"
        else if (current_menu == 22)   tm1637_show_segments(MSG_RST);  // "rSt "

        // Navigasi nomor menu
        if (btn.click_event == 1 && current_menu < 22) current_menu++;
        if (btn.click_event == 3 && current_menu > 1)  current_menu--;

        // Pilihan menu dengan tombol Menu/OK
        if (btn.click_event == 2) {
            if (current_menu == 21 || current_menu == 22) {
                tm1637_show_segments(MSG_OK);
                if (current_menu == 21) params_save();           // Simpan parameter ke flash
                else                    params_reset_defaults(); // Reset ke konfigurasi pabrik
                Delay_Ms(1000);
                system_reboot(); // Restart mikrokontroler agar parameter baru aktif
            } else {
                // Masuk ke mode edit parameter yang dipilih
                edit_value = ui_get_param_val(current_menu - 1);
                menu_state = 3;
            }
        }
        break;
    case 3: {
        // Tampilkan nilai numerik parameter yang sedang diedit
        tm1637_show_number(edit_value);
        uint8_t idx = current_menu - 1;

        if (btn.click_event == 1) edit_value = step_param_val(idx, edit_value, +1);
        if (btn.click_event == 3) edit_value = step_param_val(idx, edit_value, -1);

        // Konfirmasi nilai dengan tombol Menu/OK atau tahan tombol
        if (btn.click_event == 2 || btn.long_press_event) {
            ui_set_param_val(idx, edit_value);
            menu_state = 2; // Kembali ke daftar menu
        }
        break;
    }
    }
}

/**
 * @brief Mesin Status Antarmuka Mode Kalibrasi Hardware:
 *        1: U-rt (Live Voltage)
 *        2: A-rt (Live Current)
 *        3: t-rt (Live Temp)
 *        4: C- 1 (Faktor Pengali Tegangan, Default: 855)
 *        5: C- 2 (Faktor Pengali Arus, Default: 470)
 *        6: C- 3 (Offset Kalibrasi Suhu, Default: 0)
 *        7: SAUE (Simpan hasil kalibrasi ke flash)
 *        8: rSt  (Reset faktor kalibrasi ke nilai bawaan pabrik)
 */
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
        if (btn.raw_btn == 0) menu_state = 1; // Tunggu tombol booting dilepas
        break;
    case 1:
        tm1637_show_segments(MSG_CAL);
        if (btn.click_event == 2) menu_state = 2; // Masuk ke daftar menu kalibrasi
        break;
    case 2:
        // Tampilkan item menu kalibrasi
        if      (current_menu == 1) tm1637_show_segments(MSG_URT);  // "U-rt"
        else if (current_menu == 2) tm1637_show_segments(MSG_ART);  // "A-rt"
        else if (current_menu == 3) tm1637_show_segments(MSG_TRT);  // "t-rt"
        else if (current_menu == 4) tm1637_show_segments(MSG_C1);   // "C- 1"
        else if (current_menu == 5) tm1637_show_segments(MSG_C2);   // "C- 2"
        else if (current_menu == 6) tm1637_show_segments(MSG_C3);   // "C- 3"
        else if (current_menu == 7) tm1637_show_segments(MSG_SAVE); // "SAUE"
        else if (current_menu == 8) tm1637_show_segments(MSG_RST);  // "rSt "

        // Navigasi menu kalibrasi
        if (btn.click_event == 1 && current_menu < 8) current_menu++;
        if (btn.click_event == 3 && current_menu > 1) current_menu--;

        if (btn.click_event == 2) {
            if (current_menu == 7) {
                // Simpan kalibrasi
                tm1637_show_segments(MSG_OK);
                params_save();
                Delay_Ms(1000);
                menu_state = 2;
            } else if (current_menu == 8) {
                // Reset kalibrasi C-1, C-2, C-3 ke default
                tm1637_show_segments(MSG_OK);
                params_reset_calibration();
                Delay_Ms(1000);
                menu_state = 2;
            } else {
                // Masuk ke pembacaan live atau pengeditan faktor
                if      (current_menu == 4) edit_value = params.voltage_multiplier_x1000;
                else if (current_menu == 5) edit_value = params.current_multiplier_x1000;
                else if (current_menu == 6) edit_value = params.temp_offset;
                menu_state = 3;
            }
        }
        break;
    case 3: {
        static uint32_t last_live_update = 0;
        static int displayed_live_val = 0;

        // Pembaruan live reading sensor setiap 250 ms
        if (millis() - last_live_update >= 250) {
            last_live_update = millis();
            if      (current_menu == 1) displayed_live_val = (sys_volt_dv + 5) / 10;
            else if (current_menu == 2) displayed_live_val = sys_amp_ma;
            else if (current_menu == 3) displayed_live_val = sys_temp1;
            else if (current_menu == 4 || current_menu == 5 || current_menu == 6) displayed_live_val = edit_value;
        }
        tm1637_show_number(displayed_live_val);

        // Pengeditan faktor C-1 & C-2 (rentang 1..5000)
        if (current_menu == 4 || current_menu == 5) {
            if (btn.click_event == 1) edit_value += 1;
            if (btn.click_event == 3) edit_value -= 1;
            if (edit_value > 5000) edit_value = 5000;
            if (edit_value < 1)    edit_value = 1;
        } else if (current_menu == 6) {
            // Pengeditan offset suhu C-3 (rentang -20 s/d +20 °C)
            if (btn.click_event == 1) edit_value += 1;
            if (btn.click_event == 3) edit_value -= 1;
            if (edit_value > 20)  edit_value = 20;
            if (edit_value < -20) edit_value = -20;
        }

        // Konfirmasi nilai kalibrasi
        if (btn.click_event == 2 || btn.long_press_event) {
            if (current_menu == 4) params.voltage_multiplier_x1000 = (uint16_t)edit_value;
            if (current_menu == 5) params.current_multiplier_x1000 = (uint16_t)edit_value;
            if (current_menu == 6) params.temp_offset = (int8_t)edit_value;
            menu_state = 2; // Kembali ke menu kalibrasi
        }
        break;
    }
    }
}
