#include "UI.h"
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"

TM1637Display display(PIN_TM_CLK, PIN_TM_DAT);

const uint8_t LTR_U = SEG_B | SEG_C | SEG_D | SEG_E | SEG_F; 
const uint8_t LTR_E = SEG_A | SEG_D | SEG_E | SEG_F | SEG_G; 
const uint8_t LTR_r = SEG_E | SEG_G;                         
const uint8_t LTR_L = SEG_D | SEG_E | SEG_F;                 
const uint8_t LTR_O = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F; 
const uint8_t LTR_C = SEG_A | SEG_D | SEG_E | SEG_F;         
const uint8_t LTR_S = SEG_A | SEG_C | SEG_D | SEG_F | SEG_G; 
const uint8_t LTR_P = SEG_A | SEG_B | SEG_E | SEG_F | SEG_G; 
const uint8_t LTR_t = SEG_D | SEG_E | SEG_F | SEG_G;                 
const uint8_t LTR_A = SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G; 
const uint8_t LTR_K = SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;         
const uint8_t BLANK = 0x00;

void initUI() {
  display.setBrightness(5); 
  uint8_t msgSCP[] = { LTR_S, LTR_C, LTR_P, BLANK };
  display.setSegments(msgSCP);
  delay(1500); 
  uint8_t msgPro[] = { LTR_P, LTR_r, LTR_O, BLANK };
  display.setSegments(msgPro);
  delay(1000);
  display.clear();
}

void updateDisplay(int currentV, int currentA) {
  static unsigned long brightTimer = millis(); 
  static FaultType lastDisplayFault = FAULT_NONE;
  static SystemState lastDisplayState = STATE_STARTUP_DELAY;

  if (currentFault != lastDisplayFault) {
    lastDisplayFault = currentFault;
    if (currentFault != FAULT_NONE) brightTimer = millis(); 
  }
  if (currentState != lastDisplayState) {
    lastDisplayState = currentState;
    if (currentState == STATE_STARTUP_DELAY) brightTimer = millis(); 
  }

  if (millis() - brightTimer <= 10000UL) display.setBrightness(4); 
  else display.setBrightness(1); 
  
  bool isAltScreen = (millis() / 2000) % 2 == 1; 

  long remainingSec = 0;
  if (currentState == STATE_STARTUP_DELAY) remainingSec = (params.timeDelay * 1000UL) - (millis() - stateTimer);
  else if (currentState == STATE_ORP_REST) remainingSec = (params.orpRestDelay * 60000UL) - (millis() - stateTimer);
  
  if (remainingSec < 0) remainingSec = 0;
  remainingSec /= 1000; 

  if (currentState == STATE_RUNNING) {
    if (isAltScreen) {
      uint8_t dataV[] = {
        currentV > 99 ? display.encodeDigit(currentV / 100) : BLANK,
        display.encodeDigit((currentV / 10) % 10),
        display.encodeDigit(currentV % 10),
        LTR_U
      };
      display.setSegments(dataV);
    } else {
      display.showNumberDec(currentA, false, 4, 0); 
    }
  }
  else if (currentState == STATE_FAULT) {
    uint8_t dataErr[] = { LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault) };
    display.setSegments(dataErr);
  }
  else if (currentState == STATE_STARTUP_DELAY || currentState == STATE_ORP_REST) {
    if (remainingSec > 60) {
      if (isAltScreen && currentFault != FAULT_NONE) {
        uint8_t dataErr[] = { LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault) };
        display.setSegments(dataErr);
      } else {
        display.showNumberDec((remainingSec / 60) + 1, false, 4, 0);
      }
    } else {
      display.showNumberDec(remainingSec, false, 4, 0);
    }
  }
  else if (currentState == STATE_LOCKOUT) {
    if (isAltScreen) {
      uint8_t dataErr[] = { LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault) };
      display.setSegments(dataErr);
    } else {
      uint8_t locReason = (currentFault == FAULT_LRA) ? 1 : 2;
      uint8_t dataLoc[] = { LTR_L, LTR_O, LTR_C, display.encodeDigit(locReason) };
      display.setSegments(dataLoc);
    }
  }
}

