#include "Params.h"

SystemParams params;

const int EEPROM_MAGIC_ADDR = 0;   
const byte EEPROM_MAGIC_VAL = 0xA6; 
const int EEPROM_DATA_ADDR = 1;    

void resetToDefaultParams() {
  params.timeDelay = 300;
  params.currentMultiplier = 470; 
  params.voltageMultiplier = 855; 
  
  params.ovpLimit = 250;
  params.uvpLimit = 180;
  params.voltageHysteresis = 10;
  params.ovpUvpDelay = 5;
  params.powerSurgeLimit = 270;
  params.blackoutLimit = 140;

  params.lraLimit = 5000;         
  params.lraTime = 2;
  params.lraRetryLimit = 3;
  params.ocpLimit = 1500;         
  params.ocpDelay = 4;
  params.ucpLimit = 300;          
  params.minCurrentThreshold = 250; 

  params.ntc1Enable = false;
  params.otpLimit = 85;
  params.otpHysteresis = 10;

  params.tempOffset = 0;
  params.ntc2Enable = false;
  params.refFlowDelay = 300;
  params.refDeltaTemp = 5;
  params.flowFaultRetryLimit = 5;

  params.orpTimeLimit = 420;
  params.orpRestDelay = 30;

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
  // Perbaikan: Gunakan write(), bukan update() untuk CH32V
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL); 
}