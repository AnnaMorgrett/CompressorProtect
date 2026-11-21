#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>
#include <stdbool.h>
#include "eeprom_ob.h"

// ============================================================================
// PARAMETER SISTEM & STRUKTUR PENYIMPANAN MEMORI NON-VOLATILE
// ============================================================================
// Memori Option Bytes CH32V003 berukuran 26 byte.
// Struktur alokasi memori fisik:
// - Alamat 0      : 1 Byte Magic Number (0xAC) sebagai penanda inisialisasi
// - Alamat 1..22  : 22 Byte Struct Data `system_params_t`
// - Alamat 23     : 1 Byte Checksum CRC-8 untuk validasi integritas data
// Total memori terpakai = 24 Byte (tersisa cadangan 2 Byte di bawah 26 Byte).
// ============================================================================

// Fitur sensor suhu NTC dikunci hardware (SOP-16 tidak memiliki ADC di PC1/PC2)
#define NTC1_ENABLED 0
#define NTC2_ENABLED 0

typedef struct {
    // 1. Parameter Proteksi Arus & Kalibrasi (Field 16-bit ditaruh awal untuk alignment memori)
    uint16_t lra_limit_ma;              // S-06: Batas Arus Locked Rotor (800 - 5000 mA, Def: 1500)
    uint16_t ocp_limit_ma;              // S-09: Batas Arus Overload Running (300 - 4500 mA, Def: 800)
    uint16_t voltage_multiplier_x1000;  // C-1 : Faktor Pengali Kalibrasi Tegangan (1 - 5000, Def: 855)
    uint16_t current_multiplier_x1000;  // C-2 : Faktor Pengali Kalibrasi Arus (1 - 5000, Def: 470)

    // 2. Parameter Proteksi Tegangan (Volt)
    uint8_t  ovp_limit_div2;            // S-02: Batas Over-Voltage dibagi 2 (230-260V / 2 = 115-130, Def: 125 = 250V)
    uint8_t  uvp_limit;                 // S-03: Batas Under-Voltage (150 - 200 V, Def: 180V)

    // 3. Waktu Tunda Start & Mode Tampilan Running (Dipadatkan dalam 1 byte via Bitfield)
    uint8_t  time_delay_mins       : 4; // S-01: Delay Aman Kompresor Start (2 - 10 Menit, Def: 5)
    uint8_t  display_mode          : 4; // Mode Layar Running (1:rUn, 2:Volt, 3:Arus, 4:Suhu, 5:Alternate, Def: 5)

    uint8_t  ref_flow_delay_mins;       // S-17: Waktu Cek Aliran Freon NTC2 (2 - 10 Menit, Def: 5)
    uint8_t  orp_time_limit_hours;      // S-19: Batas Jam Kerja Nonstop ORP (2 - 12 Jam, Def: 7)

    // 4. Durasi Delay Trip (Dipadatkan dalam 1 byte)
    uint8_t  ocp_delay             : 4; // S-10: Toleransi Waktu Arus Lebih (2 - 10 Detik, Def: 4)
    uint8_t  ovp_uvp_delay         : 4; // S-04: Toleransi Waktu Gangguan OVP/UVP (3 - 10 Detik, Def: 5)

    uint8_t  voltage_hysteresis;        // S-05: Histeresis Pemulihan Tegangan (10 - 40 V, Def: 10)

    // 5. Konfigurasi Toleransi LRA (Dipadatkan dalam 1 byte)
    uint8_t  lra_time              : 4; // S-07: Jendela Toleransi Inrush Start LRA (1 - 5 Detik, Def: 2)
    uint8_t  lra_retry_limit       : 4; // S-08: Batas Percobaan Start Ulang LRA (2 - 5 Kali, Def: 3)

    // 6. Deteksi Kebocoran Freon (Under-Current Protection) & Ambang Deteksi Beban
    uint8_t  ucp_limit_x10;             // S-11: Batas Arus Minimal/Bocor dibagi 10 (0=OFF, 20-100 = 200-1000mA, Def: 0)
    uint8_t  min_current_threshold_x10; // S-12: Filter Arus Lampu Kulkas dibagi 10 (15-40 = 150-400mA, Def: 25 = 250mA)

    // 7. Proteksi Panas Suhu Kompresor (NTC1)
    uint8_t  otp_limit;                 // S-14: Batas Over-Temperature Bodi Kompresor (70 - 90 °C, Def: 85)
    uint8_t  otp_hysteresis;            // S-15: Histeresis Pemulihan Suhu OTP (5 - 20 °C, Def: 10)

    // 8. Siklus Proteksi Aliran & Istirahat Kompresor
    uint8_t  flow_fault_retry_limit;    // S-18: Batas Retry Aliran Freon Gagal (2 - 5 Kali, Def: 5)
    uint8_t  orp_rest_delay_mins;       // S-20: Durasi Istirahat Paksa Kompresor ORP (20 - 60 Menit, Def: 30)
    int8_t   temp_offset;               // C-3 : Offset Koreksi Suhu NTC (-20 s/d +20 °C, Def: 0)
} system_params_t;

// Memastikan ukuran struct tidak melampaui alokasi maksimal 24 byte di flash Option Bytes
_Static_assert(sizeof(system_params_t) <= 24, "system_params_t melebihi 24 byte (butuh 1 byte Magic + 1 byte CRC)!");

// Macro bantu untuk mengembalikan batas OVP asli dari nilai div2
#define OVP_LIMIT               (params.ovp_limit_div2 * 2)

// Batas pemutusan instan perangkat keras (tidak dapat diubah di menu demi keselamatan motor)
#define PARAM_POWER_SURGE_LIMIT 270 // Err1: Lonjakan ekstrem >= 270V (Trip seketika)
#define PARAM_BLACKOUT_LIMIT    140 // Err2: Padam/Anjlok ekstrem <= 140V (Trip seketika)

extern system_params_t params;

// Memuat parameter dari flash Option Bytes. Mengembalikan true jika CRC valid
bool params_load(void);

// Menyimpan seluruh isi struct `params` ke flash fisik beserta checksum CRC-8
void params_save(void);

// Mengembalikan pengaturan proteksi S-01 s/d S-20 ke default pabrik (Kalibrasi C1-C3 aman)
void params_reset_defaults(void);

// Mengembalikan khusus nilai kalibrasi C-1, C-2, C-3 ke nilai default pabrik
void params_reset_calibration(void);

#endif