void handleSettingMode() {
  display.setBrightness(4);
  static uint8_t menuState = 0;  
  static int8_t currentMenu = 1; 
  static int editValue = 0;    

  static uint8_t lastBtn = 0; 
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;
  
  uint8_t rawBtn = 0; 
  int adc = analogRead(PIN_BUTTONS);
  if (adc < 600) rawBtn = 1;                             
  else if (adc >= 800 && adc <= 1600) rawBtn = 2;        
  else if (adc >= 1800 && adc <= 2800) rawBtn = 3;       

  uint8_t clickEvent = 0;     
  bool longPressEvent = false;

  if (rawBtn != lastBtn) {
    if (rawBtn != 0) {
      btnPressTime = millis();
      isLongPressHandled = false;
    } else if (!isLongPressHandled && (millis() - btnPressTime > 50)) clickEvent = lastBtn; 
    lastBtn = rawBtn;
  } else if (rawBtn == 2 && !isLongPressHandled && (millis() - btnPressTime > 1000)) {
    longPressEvent = true;
    isLongPressHandled = true; 
  }

  uint8_t msgSett[] = { LTR_S, LTR_E, LTR_t, LTR_t };
  uint8_t msgSave[] = { LTR_S, LTR_A, LTR_U, LTR_E };
  uint8_t msgOk[]   = { LTR_O, LTR_K, BLANK, BLANK };
  uint8_t msgRst[]  = { LTR_r, LTR_S, LTR_t, BLANK };

  switch (menuState) {
    case 0:
      display.setSegments(msgSett);
      if (rawBtn == 0) menuState = 1; 
      break;
    case 1: 
      display.setSegments(msgSett);
      if (clickEvent == 2) menuState = 2; 
      break;
    case 2:
      if (currentMenu <= 20) {
        uint8_t dataS[] = { 
          LTR_S, SEG_G, 
          (currentMenu >= 10) ? display.encodeDigit(currentMenu / 10) : BLANK, 
          display.encodeDigit(currentMenu % 10) 
        };
        display.setSegments(dataS);
      } 
      else if (currentMenu == 21) display.setSegments(msgSave);
      else if (currentMenu == 22) display.setSegments(msgRst);

      if (clickEvent == 1 && currentMenu < 22) currentMenu++; 
      if (clickEvent == 3 && currentMenu > 1) currentMenu--; 
      
      if (clickEvent == 2) {
        if (currentMenu == 21) {
          display.setSegments(msgOk);
          saveParams(); delay(1000); restartSystem();
        } else if (currentMenu == 22) {
          display.setSegments(msgOk);
          resetToDefaultParams(); delay(1000); restartSystem();
        } else {
          int vals[20] = {
            params.timeDelay / 60, params.ovpLimit, params.uvpLimit, params.ovpUvpDelay, params.voltageHysteresis,
            params.lraLimit, params.lraTime, params.lraRetryLimit, params.ocpLimit, params.ocpDelay,
            params.ucpLimit, params.minCurrentThreshold, params.ntc1Enable, params.otpLimit, params.otpHysteresis,
            params.ntc2Enable, params.refFlowDelay / 60, params.flowFaultRetryLimit, params.orpTimeLimit / 60, params.orpRestDelay
          };
          editValue = vals[currentMenu - 1];
          menuState = 3; 
        }
      }
      break;

    case 3: 
      display.showNumberDec(editValue, false, 4, 0);

      // OPTIMASI ARRAY: Hemat ~1KB kode Flash dari 60 baris switch-case 
      const int16_t menuLimits[20][3] = {
        {2,10,1}, {230,260,10}, {150,200,10}, {3,10,1}, {10,40,1}, {800,5000,100}, 
        {1,5,1}, {2,5,1}, {300,4500,50}, {2,10,1}, {0,1000,50}, {0,400,50}, {0,1,1}, 
        {70,90,1}, {5,20,1}, {0,1,1}, {2,10,1}, {2,5,1}, {2,12,1}, {20,60,1}
      };

      if (clickEvent == 1) editValue += menuLimits[currentMenu-1][2];
      if (clickEvent == 3) editValue -= menuLimits[currentMenu-1][2];

      if (editValue < menuLimits[currentMenu-1][0]) editValue = menuLimits[currentMenu-1][0];
      if (editValue > menuLimits[currentMenu-1][1]) editValue = menuLimits[currentMenu-1][1];

      if (clickEvent == 2 || longPressEvent) {
        switch(currentMenu) {
          case 1: params.timeDelay = editValue * 60; break;
          case 2: params.ovpLimit = editValue; break;
          case 3: params.uvpLimit = editValue; break;
          case 4: params.ovpUvpDelay = editValue; break;
          case 5: params.voltageHysteresis = editValue; break;
          case 6: params.lraLimit = editValue; break;
          case 7: params.lraTime = editValue; break;
          case 8: params.lraRetryLimit = editValue; break;
          case 9: params.ocpLimit = editValue; break;
          case 10: params.ocpDelay = editValue; break;
          case 11: params.ucpLimit = editValue; break;
          case 12: params.minCurrentThreshold = editValue; break;
          case 13: params.ntc1Enable = editValue; break;
          case 14: params.otpLimit = editValue; break;
          case 15: params.otpHysteresis = editValue; break;
          case 16: params.ntc2Enable = editValue; break;
          case 17: params.refFlowDelay = editValue * 60; break;
          case 18: params.flowFaultRetryLimit = editValue; break;
          case 19: params.orpTimeLimit = editValue * 60; break;
          case 20: params.orpRestDelay = editValue; break;
        }
        menuState = 2; 
      }
      break;
  }
}

