#include "params.h"

// ============================================================================
// IMPLEMENTASI MANAJEMEN PARAMETER & VALIDASI INTEGRITAS CRC-8
// ============================================================================

system_params_t params;

#define EEPROM_MAGIC_ADDR 0
#define EEPROM_MAGIC_VAL  0xAC // Kode identitas unik versi struktur data firmware
#define EEPROM_DATA_ADDR  1
#define EEPROM_CRC_ADDR   (EEPROM_DATA_ADDR + sizeof(system_params_t))

// Menghitung checksum CRC-8 berbasis rotasi XOR untuk mendeteksi data korup/kosong
static uint8_t compute_params_crc(const system_params_t *p) {
    const uint8_t *bytes = (const uint8_t *)p;
    uint8_t crc = 0x5A; // Nilai seed awal
    for (size_t i = 0; i < sizeof(system_params_t); i++) {
        crc ^= bytes[i];
        crc = (uint8_t)((crc << 1) | (crc >> 7)); // Rotasi bit 8-bit
    }
    return crc;
}

// Verifikasi batasan nilai wajar parameter untuk mencegah data acak merusak kompresor
static bool are_params_valid(const system_params_t *p) {
    if (p->time_delay_mins < 2 || p->time_delay_mins > 10) return false;
    if (p->display_mode < 1 || p->display_mode > 5) return false;
    if (p->ovp_limit_div2 < 115 || p->ovp_limit_div2 > 130) return false; // 230 - 260 V
    if (p->uvp_limit < 150 || p->uvp_limit > 200) return false;
    if (p->lra_limit_ma < 800 || p->lra_limit_ma > 5000) return false;
    if (p->ocp_limit_ma < 300 || p->ocp_limit_ma > 4500) return false;
    if (p->voltage_multiplier_x1000 < 1 || p->voltage_multiplier_x1000 > 5000) return false;
    if (p->current_multiplier_x1000 < 1 || p->current_multiplier_x1000 > 5000) return false;
    return true;
}

// Mengembalikan parameter setting S-01 s/d S-20 ke nilai awal pabrik
// Catatan: Kalibrasi C-1, C-2, C-3 tetap dipertahankan saat menu rSt dieksekusi di setting
void params_reset_defaults(void) {
    params.time_delay_mins           = 5;    // S-01: Delay awal 5 menit
    params.display_mode              = 5;    // Mode tampilan: Auto Alternate Volt & Arus
    params.ref_flow_delay_mins       = 5;    // S-17: Delay cek aliran 5 menit
    params.orp_time_limit_hours      = 7;    // S-19: Batas kerja nonstop 7 jam
    params.voltage_multiplier_x1000  = 855;  // C-1 : Faktor pengali tegangan default 855
    params.current_multiplier_x1000  = 470;  // C-2 : Faktor pengali arus default 470
    params.temp_offset               = 0;    // C-3 : Offset koreksi suhu 0 °C
    params.ovp_limit_div2            = 125;  // S-02: Batas atas 250 V (125 * 2)
    params.uvp_limit                 = 180;  // S-03: Batas bawah 180 V
    params.ovp_uvp_delay             = 5;    // S-04: Delay trip OVP/UVP 5 detik
    params.voltage_hysteresis        = 10;   // S-05: Histeresis kembali aman 10 V
    params.lra_limit_ma              = 1500; // S-06: Arus start macet LRA 1500 mA (1.5 A)
    params.lra_time                  = 2;    // S-07: Toleransi inrush start 2 detik
    params.lra_retry_limit           = 3;    // S-08: Maksimal 3 kali percobaan start sebelum Lockout
    params.ocp_limit_ma              = 800;  // S-09: Batas arus lebih kerja 800 mA (0.8 A)
    params.ocp_delay                 = 4;    // S-10: Delay trip OCP 4 detik
    params.ucp_limit_x10             = 0;    // S-11: Deteksi kebocoran freon dinonaktifkan (0)
    params.min_current_threshold_x10 = 25;   // S-12: Filter arus lampu kulkas 250 mA
    params.otp_limit                 = 85;   // S-14: Batas suhu maksimal bodi 85 °C
    params.otp_hysteresis            = 10;   // S-15: Histeresis pemulihan suhu 10 °C
    params.flow_fault_retry_limit    = 5;    // S-18: Maksimal 5 kali retry sirkulasi freon
    params.orp_rest_delay_mins       = 30;   // S-20: Durasi istirahat paksa 30 menit
    params_save();
}

// Khusus mereset parameter kalibrasi C-1, C-2, C-3 di menu CAL
void params_reset_calibration(void) {
    params.voltage_multiplier_x1000 = 855; // 0.855
    params.current_multiplier_x1000 = 470; // 0.470
    params.temp_offset = 0;
    params_save();
}

bool params_load(void) {
    eeprom_begin();
    // 1. Cek apakah byte penanda (Magic Val) sesuai
    if (eeprom_read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VAL) {
        system_params_t temp;
        eeprom_get(EEPROM_DATA_ADDR, &temp, sizeof(temp));
        uint8_t stored_crc = eeprom_read(EEPROM_CRC_ADDR);

        // 2. Cek apakah checksum CRC cocok dan nilainya berada dalam rentang wajar
        if (stored_crc == compute_params_crc(&temp) && are_params_valid(&temp)) {
            params = temp; // Data terverifikasi aman & utuh
            return true;
        }
    }
    // Jika chip baru/kosong atau data korup, muat pengaturan pabrik aman
    params_reset_defaults();
    return false;
}

void params_save(void) {
    uint8_t crc = compute_params_crc(&params);
    eeprom_put(EEPROM_DATA_ADDR, &params, sizeof(params));
    eeprom_write(EEPROM_CRC_ADDR, crc);
    eeprom_update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
}
