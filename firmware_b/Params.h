#ifndef PARAMS_H
#define PARAMS_H

#include <EEPROM.h>

// ----------------------------------------------------------
// NTC SWITCH (hardware limitation)[cite: 7]
// PC1 / PC2 have no ADC on the CH32V003, so NTC1 / NTC2 cannot be read yet.[cite: 7]
// Keep both at 0. When the hardware can read the NTCs: set to 1,[cite: 7]
// implement readTemp1()/readTemp2() in Sensors.cpp, and give the[cite: 7]
// menu entries S-13 / S-16 (and C-3) real storage in SystemParams.[cite: 7]
// ----------------------------------------------------------
#define NTC1_ENABLED  0
#define NTC2_ENABLED  0

// CH32V003 EEPROM = 26 bytes. Magic (1) + struct (24) = 25 bytes used.[cite: 7]
// The four uint16_t fields come FIRST so every uint16_t sits on an even address.[cite: 7]
struct SystemParams {
  uint16_t lraLimit_mA;              // 800 - 5000[cite: 7]
  uint16_t ocpLimit_mA;              // 300 - 4500[cite: 7]
  uint16_t voltageMultiplier_x1000;  // 3420 = 3.420  (calibration C-1)[cite: 7]
  uint16_t currentMultiplier_x1000;  // 1880 = 1.880  (calibration C-2)[cite: 7]

  uint8_t  ovpLimit_div2;            // OVP volts / 2 (125 = 250 V). Use OVP_LIMIT below[cite: 7]
  uint8_t  uvpLimit;                 // 150 - 200[cite: 7]
  uint8_t  timeDelay_mins;           // 2 - 10[cite: 7]
  uint8_t  refFlowDelay_mins;        // 2 - 10[cite: 7]
  uint8_t  orpTimeLimit_hours;       // 2 - 12[cite: 7]
  uint8_t  ovpUvpDelay;              // 3 - 10[cite: 7]
  uint8_t  voltageHysteresis;        // 10 - 40[cite: 7]
  uint8_t  lraTime;                  // 1 - 5[cite: 7]
  uint8_t  lraRetryLimit;            // 2 - 5[cite: 7]
  uint8_t  ocpDelay;                 // 2 - 10[cite: 7]
  uint8_t  ucpLimit_x10;             // 0 or 20 - 100 (x10 mA, 0 = Off)[cite: 7]
  uint8_t  minCurrentThreshold_x10;  // 0 or 15 - 30  (x10 mA, 0 = Off)[cite: 7]
  uint8_t  otpLimit;                 // 70 - 90   (NTC1, not active yet)[cite: 7]
  uint8_t  otpHysteresis;            // 5 - 20    (NTC1, not active yet)[cite: 7]
  uint8_t  flowFaultRetryLimit;      // 2 - 5     (NTC2, not active yet)[cite: 7]
  uint8_t  orpRestDelay_mins;        // 20 - 60[cite: 7]
};

static_assert(sizeof(SystemParams) <= 25, "SystemParams too big for 26-byte EEPROM (1 magic + struct)");

// OVP is stored divided by 2 so that 260 V fits in one byte[cite: 7]
#define OVP_LIMIT  (params.ovpLimit_div2 * 2)

// Fixed limits (not stored)[cite: 7]
#define PARAM_POWER_SURGE_LIMIT   270
#define PARAM_BLACKOUT_LIMIT      140

extern SystemParams params;

void loadParams();
void saveParams();
void resetToDefaultParams();

#endif