void handleCalibrationMode() {
  display.setBrightness(4);
  static uint8_t menuState = 0;  
  static int8_t currentMenu = 1; 
  static int editValue = 0;    

  static uint8_t lastBtn = 0; 
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;
  
  uint8_t rawBtn = 0; 
  int adc = analogRead(PIN_BUTTONS);
  if (adc < 600) rawBtn = 1;                             
  else if (adc >= 800 && adc <= 1600) rawBtn = 2;        
  else if (adc >= 1800 && adc <= 2800) rawBtn = 3;       

  uint8_t clickEvent = 0;     
  bool longPressEvent = false;

  if (rawBtn != lastBtn) {
    if (rawBtn != 0) {
      btnPressTime = millis();
      isLongPressHandled = false;
    } else if (!isLongPressHandled && (millis() - btnPressTime > 50)) clickEvent = lastBtn; 
    lastBtn = rawBtn;
  } else if (rawBtn == 2 && !isLongPressHandled && (millis() - btnPressTime > 1000)) {
    longPressEvent = true;
    isLongPressHandled = true; 
  }

  uint8_t msgCal[]  = { LTR_C, LTR_A, LTR_L, BLANK };
  uint8_t msgUrt[]  = { LTR_U, SEG_G, LTR_r, LTR_t }; 
  uint8_t msgArt[]  = { LTR_A, SEG_G, LTR_r, LTR_t }; 
  uint8_t msgTrt[]  = { LTR_t, SEG_G, LTR_r, LTR_t }; 
  uint8_t msgC1[]   = { LTR_C, SEG_G, BLANK, display.encodeDigit(1) }; 
  uint8_t msgC2[]   = { LTR_C, SEG_G, BLANK, display.encodeDigit(2) }; 
  uint8_t msgC3[]   = { LTR_C, SEG_G, BLANK, display.encodeDigit(3) }; 
  uint8_t msgSave[] = { LTR_S, LTR_A, LTR_U, LTR_E };
  uint8_t msgOk[]   = { LTR_O, LTR_K, BLANK, BLANK };

  switch (menuState) {
    case 0:
      display.setSegments(msgCal);
      if (rawBtn == 0) menuState = 1; 
      break;
    case 1:
      display.setSegments(msgCal);
      if (clickEvent == 2) menuState = 2; 
      break;
    case 2:
      if (currentMenu == 1) display.setSegments(msgUrt);
      else if (currentMenu == 2) display.setSegments(msgArt);
      else if (currentMenu == 3) display.setSegments(msgTrt);
      else if (currentMenu == 4) display.setSegments(msgC1);
      else if (currentMenu == 5) display.setSegments(msgC2);
      else if (currentMenu == 6) display.setSegments(msgC3);
      else if (currentMenu == 7) display.setSegments(msgSave);

      if (clickEvent == 1 && currentMenu < 7) currentMenu++; 
      if (clickEvent == 3 && currentMenu > 1) currentMenu--; 
      
      if (clickEvent == 2) {
        if (currentMenu == 7) {
          display.setSegments(msgOk);
          saveParams(); delay(1000); menuState = 2; 
        } else {
          if (currentMenu == 4) editValue = params.voltageMultiplier;
          else if (currentMenu == 5) editValue = params.currentMultiplier;
          else if (currentMenu == 6) editValue = params.tempOffset;
          menuState = 3; 
        }
      }
      break;
    case 3: 
      {
        static unsigned long lastLiveUpdate = 0;
        static int displayedLiveVal = 0;

        if (millis() - lastLiveUpdate >= 250) {
          lastLiveUpdate = millis();
          if (currentMenu == 1) displayedLiveVal = sysVolt;
          else if (currentMenu == 2) displayedLiveVal = sysAmp;
          else if (currentMenu == 3) displayedLiveVal = sysTemp1;
          else if (currentMenu >= 4) displayedLiveVal = editValue;
        }

        display.showNumberDec(displayedLiveVal, false, 4, 0);

        if (currentMenu == 4 || currentMenu == 5) {
          if (clickEvent == 1) editValue += 1; 
          if (clickEvent == 3) editValue -= 1;
          if (editValue > 5000) editValue = 5000;
          if (editValue < 1) editValue = 1;
        }
        else if (currentMenu == 6) {
          if (clickEvent == 1) editValue += 1;
          if (clickEvent == 3) editValue -= 1;
          if (editValue > 20) editValue = 20;
          if (editValue < -20) editValue = -20;
        }

        if (clickEvent == 2 || longPressEvent) {
          if (currentMenu == 4) params.voltageMultiplier = editValue;
          if (currentMenu == 5) params.currentMultiplier = editValue;
          if (currentMenu == 6) params.tempOffset = editValue;
          menuState = 2; 
        }
      }
      break;
  }
}