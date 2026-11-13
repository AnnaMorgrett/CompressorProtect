#include "UI.h"
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"

TM1637Display display(PIN_TM_CLK, PIN_TM_DAT);

// ==========================================
// CUSTOM 7-SEGMENT LETTERS
// ==========================================
const uint8_t LTR_U = SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
const uint8_t LTR_E = SEG_A | SEG_D | SEG_E | SEG_F | SEG_G;
const uint8_t LTR_r = SEG_E | SEG_G;
const uint8_t LTR_L = SEG_D | SEG_E | SEG_F;
const uint8_t LTR_O = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
const uint8_t LTR_C = SEG_A | SEG_D | SEG_E | SEG_F;
const uint8_t LTR_S = SEG_A | SEG_C | SEG_D | SEG_F | SEG_G;
const uint8_t LTR_t = SEG_D | SEG_E | SEG_F | SEG_G;
const uint8_t LTR_A = SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;
const uint8_t LTR_K = SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;

const uint8_t BLANK = 0x00;

// ==========================================
// SHARED 7-SEGMENT MESSAGES (flash-resident)
// ==========================================
static const uint8_t MSG_SETT[] = { LTR_S, LTR_E, LTR_t, LTR_t };
static const uint8_t MSG_SAVE[] = { LTR_S, LTR_A, LTR_U, LTR_E };
static const uint8_t MSG_OK[]   = { LTR_O, LTR_K, BLANK, BLANK };
static const uint8_t MSG_RST[]  = { LTR_r, LTR_S, LTR_t, BLANK };
static const uint8_t MSG_CAL[]  = { LTR_C, LTR_A, LTR_L, BLANK };
static const uint8_t MSG_URT[]  = { LTR_U, SEG_G, LTR_r, LTR_t };
static const uint8_t MSG_ART[]  = { LTR_A, SEG_G, LTR_r, LTR_t };
static const uint8_t MSG_C1[]   = { LTR_C, SEG_G, BLANK, 0x06 };
static const uint8_t MSG_C2[]   = { LTR_C, SEG_G, BLANK, 0x5B };

// ==========================================
// INIT UI — single splash
// ==========================================
void initUI() {
  display.setBrightness(5);
  const uint8_t msgSCP[] = { LTR_S, LTR_C, 0x73, BLANK };  // "SCP "
  display.setSegments(msgSCP);
  delay(1500);
  display.clear();
}

