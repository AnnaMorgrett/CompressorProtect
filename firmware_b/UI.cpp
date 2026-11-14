#include "UI.h"
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"
#include <stddef.h>

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

// digits 1,2,3 for the fixed "C- n" labels
const uint8_t DIG_1 = 0x06;
const uint8_t DIG_2 = 0x5B;
const uint8_t DIG_3 = 0x4F;

static const uint8_t MSG_SCP[]  = {LTR_S, LTR_C, LTR_P, BLANK};
static const uint8_t MSG_PRO[]  = {LTR_P, LTR_r, LTR_O, BLANK};
static const uint8_t MSG_SETT[] = {LTR_S, LTR_E, LTR_t, LTR_t};
static const uint8_t MSG_SAVE[] = {LTR_S, LTR_A, LTR_U, LTR_E};
static const uint8_t MSG_OK[]   = {LTR_O, LTR_K, BLANK, BLANK};
static const uint8_t MSG_RST[]  = {LTR_r, LTR_S, LTR_t, BLANK};
static const uint8_t MSG_CAL[]  = {LTR_C, LTR_A, LTR_L, BLANK};
static const uint8_t MSG_URT[]  = {LTR_U, SEG_G, LTR_r, LTR_t};
static const uint8_t MSG_ART[]  = {LTR_A, SEG_G, LTR_r, LTR_t};
static const uint8_t MSG_TRT[]  = {LTR_t, SEG_G, LTR_r, LTR_t};
static const uint8_t MSG_C1[]   = {LTR_C, SEG_G, BLANK, DIG_1};
static const uint8_t MSG_C2[]   = {LTR_C, SEG_G, BLANK, DIG_2};
static const uint8_t MSG_C3[]   = {LTR_C, SEG_G, BLANK, DIG_3};

// ==========================================================
// SETTING MENU TABLE  (S-1 ... S-20, same order as the LGT version)
//   size    : bytes stored in SystemParams (0 = not stored, always shows 0)
//   min/max : limits as shown on the display
//   step    : change per Plus/Minus click
//   scale   : display value = stored value * scale   (ucp: stored x10 mA -> shown mA)
//   zeroOff : value 0 means "Off"; going below min jumps to 0, Plus from 0 jumps to min
// ==========================================================
struct ParamDef {
  uint8_t offset;
  uint8_t size;
  int16_t min_ui;
  int16_t max_ui;
  int16_t step_ui;
  int8_t  scale;
  bool    zeroOff;
};

const ParamDef menuTable[20] = {
    {offsetof(SystemParams, timeDelay_mins),           1,   2,   10,   1,  1, false},  // S-1  delay (min)
    {offsetof(SystemParams, ovpLimit_div2),            1, 230,  260,  10,  2, false},  // S-2  OVP (V), stored /2
    {offsetof(SystemParams, uvpLimit),                 1, 150,  200,  10,  1, false},  // S-3  UVP (V)
    {offsetof(SystemParams, ovpUvpDelay),              1,   3,   10,   1,  1, false},  // S-4  OVP/UVP delay (s)
    {offsetof(SystemParams, voltageHysteresis),        1,  10,   40,   1,  1, false},  // S-5  hysteresis (V)
    {offsetof(SystemParams, lraLimit_mA),              2, 800, 5000, 100,  1, false},  // S-6  LRA (mA)
    {offsetof(SystemParams, lraTime),                  1,   1,    5,   1,  1, false},  // S-7  LRA time (s)
    {offsetof(SystemParams, lraRetryLimit),            1,   2,    5,   1,  1, false},  // S-8  LRA retry
    {offsetof(SystemParams, ocpLimit_mA),              2, 300, 4500,  50,  1, false},  // S-9  OCP (mA)
    {offsetof(SystemParams, ocpDelay),                 1,   2,   10,   1,  1, false},  // S-10 OCP delay (s)
    {offsetof(SystemParams, ucpLimit_x10),             1, 200, 1000,  50, 10, true },  // S-11 UCP (mA), 0 = Off
    {offsetof(SystemParams, minCurrentThreshold_x10),  1, 150,  300,  50, 10, true },  // S-12 min run current (mA), 0 = Off
    {0,                                                0,   0,    0,   1,  1, false},  // S-13 NTC1 enable  (locked 0)
    {offsetof(SystemParams, otpLimit),                 1,  70,   90,   1,  1, false},  // S-14 OTP (C)
    {offsetof(SystemParams, otpHysteresis),            1,   5,   20,   1,  1, false},  // S-15 OTP hysteresis (C)
    {0,                                                0,   0,    0,   1,  1, false},  // S-16 NTC2 enable  (locked 0)
    {offsetof(SystemParams, refFlowDelay_mins),        1,   2,   10,   1,  1, false},  // S-17 flow check delay (min)
    {offsetof(SystemParams, flowFaultRetryLimit),      1,   2,    5,   1,  1, false},  // S-18 flow retry
    {offsetof(SystemParams, orpTimeLimit_hours),       1,   2,   12,   1,  1, false},  // S-19 ORP time (hours)
    {offsetof(SystemParams, orpRestDelay_mins),        1,  20,   60,   1,  1, false}   // S-20 ORP rest (min)
};

