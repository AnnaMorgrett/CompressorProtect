#include "UI.h"
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"
#include <stddef.h>

// CH32V003 PC3 = TM_DAT (bit 3), PC4 = TM_CLK (bit 4)
#define TM_DAT_MASK (1U << 3)
#define TM_CLK_MASK (1U << 4)

static const uint8_t DIGIT_SEG[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

#define SEG_A 0x01
#define SEG_B 0x02
#define SEG_C 0x04
#define SEG_D 0x08
#define SEG_E 0x10
#define SEG_F 0x20
#define SEG_G 0x40

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
static const uint8_t MSG_C1[]   = {LTR_C, SEG_G, BLANK, DIGIT_SEG[1]};
static const uint8_t MSG_C2[]   = {LTR_C, SEG_G, BLANK, DIGIT_SEG[2]};
static const uint8_t MSG_C3[]   = {LTR_C, SEG_G, BLANK, DIGIT_SEG[3]};
static const uint8_t MSG_PC[]   = {BLANK, LTR_P, LTR_C, BLANK};

static uint8_t tmBrightness = 0x8C;

static void tmDelay() { delayMicroseconds(5); }

static void tmStart() {
  GPIOC->BSHR = TM_CLK_MASK | TM_DAT_MASK;
  tmDelay();
  GPIOC->BCR = TM_DAT_MASK;
  tmDelay();
  GPIOC->BCR = TM_CLK_MASK;
}

static void tmStop() {
  GPIOC->BCR = TM_CLK_MASK | TM_DAT_MASK;
  tmDelay();
  GPIOC->BSHR = TM_CLK_MASK;
  tmDelay();
  GPIOC->BSHR = TM_DAT_MASK;
  tmDelay();
}

static void tmWriteByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    GPIOC->BCR = TM_CLK_MASK;
    if (b & 0x01) GPIOC->BSHR = TM_DAT_MASK;
    else          GPIOC->BCR  = TM_DAT_MASK;
    tmDelay();
    GPIOC->BSHR = TM_CLK_MASK;
    tmDelay();
    b >>= 1;
  }
  GPIOC->BCR = TM_CLK_MASK;
  pinMode(PIN_TM_DAT, INPUT_PULLUP);
  tmDelay();
  GPIOC->BSHR = TM_CLK_MASK;
  tmDelay();
  GPIOC->BCR = TM_CLK_MASK;
  pinMode(PIN_TM_DAT, OUTPUT);
}

static void tmSetBrightness(uint8_t b) {
  tmBrightness = 0x88 | (b & 0x07);
}

static void tmShowSegments(const uint8_t segs[4]) {
  tmStart(); tmWriteByte(0x40); tmStop();
  tmStart(); tmWriteByte(0xC0);
  for (uint8_t i = 0; i < 4; i++) tmWriteByte(segs[i]);
  tmStop();
  tmStart(); tmWriteByte(tmBrightness); tmStop();
}

static void tmShowNumber(int32_t num) {
  if (num < 0) num = 0;
  uint8_t segs[4] = {BLANK, BLANK, BLANK, DIGIT_SEG[num % 10]};
  num /= 10;
  if (num > 0) { segs[2] = DIGIT_SEG[num % 10]; num /= 10; }
  if (num > 0) { segs[1] = DIGIT_SEG[num % 10]; num /= 10; }
  if (num > 0) { segs[0] = DIGIT_SEG[num % 10]; }
  tmShowSegments(segs);
}