// ==========================================
// UPDATE DISPLAY
// ==========================================
void updateDisplay(float currentV, float currentA) {
  static unsigned long brightTimer = millis();
  static FaultType     lastDisplayFault = FAULT_NONE;
  static SystemState   lastDisplayState = STATE_STARTUP_DELAY;

  if (currentFault != lastDisplayFault) {
    lastDisplayFault = currentFault;
    if (currentFault != FAULT_NONE) brightTimer = millis();
  }

  if (currentState != lastDisplayState) {
    lastDisplayState = currentState;
    if (currentState == STATE_STARTUP_DELAY) brightTimer = millis();
  }

  if (millis() - brightTimer <= 10000UL) display.setBrightness(4);
  else                                   display.setBrightness(1);

  bool isAltScreen = (millis() / 2000) % 2 == 1;

  long remainingSec = 0;
  if (currentState == STATE_STARTUP_DELAY) {
    remainingSec = (long)(params.timeDelay * 1000UL) - (long)(millis() - stateTimer);
  } else if (currentState == STATE_ORP_REST) {
    remainingSec = (long)(30UL * 60000UL) - (long)(millis() - stateTimer);   // hardcoded 30 min
  }

  if (remainingSec < 0) remainingSec = 0;
  remainingSec /= 1000;

  if (currentState == STATE_RUNNING) {
    if (isAltScreen) {
      int v = (int)currentV;
      uint8_t dataV[] = {
        v > 99 ? display.encodeDigit(v / 100) : BLANK,
        display.encodeDigit((v / 10) % 10),
        display.encodeDigit(v % 10),
        LTR_U
      };
      display.setSegments(dataV);
    } else {
      int mA = (int)(currentA * 1000.0f);
      display.showNumberDec(mA, false, 4, 0);
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

// ==========================================
// SETTING MENU — P-01..P-11 (11 params) + SAUE(12) + RSt(13)
// P-12 "orpTimeLimit" removed — hardcoded to 420 min (7 h)
// ==========================================
void handleSettingMode() {
  display.setBrightness(4);
  static uint8_t menuState = 0;
  static int8_t  currentMenu = 1;
  static float   editValue = 0.0f;

  static uint8_t lastBtn = 0;
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;

  uint8_t rawBtn = 0;
  int adc = analogRead(PIN_BUTTONS);
  if      (adc < 240)                rawBtn = 1;  // Plus
  else if (adc >= 240 && adc <= 420) rawBtn = 2;  // Menu/OK
  else if (adc >= 420 && adc <= 650) rawBtn = 3;  // Minus

  uint8_t clickEvent = 0;
  bool longPressEvent = false;

  if (rawBtn != lastBtn) {
    if (rawBtn != 0) {
      btnPressTime = millis();
      isLongPressHandled = false;
    } else if (!isLongPressHandled && (millis() - btnPressTime > 50)) {
      clickEvent = lastBtn;
    }
    lastBtn = rawBtn;
  } else if (rawBtn == 2 && !isLongPressHandled && (millis() - btnPressTime > 1000)) {
    longPressEvent = true;
    isLongPressHandled = true;
  }

  switch (menuState) {
    case 0:
      display.setSegments(MSG_SETT);
      if (rawBtn == 0) menuState = 1;
      break;

    case 1:
      display.setSegments(MSG_SETT);
      if (clickEvent == 2) menuState = 2;
      break;

    case 2:
      if (currentMenu <= 11) {
        uint8_t dataS[] = {
          LTR_S,
          SEG_G,
          (currentMenu >= 10) ? display.encodeDigit(currentMenu / 10) : BLANK,
          display.encodeDigit(currentMenu % 10)
        };
        display.setSegments(dataS);
      }
      else if (currentMenu == 12) display.setSegments(MSG_SAVE);
      else if (currentMenu == 13) display.setSegments(MSG_RST);

      if (clickEvent == 1 && currentMenu < 13) currentMenu++;
      if (clickEvent == 3 && currentMenu > 1)  currentMenu--;

      if (clickEvent == 2) {
        if (currentMenu == 12) {
          display.setSegments(MSG_OK);
          saveParams();
          delay(1000);
          restartSystem();
        }
        else if (currentMenu == 13) {
          display.setSegments(MSG_OK);
          resetToDefaultParams();
          delay(1000);
          restartSystem();
        }
        else {
          if      (currentMenu == 1)  editValue = params.timeDelay / 60.0f;
          else if (currentMenu == 2)  editValue = (float)params.ovpLimit;
          else if (currentMenu == 3)  editValue = (float)params.uvpLimit;
          else if (currentMenu == 4)  editValue = (float)params.ovpUvpDelay;
          else if (currentMenu == 5)  editValue = (float)params.voltageHysteresis;
          else if (currentMenu == 6)  editValue = (float)params.lraLimit_x100 * 10.0f;
          else if (currentMenu == 7)  editValue = (float)params.lraTime;
          else if (currentMenu == 8)  editValue = (float)params.lraRetryLimit;
          else if (currentMenu == 9)  editValue = (float)params.ocpLimit_x100 * 10.0f;
          else if (currentMenu == 10) editValue = (float)params.ocpDelay;
          else if (currentMenu == 11) editValue = (float)params.minCurrentThreshold_x100 * 10.0f;

          menuState = 3;
        }
      }
      break;

    case 3: {
      display.showNumberDec((int)editValue, false, 4, 0);

      float stepValue = 1.0f;
      if      (currentMenu == 2 || currentMenu == 3) stepValue = 10.0f;
      else if (currentMenu == 6)                     stepValue = 100.0f;
      else if (currentMenu == 9)                     stepValue = 50.0f;
      else if (currentMenu == 11)                    stepValue = 50.0f;

      if (clickEvent == 1) editValue += stepValue;
      if (clickEvent == 3) editValue -= stepValue;

      if      (currentMenu == 1)  { if(editValue>10.0f)  editValue=10.0f;    if(editValue<2.0f)   editValue=2.0f;  }
      else if (currentMenu == 2)  { if(editValue>260.0f) editValue=260.0f;   if(editValue<230.0f) editValue=230.0f;}
      else if (currentMenu == 3)  { if(editValue>200.0f) editValue=200.0f;   if(editValue<150.0f) editValue=150.0f;}
      else if (currentMenu == 4)  { if(editValue>10.0f)  editValue=10.0f;    if(editValue<3.0f)   editValue=3.0f;  }
      else if (currentMenu == 5)  { if(editValue>40.0f)  editValue=40.0f;    if(editValue<10.0f)  editValue=10.0f; }
      else if (currentMenu == 6)  { if(editValue>5000.0f)editValue=5000.0f;  if(editValue<800.0f) editValue=800.0f;}
      else if (currentMenu == 7)  { if(editValue>5.0f)   editValue=5.0f;     if(editValue<1.0f)   editValue=1.0f;  }
      else if (currentMenu == 8)  { if(editValue>5.0f)   editValue=5.0f;     if(editValue<2.0f)   editValue=2.0f;  }
      else if (currentMenu == 9)  { if(editValue>4500.0f)editValue=4500.0f;  if(editValue<300.0f) editValue=300.0f;}
      else if (currentMenu == 10) { if(editValue>10.0f)  editValue=10.0f;    if(editValue<2.0f)   editValue=2.0f;  }
      else if (currentMenu == 11) { if(editValue>300.0f) editValue=400.0f;   if(editValue<150.0f) editValue=0.0f;  }

      if (clickEvent == 2 || longPressEvent) {
        if      (currentMenu == 1)  params.timeDelay = (uint16_t)editValue * 60;
        else if (currentMenu == 2)  params.ovpLimit = (uint16_t)editValue;
        else if (currentMenu == 3)  params.uvpLimit = (uint16_t)editValue;
        else if (currentMenu == 4)  params.ovpUvpDelay = (uint8_t)editValue;
        else if (currentMenu == 5)  params.voltageHysteresis = (uint8_t)editValue;
        else if (currentMenu == 6)  params.lraLimit_x100 = (uint16_t)(editValue / 10.0f);
        else if (currentMenu == 7)  params.lraTime = (uint8_t)editValue;
        else if (currentMenu == 8)  params.lraRetryLimit = (uint8_t)editValue;
        else if (currentMenu == 9)  params.ocpLimit_x100 = (uint16_t)(editValue / 10.0f);
        else if (currentMenu == 10) params.ocpDelay = (uint8_t)editValue;
        else if (currentMenu == 11) params.minCurrentThreshold_x100 = (uint16_t)(editValue / 10.0f);

        menuState = 2;
      }
      break;
    }
  }
}

// ==========================================
// CALIBRATION MENU — 5 items: U-rt, A-rt, C-1, C-2, SAUE
// ==========================================
void handleCalibrationMode() {
  display.setBrightness(4);
  static uint8_t menuState = 0;
  static int8_t  currentMenu = 1;
  static float   editValue = 0.0f;

  static uint8_t lastBtn = 0;
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;

  uint8_t rawBtn = 0;
  int adc = analogRead(PIN_BUTTONS);
  if      (adc < 240)                rawBtn = 1;
  else if (adc >= 240 && adc <= 420) rawBtn = 2;
  else if (adc >= 420 && adc <= 650) rawBtn = 3;

  uint8_t clickEvent = 0;
  bool longPressEvent = false;

  if (rawBtn != lastBtn) {
    if (rawBtn != 0) {
      btnPressTime = millis();
      isLongPressHandled = false;
    } else if (!isLongPressHandled && (millis() - btnPressTime > 50)) {
      clickEvent = lastBtn;
    }
    lastBtn = rawBtn;
  } else if (rawBtn == 2 && !isLongPressHandled && (millis() - btnPressTime > 1000)) {
    longPressEvent = true;
    isLongPressHandled = true;
  }

  switch (menuState) {
    case 0:
      display.setSegments(MSG_CAL);
      if (rawBtn == 0) menuState = 1;
      break;

    case 1:
      display.setSegments(MSG_CAL);
      if (clickEvent == 2) menuState = 2;
      break;

    case 2:
      if      (currentMenu == 1) display.setSegments(MSG_URT);
      else if (currentMenu == 2) display.setSegments(MSG_ART);
      else if (currentMenu == 3) display.setSegments(MSG_C1);
      else if (currentMenu == 4) display.setSegments(MSG_C2);
      else if (currentMenu == 5) display.setSegments(MSG_SAVE);

      if (clickEvent == 1 && currentMenu < 5) currentMenu++;
      if (clickEvent == 3 && currentMenu > 1) currentMenu--;

      if (clickEvent == 2) {
        if (currentMenu == 5) {
          display.setSegments(MSG_OK);
          saveParams();
          delay(1000);
          menuState = 2;
        }
        else {
          if      (currentMenu == 3) editValue = params.voltageMultiplier_x1000 / 1000.0f;
          else if (currentMenu == 4) editValue = params.currentMultiplier_x1000 / 1000.0f;
          menuState = 3;
        }
      }
      break;

    case 3: {
      static unsigned long lastLiveUpdate = 0;
      static int displayedLiveVal = 0;

      if (millis() - lastLiveUpdate >= 250) {
        lastLiveUpdate = millis();
        if      (currentMenu == 1) displayedLiveVal = (int)sysVolt;
        else if (currentMenu == 2) displayedLiveVal = (int)(sysAmp * 1000.0f);
        else if (currentMenu == 3 || currentMenu == 4) displayedLiveVal = (int)(editValue * 1000.0f);
      }

      display.showNumberDec(displayedLiveVal, false, 4, 0);

      if (currentMenu == 3 || currentMenu == 4) {
        if (clickEvent == 1) editValue += 0.001f;
        if (clickEvent == 3) editValue -= 0.001f;
        if (editValue > 5.0f)   editValue = 5.0f;
        if (editValue < 0.001f) editValue = 0.001f;
      }

      if (clickEvent == 2 || longPressEvent) {
        if (currentMenu == 3) params.voltageMultiplier_x1000 = (uint16_t)(editValue * 1000.0f);
        if (currentMenu == 4) params.currentMultiplier_x1000 = (uint16_t)(editValue * 1000.0f);
        menuState = 2;
      }
      break;
    }
  }
}