#ifndef PARAMS_H
#define PARAMS_H

#include <EEPROM.h>

// ==========================================================
// SYSTEM PARAMETERS (EEPROM DATA)
// ==========================================================

struct SystemParams {
  // 1. Basic & Calibration
  uint16_t timeDelay;         // Default: 300 detik (5 Menit)
  float currentMultiplier;    // Default: 0.5
  float voltageMultiplier;    // Default: 0.5
  
  // 2. Voltage Protection (Satuan: Volt)
  uint16_t ovpLimit;          // Default: 250
  uint16_t uvpLimit;          // Default: 180
  uint8_t voltageHysteresis;  // Default: 10
  uint8_t ovpUvpDelay;        // Default: 5 detik
  uint16_t powerSurgeLimit;   // Default: 270 (Instant Trip)
  uint16_t blackoutLimit;     // Default: 140 (Instant Trip)

  // 3. Current Protection (Satuan: Ampere & Detik)
  float lraLimit;             // Default: 5.0 A (Batas Arus Start/Macet)
  uint8_t lraTime;            // Default: 2 detik (Toleransi waktu inrush)
  uint8_t lraRetryLimit;      // Default: 3 kali (Lockout jika gagal start terus)
  
  float ocpLimit;             // Default: 1.5 A (Batas Arus Overload saat running)
  uint8_t ocpDelay;           // Default: 4 detik
  
  float ucpLimit;             // Default: 0.3 A (Under Current/Freon Bocor - 0=Disable)
  
  // BATAS MINIMAL ARUS KOMPRESOR JALAN (Filter lampu kulkas)
  float minCurrentThreshold;  // Default: 0.25 A

  // 4. Temperature Protection (NTC1 - Compressor)
  bool ntc1Enable;            // Default: 0 (Disable)
  uint8_t otpLimit;           // Default: 85 C
  uint8_t otpHysteresis;      // Default: 10 C (Recovery at 75 C)

  // 5. Refrigerant Flow (NTC2 & UCP Fault)
  int8_t tempOffset;          // Default: 0 (Offset kalibrasi suhu C-3)
  bool ntc2Enable;            // Default: 0 (Disable)
  uint16_t refFlowDelay;      // Default: 300 detik (5 Menit) - Diubah ke uint16_t agar muat 300
  uint8_t refDeltaTemp;       // Default: 5 C (Kenaikan suhu minimal dari suhu awal)
  uint8_t flowFaultRetryLimit;// Default: 5 kali (Lockout jika bocor/mampet terus)

  // 6. Over Running Protection (ORP)
  uint16_t orpTimeLimit;      // Default: 420 Menit (7 Jam)
  uint16_t orpRestDelay;      // Default: 30 Menit (Waktu istirahat paksa)
};

extern SystemParams params;

void loadParams();
void saveParams();
void resetToDefaultParams();

#endif