const ParamDef menuTable[20] = {
  {offsetof(SystemParams, timeDelay_mins),           1,   2,   10,   1,  1, false},
  {offsetof(SystemParams, ovpLimit_div2),            1, 230,  260,  10,  2, false},
  {offsetof(SystemParams, uvpLimit),                 1, 150,  200,  10,  1, false},
  {offsetof(SystemParams, ovpUvpDelay),              1,   3,   10,   1,  1, false},
  {offsetof(SystemParams, voltageHysteresis),        1,  10,   40,   1,  1, false},
  {offsetof(SystemParams, lraLimit_mA),              2, 800, 5000, 100,  1, false},
  {offsetof(SystemParams, lraTime),                  1,   1,    5,   1,  1, false},
  {offsetof(SystemParams, lraRetryLimit),            1,   2,    5,   1,  1, false},
  {offsetof(SystemParams, ocpLimit_mA),              2, 300, 4500,  50,  1, false},
  {offsetof(SystemParams, ocpDelay),                 1,   2,   10,   1,  1, false},
  {offsetof(SystemParams, ucpLimit_x10),             1, 200, 1000,  50, 10, true },
  {offsetof(SystemParams, minCurrentThreshold_x10),  1, 150,  300,  50, 10, true },
  {0,                                                0,   0,    0,   1,  1, false},
  {offsetof(SystemParams, otpLimit),                 1,  70,   90,   1,  1, false},
  {offsetof(SystemParams, otpHysteresis),            1,   5,   20,   1,  1, false},
  {0,                                                0,   0,    0,   1,  1, false},
  {offsetof(SystemParams, refFlowDelay_mins),        1,   2,   10,   1,  1, false},
  {offsetof(SystemParams, flowFaultRetryLimit),      1,   2,    5,   1,  1, false},
  {offsetof(SystemParams, orpTimeLimit_hours),       1,   2,   12,   1,  1, false},
  {offsetof(SystemParams, orpRestDelay_mins),        1,  20,   60,   1,  1, false}
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

static void showErrDisplay(uint8_t code) {
  uint8_t dataErr[4] = {LTR_E, LTR_r, LTR_r, DIGIT_SEG[code]};
  tmShowSegments(dataErr);
  setDisplay4('E', 'r', 'r', '0' + code);
}

void initUI() {
  pinMode(PIN_TM_CLK, OUTPUT);
  pinMode(PIN_TM_DAT, OUTPUT);
  tmSetBrightness(5);
  tmShowSegments(MSG_SCP);
  delay(1500);
  tmShowSegments(MSG_PRO);
  delay(1000);
  static const uint8_t CLR[4] = {BLANK, BLANK, BLANK, BLANK};
  tmShowSegments(CLR);
}

void showSerialMenuDisplay() {
  tmSetBrightness(4);
  tmShowSegments(MSG_PC);
  setDisplay4(' ', 'P', 'C', ' ');
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

  tmSetBrightness((millis() - brightTimer <= 10000UL) ? 4 : 1);

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
      uint8_t dataV[4] = {
        v > 99 ? DIGIT_SEG[v / 100] : BLANK,
        DIGIT_SEG[(v / 10) % 10],
        DIGIT_SEG[v % 10],
        LTR_U
      };
      tmShowSegments(dataV);
      setDisplay4((v >= 100) ? ('0' + v / 100) : ' ', '0' + (v / 10) % 10, '0' + v % 10, 'U');
    } else {
      tmShowNumber(currentA_mA);
      setDisplayNum(currentA_mA);
    }
  }
  else if (currentState == STATE_FAULT) {
    showErrDisplay(currentFault);
  }
  else if (currentState == STATE_STARTUP_DELAY || currentState == STATE_ORP_REST) {
    if (remainingSec > 60) {
      if (isAltScreen && currentFault != FAULT_NONE) {
        showErrDisplay(currentFault);
      } else {
        int m = (remainingSec / 60) + 1;
        tmShowNumber(m);
        setDisplay4(' ', 'd', (m >= 10) ? ('0' + m / 10) : ' ', '0' + m % 10);
      }
    } else {
      tmShowNumber(remainingSec);
      int s = remainingSec;
      setDisplay4(' ', 's', (s >= 10) ? ('0' + s / 10) : ' ', '0' + s % 10);
    }
  }
  else if (currentState == STATE_LOCKOUT) {
    if (isAltScreen) {
      showErrDisplay(currentFault);
    } else {
      uint8_t locReason = (currentFault == FAULT_LRA) ? 1 : 2;
      uint8_t dataLoc[4] = {LTR_L, LTR_O, LTR_C, DIGIT_SEG[locReason]};
      tmShowSegments(dataLoc);
      setDisplay4('L', 'O', 'C', '0' + locReason);
    }
  }
}

struct ButtonEvents {
  uint8_t rawBtn;
  uint8_t clickEvent;
  bool    longPressEvent;
};

