#ifndef PARAMS_H
#define PARAMS_H

#include <EEPROM.h>

// ==========================================================
// SYSTEM PARAMETERS (EEPROM DATA)
// ----------------------------------------------------------
// CH32V003 option-byte EEPROM is limited to 26 bytes total.
// Magic byte occupies _data[0], struct occupies _data[1..24].
// Struct is 24 bytes exactly (no padding, reordered for alignment).
// ==========================================================

struct SystemParams {
  // --- 16-bit fields (must be first for 2-byte alignment) ---
  uint16_t timeDelay;                    // seconds,    default 300
  uint16_t currentMultiplier_x1000;      // 1880 = 1.880 A/count (was float 1.880)
  uint16_t voltageMultiplier_x1000;      // 3420 = 3.420 V/count (was float 3.420)
  uint16_t ovpLimit;                     // V,          default 250
  uint16_t uvpLimit;                     // V,          default 180
  uint16_t lraLimit_x100;                // 150 = 1.50 A (was float 1.5)
  uint16_t ocpLimit_x100;                // 80  = 0.80 A (was float 0.8)
  uint16_t minCurrentThreshold_x100;     // 25  = 0.25 A (was float 0.25)
  uint16_t orpTimeLimit;                 // minutes,    default 420 (= 7 h)

  // --- 8-bit fields (packed at the end, no padding) ---
  uint8_t  voltageHysteresis;            // V,          default 10
  uint8_t  ovpUvpDelay;                  // seconds,    default 5
  uint8_t  lraTime;                      // seconds,    default 2
  uint8_t  lraRetryLimit;                // count,      default 3
  uint8_t  ocpDelay;                     // seconds,    default 4
  uint8_t  orpRestDelay;                 // minutes,    default 30
};
// Size = 9*2 + 6*1 = 24 bytes exactly. No padding.

// Hard-coded instantaneous thresholds (were menu-tunable on LGT8F328P
// and stored as 2-byte fields; removed here to fit CH32V003 EEPROM).
#define PARAM_POWER_SURGE_LIMIT   270.0f    // V, instant trip
#define PARAM_BLACKOUT_LIMIT      140.0f    // V, instant trip

extern SystemParams params;

void loadParams();
void saveParams();
void resetToDefaultParams();

#endif