int32_t getParamVal(uint8_t idx) {
  if (menuTable[idx].size == 0) return 0;
  const uint8_t *p = (const uint8_t *)&params + menuTable[idx].offset;
  int32_t raw = (menuTable[idx].size == 1) ? *(const uint8_t *)p : *(const uint16_t *)p;
  return raw * menuTable[idx].scale;
}

void setParamVal(uint8_t idx, int32_t ui) {
  if (menuTable[idx].size == 0) return;
  uint8_t *p = (uint8_t *)&params + menuTable[idx].offset;
  int32_t raw = ui / menuTable[idx].scale;
  if (menuTable[idx].size == 1) *(uint8_t *)p = (uint8_t)raw;
  else *(uint16_t *)p = (uint16_t)raw;
}

// Applies one Plus/Minus click to a menu value, keeping the limits
int32_t stepParamVal(uint8_t idx, int32_t value, int8_t direction) {
  const ParamDef &d = menuTable[idx];
  if (direction > 0) {
    if (d.zeroOff && value == 0) value = d.min_ui;
    else value += d.step_ui;
  } else {
    value -= d.step_ui;
    if (d.zeroOff && value < d.min_ui) value = 0;
  }
  if (!(d.zeroOff && value == 0)) {
    if (value < d.min_ui) value = d.min_ui;
    if (value > d.max_ui) value = d.max_ui;
  }
  return value;
}

char currentDisplayStr[5] = "----";

static void setDisplay4(char c0, char c1, char c2, char c3) {
  currentDisplayStr[0] = c0;
  currentDisplayStr[1] = c1;
  currentDisplayStr[2] = c2;
  currentDisplayStr[3] = c3;
  currentDisplayStr[4] = '\0';
}

static void setDisplayNum(int num) {
  if (num < 0) num = 0;
  currentDisplayStr[3] = '0' + (num % 10);
  num /= 10;
  currentDisplayStr[2] = (num > 0) ? ('0' + (num % 10)) : ' ';
  num /= 10;
  currentDisplayStr[1] = (num > 0) ? ('0' + (num % 10)) : ' ';
  num /= 10;
  currentDisplayStr[0] = (num > 0) ? ('0' + (num % 10)) : ' ';
  currentDisplayStr[4] = '\0';
}

void initUI() {
  display.setBrightness(5);
  display.setSegments(MSG_SCP);
  delay(1500);
  display.setSegments(MSG_PRO);
  delay(1000);
  display.clear();
}

