#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// MODUL ANTARMUKA PENGGUNA (USER INTERFACE - UI) & MANAJEMEN STATUS SISTEM
// ============================================================================
// Modul ini mengelola seluruh interaksi pengguna melalui:
// 1. Layar 4-digit 7-segment TM1637.
// 2. Tiga tombol fisik (Plus, Menu/OK, Minus) via pembacaan tangga resistor analog.
// 3. Mesin status proteksi (Startup Delay, Running, Fault, Lockout, ORP Rest).
// 4. Menu Konfigurasi Parameter (S-01 s/d S-20, SAUE, rSt).
// 5. Menu Kalibrasi Hardware (U-rt, A-rt, t-rt, C-1, C-2, C-3, SAUE, rSt).
// ============================================================================

/**
 * @brief Mode Operasi Utama Sistem (Dipilih saat power-on booting).
 */
typedef enum {
    MODE_NORMAL_PROTECTION = 0, // Mode Normal: Sistem mengawasi listrik dan melindungi kompresor
    MODE_CALIBRATION       = 1, // Mode Kalibrasi: Dipicu jika pin PC6 di-jumper ke GND saat booting
    MODE_SETTING           = 2  // Mode Setting: Dipicu jika tombol Menu ditahan saat booting
} operating_mode_t;

/**
 * @brief Siklus Mesin Status Proteksi Kompresor (State Machine).
 */
typedef enum {
    STATE_STARTUP_DELAY = 0, // Waktu tunda aman sebelum kompresor dinyalakan (menyeimbangkan tekanan refrigeran)
    STATE_RUNNING       = 1, // Kompresor bekerja normal (Relay ON) dengan pemantauan voltase, arus & suhu aktif
    STATE_FAULT         = 2, // Terjadi gangguan listrik/beban (Relay OFF instan, alarm aktif, menunggu listrik aman)
    STATE_LOCKOUT       = 3, // Terkunci permanen akibat retry proteksi terlampaui (butuh reset manual tahan tombol 3s)
    STATE_ORP_REST      = 4  // Waktu istirahat wajib setelah kompresor bekerja nonstop melampaui batas jam ORP
} system_state_t;

/**
 * @brief Daftar Kode Gangguan / Trip Proteksi (Fault Codes).
 *        Sesuai dengan Buku Panduan Pengguna resmi:
 */
typedef enum {
    FAULT_NONE     = 0, // Normal / Tidak ada gangguan
    FAULT_SURGE    = 1, // Err 1: Power Surge / Tegangan Petir Ekstrem (> 275V)
    FAULT_BLACKOUT = 2, // Err 2: Blackout / Pemadaman Listrik Mendadak (< 100V)
    FAULT_OVP      = 3, // Err 3: Over-Voltage Protection (Tegangan PLN di atas batas S-02)
    FAULT_UVP      = 4, // Err 4: Under-Voltage Protection (Tegangan PLN di bawah batas S-03)
    FAULT_LRA      = 5, // Err 5: Locked Rotor Amperes / Rotor Terkunci / Gagal Start (Arus start di atas S-06)
    FAULT_OCP      = 6, // Err 6: Over-Current Protection / Beban Lebih saat Running (Arus kerja di atas S-09)
    FAULT_UCP      = 7, // Err 7: Under-Current Protection / Kebocoran Freon (Arus terlalu rendah < S-11)
    FAULT_OTP      = 8, // Err 8: Over-Temperature Protection / Kompresor Terlalu Panas (> S-14)
    FAULT_REF_FLOW = 9  // Err 9: Refrigerant Flow Fault / Aliran Freon Buntu / Sirkulasi Macet
} fault_type_t;

// Variabel status global sistem
extern operating_mode_t current_mode;
extern system_state_t   current_state;
extern fault_type_t     current_fault;
extern uint32_t         state_timer;
extern uint8_t          lra_retry_count;
extern uint8_t          flow_retry_count;
extern uint16_t         sys_volt_dv;
extern uint16_t         sys_amp_ma;
extern int16_t          sys_temp1;
extern int16_t          sys_temp2;

// String 4-karakter mirror visual tampilan display aktif untuk telemetri serial
extern char             current_display_str[5];

/**
 * @brief Inisialisasi layar TM1637 dan menjalankan animasi teks selamat datang ("SCP " -> "PrO ").
 */
void    ui_init(void);

/**
 * @brief Memperbarui tampilan 7-segment sesuai dengan status mesin yang sedang berjalan:
 *        - Screen Saver (meredup otomatis setelah 10 detik).
 *        - Mode Running (5 mode pilihan: rUn, Volt, Arus, Suhu, atau Auto-Scroll).
 *        - Mode Fault (flip-flop bergantian antara ErrX dan nilai tegangan/suhu setiap 2 detik).
 *        - Mode Delay (countdown menit atau detik, bergantian dengan kode error sebelumnya).
 *        - Mode Lockout (flip-flop antara "LOC1"/"LOC2" dan "Err5"/"Err9").
 * @param current_v Nilai tegangan saat ini dalam Volt bulat.
 * @param current_ma Nilai arus saat ini dalam Milliampere.
 */
void    ui_update_display(uint16_t current_v, uint16_t current_ma);

/**
 * @brief Menangani navigasi antarmuka Mode Setting parameter (S-01 s/d S-20, SAUE, rSt).
 */
void    ui_handle_setting_mode(void);

/**
 * @brief Menangani antarmuka Mode Kalibrasi (U-rt, A-rt, t-rt, C-1, C-2, C-3, SAUE, rSt).
 */
void    ui_handle_calibration_mode(void);

/**
 * @brief Menampilkan teks " PC " pada layar 7-segment saat masuk ke Serial CLI Menu.
 */
void    ui_show_serial_menu_display(void);

/**
 * @brief Melakukan reset mikrokontroler secara software via register Watchdog.
 */
void    system_reboot(void);

/**
 * @brief Menghasilkan nada chirp singkat saat tombol ditekan (jika BUTTON_CHIRP diaktifkan).
 */
void    system_button_chirp(void);

/**
 * @brief Mengambil nilai parameter tertentu berdasarkan indeksnya (0 = S-01, 19 = S-20).
 * @param idx Indeks parameter (0 s/d 19).
 * @return Nilai parameter dalam skala desimal riil.
 */
int32_t ui_get_param_val(uint8_t idx);

/**
 * @brief Menetapkan nilai baru pada parameter tertentu berdasarkan indeksnya.
 * @param idx Indeks parameter (0 s/d 19).
 * @param val Nilai baru.
 */
void    ui_set_param_val(uint8_t idx, int32_t val);

#endif // UI_H
