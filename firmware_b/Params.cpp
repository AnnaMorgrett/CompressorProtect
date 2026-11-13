#include "Params.h"

SystemParams params;

const int  EEPROM_MAGIC_ADDR = 0;
const byte EEPROM_MAGIC_VAL  = 0xA7; // Changed from A6
const int  EEPROM_DATA_ADDR  = 1;

void resetToDefaultParams() {
  params.timeDelay_mins            = 5;
  params.refFlowDelay_mins         = 5;
  params.orpTimeLimit_hours        = 7;
  params.voltageMultiplier_x100    = 342;
  params.currentMultiplier_x100    = 188;
  params.ovpLimit                  = 250;
  params.uvpLimit                  = 180;
  params.ovpUvpDelay               = 5;
  params.voltageHysteresis         = 10;
  params.lraLimit_mA               = 1500;
  params.lraTime                   = 2;
  params.lraRetryLimit             = 3;
  params.ocpLimit_mA               = 800;
  params.ocpDelay                  = 4;
  params.ucpLimit_x10              = 0;
  params.minCurrentThreshold_x10   = 25;
  params.ntc1Enable                = 0;
  params.otpLimit                  = 85;
  params.otpHysteresis             = 10;
  params.ntc2Enable                = 0;
  params.flowFaultRetryLimit       = 5;
  params.orpRestDelay_mins         = 30;

  saveParams();
}

void loadParams() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VAL) {
    EEPROM.get(EEPROM_DATA_ADDR, params);
  } else {
    resetToDefaultParams();
  }
}

void saveParams() {
  EEPROM.put(EEPROM_DATA_ADDR, params);
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
  EEPROM.commit();
}