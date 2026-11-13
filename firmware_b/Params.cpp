#include "Params.h"

SystemParams params;

const int  EEPROM_MAGIC_ADDR = 0;
// Bumped A5 -> A6 because the struct layout changed. Existing boards
// will auto-reset to factory defaults on first boot after flashing.
const byte EEPROM_MAGIC_VAL  = 0xA6;
const int  EEPROM_DATA_ADDR  = 1;

void resetToDefaultParams() {
  params.timeDelay                 = 300;
  params.currentMultiplier_x1000   = 1880;   // 1.880
  params.voltageMultiplier_x1000   = 3420;   // 3.420
  params.ovpLimit                  = 250;
  params.uvpLimit                  = 180;
  params.lraLimit_x100             = 150;    // 1.50 A
  params.ocpLimit_x100             = 80;     // 0.80 A
  params.minCurrentThreshold_x100  = 25;     // 0.25 A
  params.orpTimeLimit              = 420;    // 420 min = 7 h

  params.voltageHysteresis         = 10;
  params.ovpUvpDelay               = 5;
  params.lraTime                   = 2;
  params.lraRetryLimit             = 3;
  params.ocpDelay                  = 4;
  params.orpRestDelay              = 30;

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