void updateDisplay(uint16_t currentV, uint16_t currentA_mA) {
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
  else                                   display.setBrightness(1);

  bool isAltScreen = (millis() / 2000) % 2 == 1;

  long remainingSec = 0;
  if (currentState == STATE_STARTUP_DELAY) {
    remainingSec = (long)(params.timeDelay_mins * 60000UL) - (long)(millis() - stateTimer);
  } else if (currentState == STATE_ORP_REST) {
    remainingSec = (long)(params.orpRestDelay_mins * 60000UL) - (long)(millis() - stateTimer);
  }

  if (remainingSec < 0) remainingSec = 0;
  remainingSec /= 1000;

  if (currentState == STATE_RUNNING) {
    if (isAltScreen) {
      int v = currentV;
      uint8_t dataV[] = {
          v > 99 ? display.encodeDigit(v / 100) : BLANK,
          display.encodeDigit((v / 10) % 10),
          display.encodeDigit(v % 10),
          LTR_U};
      display.setSegments(dataV);
      setDisplay4((v >= 100) ? ('0' + v / 100) : ' ', '0' + (v / 10) % 10, '0' + v % 10, 'U');
    } else {
      display.showNumberDec(currentA_mA, false, 4, 0);
      setDisplayNum(currentA_mA);
    }
  }
  else if (currentState == STATE_FAULT) {
    uint8_t dataErr[] = {LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault)};
    display.setSegments(dataErr);
    setDisplay4('E', 'r', 'r', '0' + currentFault);
  }
  else if (currentState == STATE_STARTUP_DELAY || currentState == STATE_ORP_REST) {
    if (remainingSec > 60) {
      if (isAltScreen && currentFault != FAULT_NONE) {
        uint8_t dataErr[] = {LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault)};
        display.setSegments(dataErr);
        setDisplay4('E', 'r', 'r', '0' + currentFault);
      } else {
        int m = (remainingSec / 60) + 1;
        display.showNumberDec(m, false, 4, 0);
        setDisplay4(' ', 'd', (m >= 10) ? ('0' + m / 10) : ' ', '0' + m % 10);
      }
    } else {
      display.showNumberDec(remainingSec, false, 4, 0);
      int s = remainingSec;
      setDisplay4(' ', 's', (s >= 10) ? ('0' + s / 10) : ' ', '0' + s % 10);
    }
  }
  else if (currentState == STATE_LOCKOUT) {
    if (isAltScreen) {
      uint8_t dataErr[] = {LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault)};
      display.setSegments(dataErr);
      setDisplay4('E', 'r', 'r', '0' + currentFault);
    } else {
      uint8_t locReason = (currentFault == FAULT_LRA) ? 1 : 2;
      uint8_t dataLoc[] = {LTR_L, LTR_O, LTR_C, display.encodeDigit(locReason)};
      display.setSegments(dataLoc);
      setDisplay4('L', 'O', 'C', '0' + locReason);
    }
  }
}

// ==========================================================
// BUTTONS (shared by Setting and Calibration)
// 1 = Plus, 2 = Menu/OK, 3 = Minus. Chirp on every press.
// ==========================================================
struct ButtonEvents {
  uint8_t rawBtn;
  uint8_t clickEvent;
  bool    longPressEvent;
};

static ButtonEvents readButtons(uint8_t &lastBtn, unsigned long &btnPressTime, bool &isLongPressHandled) {
  ButtonEvents e = {0, 0, false};

  int adc = analogRead(PIN_BUTTONS);
  if (adc < 240)
    e.rawBtn = 1;
  else if (adc >= 240 && adc <= 420)
    e.rawBtn = 2;
  else if (adc >= 420 && adc <= 650)
    e.rawBtn = 3;

  if (e.rawBtn != lastBtn) {
    if (e.rawBtn != 0) {
      btnPressTime = millis();
      isLongPressHandled = false;
      triggerButtonChirp();
    } else if (!isLongPressHandled && (millis() - btnPressTime > 50)) {
      e.clickEvent = lastBtn;
    }
    lastBtn = e.rawBtn;
  } else if (e.rawBtn == 2 && !isLongPressHandled && (millis() - btnPressTime > 1000)) {
    e.longPressEvent = true;
    isLongPressHandled = true;
  }
  return e;
}

void handleSettingMode() {
  display.setBrightness(4);
  static uint8_t menuState = 0;   // 0 wait release, 1 "SEtt", 2 menu list, 3 edit
  static int8_t currentMenu = 1;  // 1-20 parameters, 21 SAUE, 22 rSt
  static int32_t editValue = 0;

  static uint8_t lastBtn = 0;
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;

  ButtonEvents btn = readButtons(lastBtn, btnPressTime, isLongPressHandled);

  switch (menuState) {
  case 0:
    display.setSegments(MSG_SETT);
    if (btn.rawBtn == 0) menuState = 1;
    break;

  case 1:
    display.setSegments(MSG_SETT);
    if (btn.clickEvent == 2) menuState = 2;
    break;

  case 2:
    if (currentMenu <= 20) {
      uint8_t dataS[] = {
          LTR_S,
          SEG_G,
          (currentMenu >= 10) ? display.encodeDigit(currentMenu / 10) : BLANK,
          display.encodeDigit(currentMenu % 10)};
      display.setSegments(dataS);
    }
    else if (currentMenu == 21) display.setSegments(MSG_SAVE);
    else if (currentMenu == 22) display.setSegments(MSG_RST);

    if (btn.clickEvent == 1 && currentMenu < 22) currentMenu++;
    if (btn.clickEvent == 3 && currentMenu > 1)  currentMenu--;

    if (btn.clickEvent == 2) {
      if (currentMenu == 21) {
        display.setSegments(MSG_OK);
        saveParams();
        delay(1000);
        restartSystem();
      } else if (currentMenu == 22) {
        display.setSegments(MSG_OK);
        resetToDefaultParams();
        delay(1000);
        restartSystem();
      } else {
        editValue = getParamVal(currentMenu - 1);
        menuState = 3;
      }
    }
    break;

  case 3: {
    display.showNumberDec(editValue, false, 4, 0);
    uint8_t idx = currentMenu - 1;

    if (btn.clickEvent == 1) editValue = stepParamVal(idx, editValue, +1);
    if (btn.clickEvent == 3) editValue = stepParamVal(idx, editValue, -1);

    if (btn.clickEvent == 2 || btn.longPressEvent) {
      setParamVal(idx, editValue);
      menuState = 2;
    }
    break;
  }
  }
}

