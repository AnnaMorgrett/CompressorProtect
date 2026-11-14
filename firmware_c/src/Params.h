#ifndef PARAMS_H
#define PARAMS_H
#include <EEPROM.h>

// ----------------------------------------------------------
// NTC SWITCH (hardware limitation)
// PC1 / PC2 have no ADC on the CH32V003, so NTC1 / NTC2 cannot be read yet.
// Keep both at 0 until PCB reroute.
// ----------------------------------------------------------
#define NTC1_ENABLED 0
#define NTC2_ENABLED 0

struct SystemParams
{
  uint16_t lraLimit_mA;             // 800 - 5000
  uint16_t ocpLimit_mA;             // 300 - 4500
  uint16_t voltageMultiplier_x1000; // 3420 = 3.420 (calibration C-1)
  uint16_t currentMultiplier_x1000; // 1880 = 1.880 (calibration C-2)
  uint8_t ovpLimit_div2;            // OVP volts / 2 (125 = 250 V)
  uint8_t uvpLimit;                 // 150 - 200
  uint8_t timeDelay_mins;           // 2 - 10
  uint8_t refFlowDelay_mins;        // 2 - 10
  uint8_t orpTimeLimit_hours;       // 2 - 12
  uint8_t ovpUvpDelay;              // 3 - 10
  uint8_t voltageHysteresis;        // 10 - 40
  uint8_t lraTime;                  // 1 - 5
  uint8_t lraRetryLimit;            // 2 - 5
  uint8_t ocpDelay;                 // 2 - 10
  uint8_t ucpLimit_x10;             // 0 or 20 - 100 (x10 mA, 0 = Off)
  uint8_t minCurrentThreshold_x10;  // 0 or 15 - 30  (x10 mA, 0 = Off)
  uint8_t otpLimit;                 // 70 - 90
  uint8_t otpHysteresis;            // 5 - 20
  uint8_t flowFaultRetryLimit;      // 2 - 5
  uint8_t orpRestDelay_mins;        // 20 - 60
};

static_assert(sizeof(SystemParams) <= 25, "SystemParams too big for 26-byte EEPROM (1 magic + struct)");

#define OVP_LIMIT (params.ovpLimit_div2 * 2)

#define PARAM_POWER_SURGE_LIMIT 270
#define PARAM_BLACKOUT_LIMIT 140

extern SystemParams params;

void loadParams();
void saveParams();
void resetToDefaultParams();

#endif