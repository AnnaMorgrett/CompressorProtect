#ifndef PARAMS_H
#define PARAMS_H

#include <EEPROM.h>

// CH32V003 EEPROM = 26 bytes. Magic (1) + struct (22) = 23 bytes used.
struct SystemParams {
  uint16_t lraLimit_mA;                // 800 - 5000
  uint16_t ocpLimit_mA;                // 300 - 4500
  uint8_t  timeDelay_mins;             // 2 - 10
  uint8_t  refFlowDelay_mins;          // 2 - 10
  uint8_t  orpTimeLimit_hours;         // 2 - 12
  uint8_t  voltageMultiplier_x100;     // 342 = 3.42
  uint8_t  currentMultiplier_x100;     // 188 = 1.88
  uint8_t  ovpLimit;                   // 230 - 260
  uint8_t  uvpLimit;                   // 150 - 200
  uint8_t  ovpUvpDelay;                // 3 - 10
  uint8_t  voltageHysteresis;          // 10 - 40
  uint8_t  lraTime;                    // 1 - 5
  uint8_t  lraRetryLimit;              // 2 - 5
  uint8_t  ocpDelay;                   // 2 - 10
  uint8_t  ucpLimit_x10;               // 0 - 100 (x10 mA, 0=Off)
  uint8_t  minCurrentThreshold_x10;    // 0 - 30  (x10 mA)
  uint8_t  ntc1Enable;                 // 0 or 1
  uint8_t  otpLimit;                   // 70 - 90
  uint8_t  otpHysteresis;              // 5 - 20
  uint8_t  ntc2Enable;                 // 0 or 1
  uint8_t  flowFaultRetryLimit;        // 2 - 5
  uint8_t  orpRestDelay_mins;          // 20 - 60
}; // Size = 22 bytes

// Removed 'f' suffix to avoid pulling in float math
#define PARAM_POWER_SURGE_LIMIT   270
#define PARAM_BLACKOUT_LIMIT      140

extern SystemParams params;

void loadParams();
void saveParams();
void resetToDefaultParams();

#endif