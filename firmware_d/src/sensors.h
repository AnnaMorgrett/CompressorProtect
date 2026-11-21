#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

// ============================================================================
// MODUL AKUISISI SENSOR ANALOG & PENGUKURAN SINYAL LISTRIK TRUE-RMS
// ============================================================================
// Modul ini bertugas membaca dan memproses sinyal dari:
// 1. Sensor Tegangan AC (ZMPT101B) pada pin PA2 (AIN0)
// 2. Sensor Arus AC (ZMCT103C) pada pin PA1 (AIN1)
// 3. Tombol Panel Tangga Resistor pada pin PD6 (AIN6)
// 4. Sensor Suhu NTC (jika diaktifkan)
// Menggunakan algoritma True-RMS berbasis fixed-point 64-bit integer tanpa floating-point.
// ============================================================================

/**
 * @brief Inisialisasi periferal ADC1 internal CH32V003 dan kalibrasi otomatis.
 *        Mengatur GPIO analog, clock prescaler /8 (6 MHz), dan waktu sampling.
 */
void sensors_init(void);

/**
 * @brief Membaca nilai ADC 10-bit mentah (rentang 0 - 1023) pada pin tertentu.
 * @param pin Pin mikrokontroler (PA1, PA2, atau PD6).
 * @return Nilai konversi ADC 10-bit.
 */
uint16_t sensors_read_adc(uint8_t pin);

/**
 * @brief Mengukur tegangan listrik AC PLN menggunakan metode True-RMS 2 siklus (40 ms).
 *        Menerapkan peredaman noise (noise gate < 5 ADC counts), normalisasi faktor
 *        kalibrasi C-1, dan filter adaptif respons cepat (85:15 smoothing).
 * @return Nilai tegangan dalam satuan Decivolts (0.1 V), contoh: 2200 = 220.0 Volt.
 */
uint16_t sensors_read_voltage_dv(void);

/**
 * @brief Mengukur arus listrik beban kompresor menggunakan metode True-RMS 2 siklus (40 ms).
 *        Menerapkan noise gate (< 100 mA dianggap 0 mA), normalisasi faktor kalibrasi C-2,
 *        dan filter adaptif respons cepat.
 * @return Nilai arus dalam satuan Milliamperes (1 mA), contoh: 1500 = 1.500 Ampere.
 */
uint16_t sensors_read_current_ma(void);

/**
 * @brief Membaca sensor suhu NTC1 (Suhu Bodi Kompresor).
 * @return Suhu dalam derajat Celsius (°C) beserta offset kalibrasi C-3.
 */
int16_t sensors_read_temp1(void);

/**
 * @brief Membaca sensor suhu NTC2 (Suhu Pipa Filter Dryer).
 * @return Suhu dalam derajat Celsius (°C) beserta offset kalibrasi C-3.
 */
int16_t sensors_read_temp2(void);

#endif // SENSORS_H