void handleCalibrationMode() {
  display.setBrightness(4);
  static uint8_t menuState = 0;   // 0 wait release, 1 "CAL", 2 menu list, 3 live view / edit
  static int8_t currentMenu = 1;  // 1 U-rt, 2 A-rt, 3 t-rt, 4 C-1, 5 C-2, 6 C-3, 7 SAUE
  static int32_t editValue = 0;

  static uint8_t lastBtn = 0;
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;

  ButtonEvents btn = readButtons(lastBtn, btnPressTime, isLongPressHandled);

  switch (menuState) {
  case 0:
    display.setSegments(MSG_CAL);
    if (btn.rawBtn == 0) menuState = 1;
    break;

  case 1:
    display.setSegments(MSG_CAL);
    if (btn.clickEvent == 2) menuState = 2;
    break;

  case 2:
    if      (currentMenu == 1) display.setSegments(MSG_URT);
    else if (currentMenu == 2) display.setSegments(MSG_ART);
    else if (currentMenu == 3) display.setSegments(MSG_TRT);
    else if (currentMenu == 4) display.setSegments(MSG_C1);
    else if (currentMenu == 5) display.setSegments(MSG_C2);
    else if (currentMenu == 6) display.setSegments(MSG_C3);
    else if (currentMenu == 7) display.setSegments(MSG_SAVE);

    if (btn.clickEvent == 1 && currentMenu < 7) currentMenu++;
    if (btn.clickEvent == 3 && currentMenu > 1) currentMenu--;

    if (btn.clickEvent == 2) {
      if (currentMenu == 7) {
        display.setSegments(MSG_OK);
        saveParams();
        delay(1000);
        menuState = 2;   // power off / remove jumper to leave
      } else {
        if      (currentMenu == 4) editValue = params.voltageMultiplier_x1000;
        else if (currentMenu == 5) editValue = params.currentMultiplier_x1000;
        menuState = 3;
      }
    }
    break;

  case 3: {
    static unsigned long lastLiveUpdate = 0;
    static int displayedLiveVal = 0;

    // Live view refreshed every 250 ms (same as LGT)
    if (millis() - lastLiveUpdate >= 250) {
      lastLiveUpdate = millis();
      if      (currentMenu == 1) displayedLiveVal = sysVolt;
      else if (currentMenu == 2) displayedLiveVal = sysAmp_mA;
      else if (currentMenu == 3) displayedLiveVal = sysTemp1;   // 0 until NTC hardware exists
      else if (currentMenu == 4 || currentMenu == 5) displayedLiveVal = editValue;
      else if (currentMenu == 6) displayedLiveVal = 0;          // temp offset: locked 0 for now
    }

    display.showNumberDec(displayedLiveVal, false, 4, 0);

    // C-1 / C-2: 0.001 per click, same step as the LGT version
    if (currentMenu == 4 || currentMenu == 5) {
      if (btn.clickEvent == 1) editValue += 1;
      if (btn.clickEvent == 3) editValue -= 1;
      if (editValue > 5000) editValue = 5000;
      if (editValue < 1)    editValue = 1;
    }

    if (btn.clickEvent == 2 || btn.longPressEvent) {
      if (currentMenu == 4) params.voltageMultiplier_x1000 = (uint16_t)editValue;
      if (currentMenu == 5) params.currentMultiplier_x1000 = (uint16_t)editValue;
      menuState = 2;
    }
    break;
  }
  }
}
