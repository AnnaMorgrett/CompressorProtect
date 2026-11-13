#ifndef PARAMS_H
#define PARAMS_H

#include <EEPROM.h>

struct SystemParams {
  uint16_t timeDelay;         
  int currentMultiplier;      // Integer (Misal 470 untuk mewakili 0.470)
  int voltageMultiplier;      // Integer (Misal 855 untuk mewakili 0.855)
  
  uint16_t ovpLimit;          
  uint16_t uvpLimit;          
  uint8_t voltageHysteresis;  
  uint8_t ovpUvpDelay;        
  uint16_t powerSurgeLimit;   
  uint16_t blackoutLimit;     

  int lraLimit;               // Integer dalam mA (Misal 5000)
  uint8_t lraTime;            
  uint8_t lraRetryLimit;      
  
  int ocpLimit;               // Integer dalam mA (Misal 1500)
  uint8_t ocpDelay;           
  
  int ucpLimit;               // Integer dalam mA (Misal 300)
  int minCurrentThreshold;    // Integer dalam mA (Misal 250)

  bool ntc1Enable;            
  uint8_t otpLimit;           
  uint8_t otpHysteresis;      

  int8_t tempOffset;          
  bool ntc2Enable;            
  uint16_t refFlowDelay;      
  uint8_t refDeltaTemp;       
  uint8_t flowFaultRetryLimit;

  uint16_t orpTimeLimit;      
  uint16_t orpRestDelay;      
};

extern SystemParams params;

void loadParams();
void saveParams();
void resetToDefaultParams();

#endif