static ButtonEvents readButtons(uint8_t &lastBtn, unsigned long &btnPressTime, bool &isLongPressHandled) {
  ButtonEvents e = {0, 0, false};
  int adc = analogRead(PIN_BUTTONS);
  if (adc < 240)                        e.rawBtn = 1;
  else if (adc >= 240 && adc <= 420)    e.rawBtn = 2;
  else if (adc >= 420 && adc <= 650)    e.rawBtn = 3;

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
  tmSetBrightness(4);
  static uint8_t menuState = 0;
  static int8_t currentMenu = 1;
  static int32_t editValue = 0;
  static uint8_t lastBtn = 0;
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;

  ButtonEvents btn = readButtons(lastBtn, btnPressTime, isLongPressHandled);

  switch (menuState) {
  case 0:
    tmShowSegments(MSG_SETT);
    if (btn.rawBtn == 0) menuState = 1;
    break;
  case 1:
    tmShowSegments(MSG_SETT);
    if (btn.clickEvent == 2) menuState = 2;
    break;
  case 2:
    if (currentMenu <= 20) {
      uint8_t dataS[4] = {
        LTR_S, SEG_G,
        (currentMenu >= 10) ? DIGIT_SEG[currentMenu / 10] : BLANK,
        DIGIT_SEG[currentMenu % 10]
      };
      tmShowSegments(dataS);
    }
    else if (currentMenu == 21) tmShowSegments(MSG_SAVE);
    else if (currentMenu == 22) tmShowSegments(MSG_RST);

    if (btn.clickEvent == 1 && currentMenu < 22) currentMenu++;
    if (btn.clickEvent == 3 && currentMenu > 1)  currentMenu--;

    if (btn.clickEvent == 2) {
      if (currentMenu == 21 || currentMenu == 22) {
        tmShowSegments(MSG_OK);
        if (currentMenu == 21) saveParams();
        else                   resetToDefaultParams();
        delay(1000);
        restartSystem();
      } else {
        editValue = getParamVal(currentMenu - 1);
        menuState = 3;
      }
    }
    break;
  case 3: {
    tmShowNumber(editValue);
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
  tmSetBrightness(4);
  static uint8_t menuState = 0;
  static int8_t currentMenu = 1;
  static int32_t editValue = 0;
  static uint8_t lastBtn = 0;
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;

  ButtonEvents btn = readButtons(lastBtn, btnPressTime, isLongPressHandled);

  switch (menuState) {
  case 0:
    tmShowSegments(MSG_CAL);
    if (btn.rawBtn == 0) menuState = 1;
    break;
  case 1:
    tmShowSegments(MSG_CAL);
    if (btn.clickEvent == 2) menuState = 2;
    break;
  case 2:
    if      (currentMenu == 1) tmShowSegments(MSG_URT);
    else if (currentMenu == 2) tmShowSegments(MSG_ART);
    else if (currentMenu == 3) tmShowSegments(MSG_TRT);
    else if (currentMenu == 4) tmShowSegments(MSG_C1);
    else if (currentMenu == 5) tmShowSegments(MSG_C2);
    else if (currentMenu == 6) tmShowSegments(MSG_C3);
    else if (currentMenu == 7) tmShowSegments(MSG_SAVE);

    if (btn.clickEvent == 1 && currentMenu < 7) currentMenu++;
    if (btn.clickEvent == 3 && currentMenu > 1) currentMenu--;

    if (btn.clickEvent == 2) {
      if (currentMenu == 7) {
        tmShowSegments(MSG_OK);
        saveParams();
        delay(1000);
        menuState = 2;
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
    if (millis() - lastLiveUpdate >= 250) {
      lastLiveUpdate = millis();
      if      (currentMenu == 1) displayedLiveVal = sysVolt;
      else if (currentMenu == 2) displayedLiveVal = sysAmp_mA;
      else if (currentMenu == 3) displayedLiveVal = sysTemp1;
      else if (currentMenu == 4 || currentMenu == 5) displayedLiveVal = editValue;
      else if (currentMenu == 6) displayedLiveVal = 0;
    }
    tmShowNumber(displayedLiveVal);
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