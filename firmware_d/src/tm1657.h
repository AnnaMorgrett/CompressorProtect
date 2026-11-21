#ifndef TM1657_H
#define TM1657_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// DRIVER DISPLAY 7-SEGMENT TM1637 (4-DIGIT DENGAN PROTOKOL SERIAL KHUSUS)
// ============================================================================
// TM1637 adalah IC pengendali LED display 7-segment 4 digit atau lebih.
// Protokol komunikasinya menyerupai I2C (dua kawat: CLK dan DIO), namun tanpa
// pengalamatan perangkat 7-bit standar (data dikirim LSB first, dan ACK dibaca
// pada pulsa clock ke-9).
// ============================================================================

// --- Definisi Bit Segmen Standar 7-Segment (Common Anode / Kathode TM1637) ---
// Format byte per digit: [DP, G, F, E, D, C, B, A]
//  -- A --
// |       |
// F       B
// |       |
//  -- G --
// |       |
// E       C
// |       |
//  -- D --   (DP)
#define SEG_A  0b00000001 // Segmen atas horizontal
#define SEG_B  0b00000010 // Segmen kanan atas vertikal
#define SEG_C  0b00000100 // Segmen kanan bawah vertikal
#define SEG_D  0b00001000 // Segmen bawah horizontal
#define SEG_E  0b00010000 // Segmen kiri bawah vertikal
#define SEG_F  0b00100000 // Segmen kiri atas vertikal
#define SEG_G  0b01000000 // Segmen tengah horizontal
#define SEG_DP 0b10000000 // Titik desimal / colon

// Nilai kosong (semua segmen padam)
#define BLANK  0x00

// --- Karakter / Huruf Khusus untuk Antarmuka Tampilan Sistem ---
// Huruf 'U' untuk satuan tegangan (misal: "220U")
#define LTR_U (SEG_B | SEG_C | SEG_D | SEG_E | SEG_F)

// Huruf 'E' untuk kode Error (misal: "Err1")
#define LTR_E (SEG_A | SEG_D | SEG_E | SEG_F | SEG_G)

// Huruf 'r' kecil untuk "run", "Err", atau kalibrasi "U-rt"
#define LTR_r (SEG_E | SEG_G)

// Huruf 'L' untuk Lockout "LOC"
#define LTR_L (SEG_D | SEG_E | SEG_F)

// Huruf 'O' atau '0' untuk Lockout "LOC"
#define LTR_O (SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F)

// Huruf 'C' untuk "LOC", "C-1", "C-2", "C-3", dan satuan suhu " xxC"
#define LTR_C (SEG_A | SEG_D | SEG_E | SEG_F)

// Huruf 'S' untuk menu Setting "SEtt" atau "S-01"
#define LTR_S (SEG_A | SEG_C | SEG_D | SEG_F | SEG_G)

// Huruf 'P' untuk teks pembuka "SCP " dan "PrO "
#define LTR_P (SEG_A | SEG_B | SEG_E | SEG_F | SEG_G)

// Huruf 'n' kecil untuk teks "rUn "
#define LTR_n (SEG_C | SEG_E | SEG_G)

// Huruf 't' kecil untuk teks "SEtt", "rSt ", dan "U-rt"
#define LTR_t (SEG_D | SEG_E | SEG_F | SEG_G)

// Huruf 'A' untuk menu kalibrasi arus "A-rt" dan teks simpan "SAUE"
#define LTR_A (SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G)

// Huruf 'K' untuk konfirmasi simpan "OK  "
#define LTR_K (SEG_B | SEG_C | SEG_E | SEG_F | SEG_G)

// Tabel lookup segmen untuk angka 0 sampai 9
extern const uint8_t DIGIT_SEG[10];

/**
 * @brief Inisialisasi port GPIO untuk antarmuka TM1637.
 *        CLK diatur sebagai Push-Pull Output.
 *        DIO diatur sebagai Open-Drain Output dengan resistor pull-up eksternal.
 */
void tm1637_init(void);

/**
 * @brief Mengatur tingkat kecerahan layar TM1637.
 * @param brightness Tingkat kecerahan dari 0 (paling redup) hingga 7 (paling terang).
 */
void tm1637_set_brightness(uint8_t brightness);

/**
 * @brief Mengirimkan 4 byte pola segmen mentah ke digit 0..3 pada layar.
 * @param segments Array berisi 4 byte data segmen untuk digit 0 (paling kiri) sampai digit 3 (paling kanan).
 */
void tm1637_show_segments(const uint8_t segments[4]);

/**
 * @brief Menampilkan bilangan bulat bertanda (desimal) dengan perataan kanan (right-aligned).
 *        Mendukung angka negatif (misal -5 °C) dengan menampilkan tanda minus '-' (SEG_G) di sisi kiri.
 * @param num Nilai integer yang akan ditampilkan (-999 hingga 9999).
 */
void tm1637_show_number(int num);

#endif // TM1657_H
