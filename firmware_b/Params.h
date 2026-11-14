#ifndef PARAMS_H
#define PARAMS_H

#include <EEPROM.h>

// ----------------------------------------------------------
// NTC SWITCH (hardware limitation)
// PC1 / PC2 have no ADC on the CH32V003, so NTC1 / NTC2 cannot be read yet.
// Keep both at 0. When the hardware can read the NTCs: set to 1,
// implement readTemp1()/readTemp2() in Sensors.cpp, and give the
// menu entries S-13 / S-16 (and C-3) real storage in SystemParams.
// ----------------------------------------------------------
#define NTC1_ENABLED  0
#define NTC2_ENABLED  0

// CH32V003 EEPROM = 26 bytes. Magic (1) + struct (24) = 25 bytes used.
// The four uint16_t fields come FIRST so every uint16_t sits on an even address.
struct SystemParams {
  uint16_t lraLimit_mA;              // 800 - 5000
  uint16_t ocpLimit_mA;              // 300 - 4500
  uint16_t voltageMultiplier_x1000;  // 3420 = 3.420  (calibration C-1)
  uint16_t currentMultiplier_x1000;  // 1880 = 1.880  (calibration C-2)
  uint8_t  ovpLimit_div2;            // OVP volts / 2 (125 = 250 V). Use OVP_LIMIT below
  uint8_t  uvpLimit;                 // 150 - 200
  uint8_t  timeDelay_mins;           // 2 - 10
  uint8_t  refFlowDelay_mins;        // 2 - 10
  uint8_t  orpTimeLimit_hours;       // 2 - 12
  uint8_t  ovpUvpDelay;              // 3 - 10
  uint8_t  voltageHysteresis;        // 10 - 40
  uint8_t  lraTime;                  // 1 - 5
  uint8_t  lraRetryLimit;            // 2 - 5
  uint8_t  ocpDelay;                 // 2 - 10
  uint8_t  ucpLimit_x10;             // 0 or 20 - 100 (x10 mA, 0 = Off)
  uint8_t  minCurrentThreshold_x10;  // 0 or 15 - 30  (x10 mA, 0 = Off)
  uint8_t  otpLimit;                 // 70 - 90   (NTC1, not active yet)
  uint8_t  otpHysteresis;            // 5 - 20    (NTC1, not active yet)
  uint8_t  flowFaultRetryLimit;      // 2 - 5     (NTC2, not active yet)
  uint8_t  orpRestDelay_mins;        // 20 - 60
};

static_assert(sizeof(SystemParams) <= 25, "SystemParams too big for 26-byte EEPROM (1 magic + struct)");

// OVP is stored divided by 2 so that 260 V fits in one byte
#define OVP_LIMIT  (params.ovpLimit_div2 * 2)

// Fixed limits (not stored)
#define PARAM_POWER_SURGE_LIMIT   270
#define PARAM_BLACKOUT_LIMIT      140

extern SystemParams params;

void loadParams();
void saveParams();
void resetToDefaultParams();

#endif
