#ifndef BOARD_H
#define BOARD_H

#include "ch32fun.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// DEFINISI PIN & PEMETAAN HARDWARE (CH32V003A4M6 SOP-16)
// ============================================================================
// Mikrokontroler: WCH CH32V003A4M6 (32-bit RISC-V RV32EC, 48 MHz, 16KB Flash, 2KB RAM)
// Kemasan: SOP-16 (16 Pin)
// Proyek: Smart Compressor Protector (SCP Pro) - Batch 2 Cost-Effective
// ============================================================================

// --- Output Digital ---
// Pin 8 (PD4): Driver Relay Kompresor. Aktif HIGH (Logic 1 = Relay Nyala, Kompresor Hidup).
#define PIN_RELAY          PD4

// Pin 9 (PD5): LED Indikator Status Sistem. Aktif HIGH.
// Pola: Solid ON saat Running, Kedip lambat (500ms) saat Delay, Kedip N-kali saat Fault.
#define PIN_LED_STATUS     PD5

// Pin 16 (PC0): Driver Buzzer Indikator. Aktif HIGH.
// Mengendalikan Active Buzzer untuk alarm bahaya (5 kali bip).
#define PIN_BUZZER         PC0

// --- Antarmuka Display TM1637 (4-Digit 7-Segment) ---
// Pin 3 (PC3): Jalur Data (DIO) TM1637. Menggunakan mode Open-Drain dengan pull-up eksternal.
#define PIN_TM_DAT         PC3

// Pin 4 (PC4): Jalur Clock (CLK) TM1637. Menggunakan mode Push-Pull Output.
#define PIN_TM_CLK         PC4

// --- Input Analog (ADC 10-bit: 0 - 1023, Tegangan Referensi VDD = 5V / 3.3V) ---
// Pin 12 (PA1 / AIN1): Sensor Arus AC (ZMCT103). Tegangan offset ~VDD/2, membutuhkan True-RMS.
#define PIN_CURRENT_SENS   PA1

// Pin 13 (PA2 / AIN0): Sensor Tegangan AC PLN (ZMPT101). Tegangan offset ~VDD/2, True-RMS.
#define PIN_VOLTAGE_SENS   PA2

// Pin 10 (PD6 / AIN6): Tangga Resistor 3 Tombol Panel (Plus, Menu/OK, Minus).
// - Plus   (+)    : ADC < 240
// - Menu   (OK)   : ADC 240 - 420
// - Minus  (-)    : ADC 420 - 650
#define PIN_BUTTONS        PD6

// --- Input Sensor Suhu NTC (Catatan Keterbatasan Silikon CH32V003) ---
// PERHATIAN: Pada kemasan fisik SOP-16, Pin 1 (PC1) dan Pin 2 (PC2) TIDAK terhubung
// ke multiplexer ADC internal chip CH32V003 (hanya I/O digital).
// Maka sensor suhu NTC1 & NTC2 dinonaktifkan di firmware (NTC1_ENABLED 0, NTC2_ENABLED 0).
#define PIN_TEMP_DRYER     PC1   // Pin 1 (PC1) - NTC2 Filter Dryer
#define PIN_TEMP_COMP      PC2   // Pin 2 (PC2) - NTC1 Bodi Kompresor

// --- Port Komunikasi Serial & Kalibrasi (Header H1) ---
// Pin 6 (PC7): TX Software UART (9600 baud, 8N1) terhubung ke Pin 2 Header H1.
#define PIN_DEBUG_TX       PC7

// Pin 5 (PC6): RX Software UART & Pin Jumper Kalibrasi (Pin 3 Header H1).
// Saat Booting: Jika di-jumper ke GND (Active LOW), sistem masuk MODE KALIBRASI.
// Saat Running: Difungsikan sebagai jalur penerima data perintah (RX CLI 9600 bps).
#define PIN_DEBUG_RX       PC6
#define PIN_CALIBRATION    PC6

// Pin 7 (PD1): Pin SWIO (Single-Wire Debug & Flashing via WCH-Link / minichlink).
#define PIN_SWIO_DEBUG     PD1

// --- Konfigurasi Fitur Perangkat Keras ---
#define ENABLE_SERIAL_DEBUG 1     // 1 = Aktifkan CLI serial & telemetri 9600 baud
#define BUZZER_PASSIVE      0     // 0 = Active Buzzer (DC biasa), 1 = Passive Buzzer (PWM)
#define BUZZER_FREQ_HZ      2700  // Frekuensi gelombang jika menggunakan passive buzzer
#define BUTTON_CHIRP        0     // 0 = Tombol hening (sesuai Batch 1), 1 = Bip setiap tombol ditekan

#endif
