#include "UI.h"
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"
#include <stddef.h> // For offsetof()

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
const uint8_t LTR_P = SEG_A | SEG_B | SEG_E | SEG_F | SEG_G;
const uint8_t BLANK = 0x00;

static const uint8_t MSG_SETT[] = {LTR_S, LTR_E, LTR_t, LTR_t};
static const uint8_t MSG_SAVE[] = {LTR_S, LTR_A, LTR_U, LTR_E};
static const uint8_t MSG_OK[] = {LTR_O, LTR_K, BLANK, BLANK};
static const uint8_t MSG_RST[] = {LTR_r, LTR_S, LTR_t, BLANK};
static const uint8_t MSG_CAL[] = {LTR_C, LTR_A, LTR_L, BLANK};
static const uint8_t MSG_URT[] = {LTR_U, SEG_G, LTR_r, LTR_t};
static const uint8_t MSG_ART[] = {LTR_A, SEG_G, LTR_r, LTR_t};
static const uint8_t MSG_C1[] = {LTR_C, SEG_G, BLANK, 0x06};
static const uint8_t MSG_C2[] = {LTR_C, SEG_G, BLANK, 0x5B};

// ==========================================
// COMPACT MENU TABLE
// ==========================================
struct ParamDef
{
  uint8_t offset;
  uint8_t size; // 1 or 2 bytes
  int16_t min_ui;
  int16_t max_ui;
  uint8_t step_ui;
  int8_t scale; // >0: ui = raw * scale. <0: ui = raw / (-scale)
};

const ParamDef menuTable[20] = {
    {offsetof(SystemParams, timeDelay_mins), 1, 2, 10, 1, 1},           // S-01
    {offsetof(SystemParams, ovpLimit), 1, 230, 260, 10, 1},             // S-02
    {offsetof(SystemParams, uvpLimit), 1, 150, 200, 10, 1},             // S-03
    {offsetof(SystemParams, ovpUvpDelay), 1, 3, 10, 1, 1},              // S-04
    {offsetof(SystemParams, voltageHysteresis), 1, 10, 40, 1, 1},       // S-05
    {offsetof(SystemParams, lraLimit_mA), 2, 800, 5000, 100, 1},        // S-06
    {offsetof(SystemParams, lraTime), 1, 1, 5, 1, 1},                   // S-07
    {offsetof(SystemParams, lraRetryLimit), 1, 2, 5, 1, 1},             // S-08
    {offsetof(SystemParams, ocpLimit_mA), 2, 300, 4500, 50, 1},         // S-09
    {offsetof(SystemParams, ocpDelay), 1, 2, 10, 1, 1},                 // S-10
    {offsetof(SystemParams, ucpLimit_x10), 1, 0, 100, 5, 10},           // S-11
    {offsetof(SystemParams, minCurrentThreshold_x10), 1, 0, 30, 5, 10}, // S-12
    {offsetof(SystemParams, ntc1Enable), 1, 0, 1, 1, 1},                // S-13
    {offsetof(SystemParams, otpLimit), 1, 70, 90, 1, 1},                // S-14
    {offsetof(SystemParams, otpHysteresis), 1, 5, 20, 1, 1},            // S-15
    {offsetof(SystemParams, ntc2Enable), 1, 0, 1, 1, 1},                // S-16
    {offsetof(SystemParams, refFlowDelay_mins), 1, 2, 10, 1, 1},        // S-17
    {offsetof(SystemParams, flowFaultRetryLimit), 1, 2, 5, 1, 1},       // S-18
    {offsetof(SystemParams, orpTimeLimit_hours), 1, 2, 12, 1, 1},       // S-19
    {offsetof(SystemParams, orpRestDelay_mins), 1, 20, 60, 5, 1}        // S-20
};

int32_t getParamVal(uint8_t idx)
{
  const uint8_t *p = (const uint8_t *)&params + menuTable[idx].offset;
  int32_t raw = (menuTable[idx].size == 1) ? *(const uint8_t *)p : *(const uint16_t *)p;
  if (menuTable[idx].scale > 0)
    return raw * menuTable[idx].scale;
  return raw / (-menuTable[idx].scale);
}

void setParamVal(uint8_t idx, int32_t ui)
{
  uint8_t *p = (uint8_t *)&params + menuTable[idx].offset;
  int32_t raw = (menuTable[idx].scale > 0) ? (ui / menuTable[idx].scale) : (ui * -menuTable[idx].scale);
  if (menuTable[idx].size == 1)
    *(uint8_t *)p = (uint8_t)raw;
  else
    *(uint16_t *)p = (uint16_t)raw;
}

// ==========================================
// INIT UI
// ==========================================
void initUI()
{
  display.setBrightness(5);
  const uint8_t msgSCP[] = {LTR_S, LTR_C, 0x73, BLANK};
  display.setSegments(msgSCP);
  delay(1500);
  display.clear();
}

// ==========================================
// UPDATE DISPLAY
// ==========================================
void updateDisplay(float currentV, float currentA)
{
  static unsigned long brightTimer = millis();
  static FaultType lastDisplayFault = FAULT_NONE;
  static SystemState lastDisplayState = STATE_STARTUP_DELAY;

  if (currentFault != lastDisplayFault)
  {
    lastDisplayFault = currentFault;
    if (currentFault != FAULT_NONE)
      brightTimer = millis();
  }

  if (currentState != lastDisplayState)
  {
    lastDisplayState = currentState;
    if (currentState == STATE_STARTUP_DELAY)
      brightTimer = millis();
  }

  if (millis() - brightTimer <= 10000UL)
    display.setBrightness(4);
  else
    display.setBrightness(1);

  bool isAltScreen = (millis() / 2000) % 2 == 1;

  long remainingSec = 0;
  if (currentState == STATE_STARTUP_DELAY)
  {
    remainingSec = (long)(params.timeDelay_mins * 60000UL) - (long)(millis() - stateTimer);
  }
  else if (currentState == STATE_ORP_REST)
  {
    remainingSec = (long)(params.orpRestDelay_mins * 60000UL) - (long)(millis() - stateTimer);
  }

  if (remainingSec < 0)
    remainingSec = 0;
  remainingSec /= 1000;

  if (currentState == STATE_RUNNING)
  {
    if (isAltScreen)
    {
      int v = (int)currentV;
      uint8_t dataV[] = {
          v > 99 ? display.encodeDigit(v / 100) : BLANK,
          display.encodeDigit((v / 10) % 10),
          display.encodeDigit(v % 10),
          LTR_U};
      display.setSegments(dataV);
    }
    else
    {
      int mA = (int)(currentA * 1000.0f);
      display.showNumberDec(mA, false, 4, 0);
    }
  }
  else if (currentState == STATE_FAULT)
  {
    uint8_t dataErr[] = {LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault)};
    display.setSegments(dataErr);
  }
  else if (currentState == STATE_STARTUP_DELAY || currentState == STATE_ORP_REST)
  {
    if (remainingSec > 60)
    {
      if (isAltScreen && currentFault != FAULT_NONE)
      {
        uint8_t dataErr[] = {LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault)};
        display.setSegments(dataErr);
      }
      else
      {
        display.showNumberDec((remainingSec / 60) + 1, false, 4, 0);
      }
    }
    else
    {
      display.showNumberDec(remainingSec, false, 4, 0);
    }
  }
  else if (currentState == STATE_LOCKOUT)
  {
    if (isAltScreen)
    {
      uint8_t dataErr[] = {LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault)};
      display.setSegments(dataErr);
    }
    else
    {
      uint8_t locReason = (currentFault == FAULT_LRA) ? 1 : 2;
      uint8_t dataLoc[] = {LTR_L, LTR_O, LTR_C, display.encodeDigit(locReason)};
      display.setSegments(dataLoc);
    }
  }
}

// ==========================================
// TABLE-DRIVEN SETTING MENU (22 Positions)
// ==========================================
void handleSettingMode()
{
  display.setBrightness(4);
  static uint8_t menuState = 0;
  static int8_t currentMenu = 1;
  static int32_t editValue = 0;

  static uint8_t lastBtn = 0;
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;

  uint8_t rawBtn = 0;
  int adc = analogRead(PIN_BUTTONS);
  if (adc < 240)
    rawBtn = 1; // Plus
  else if (adc >= 240 && adc <= 420)
    rawBtn = 2; // Menu/OK
  else if (adc >= 420 && adc <= 650)
    rawBtn = 3; // Minus

  uint8_t clickEvent = 0;
  bool longPressEvent = false;

  if (rawBtn != lastBtn)
  {
    if (rawBtn != 0)
    {
      btnPressTime = millis();
      isLongPressHandled = false;
      triggerButtonChirp(); // <--- ADD THIS LINE
    }
    else if (!isLongPressHandled && (millis() - btnPressTime > 50))
    {
      clickEvent = lastBtn;
    }
    lastBtn = rawBtn;
  }
  else if (rawBtn == 2 && !isLongPressHandled && (millis() - btnPressTime > 1000))
  {
    longPressEvent = true;
    isLongPressHandled = true;
  }

  switch (menuState)
  {
  case 0:
    display.setSegments(MSG_SETT);
    if (rawBtn == 0)
      menuState = 1;
    break;

  case 1:
    display.setSegments(MSG_SETT);
    if (clickEvent == 2)
      menuState = 2;
    break;

  case 2:
    if (currentMenu <= 20)
    {
      uint8_t dataS[] = {
          LTR_S,
          SEG_G,
          (currentMenu >= 10) ? display.encodeDigit(currentMenu / 10) : BLANK,
          display.encodeDigit(currentMenu % 10)};
      display.setSegments(dataS);
    }
    else if (currentMenu == 21)
      display.setSegments(MSG_SAVE);
    else if (currentMenu == 22)
      display.setSegments(MSG_RST);

    if (clickEvent == 1 && currentMenu < 22)
      currentMenu++;
    if (clickEvent == 3 && currentMenu > 1)
      currentMenu--;

    if (clickEvent == 2)
    {
      if (currentMenu == 21)
      {
        display.setSegments(MSG_OK);
        saveParams();
        delay(1000);
        restartSystem();
      }
      else if (currentMenu == 22)
      {
        display.setSegments(MSG_OK);
        resetToDefaultParams();
        delay(1000);
        restartSystem();
      }
      else
      {
        editValue = getParamVal(currentMenu - 1);
        menuState = 3;
      }
    }
    break;

  case 3:
  {
    display.showNumberDec(editValue, false, 4, 0);
    uint8_t idx = currentMenu - 1;

    if (clickEvent == 1)
      editValue += menuTable[idx].step_ui;
    if (clickEvent == 3)
      editValue -= menuTable[idx].step_ui;

    if (editValue < menuTable[idx].min_ui)
      editValue = menuTable[idx].min_ui;
    if (editValue > menuTable[idx].max_ui)
      editValue = menuTable[idx].max_ui;

    if (clickEvent == 2 || longPressEvent)
    {
      setParamVal(idx, editValue);
      menuState = 2;
    }
    break;
  }
  }
}

// ==========================================
// CALIBRATION MENU — (U-rt, A-rt, C-1, C-2, SAUE)
// ==========================================
void handleCalibrationMode()
{
  display.setBrightness(4);
  static uint8_t menuState = 0;
  static int8_t currentMenu = 1;
  static int32_t editValue = 0;

  static uint8_t lastBtn = 0;
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;

  uint8_t rawBtn = 0;
  int adc = analogRead(PIN_BUTTONS);
  if (adc < 240)
    rawBtn = 1;
  else if (adc >= 240 && adc <= 420)
    rawBtn = 2;
  else if (adc >= 420 && adc <= 650)
    rawBtn = 3;

  uint8_t clickEvent = 0;
  bool longPressEvent = false;

  if (rawBtn != lastBtn)
  {
    if (rawBtn != 0)
    {
      btnPressTime = millis();
      isLongPressHandled = false;
      triggerButtonChirp(); // <--- ADD THIS LINE
    }
    else if (!isLongPressHandled && (millis() - btnPressTime > 50))
    {
      clickEvent = lastBtn;
    }
    lastBtn = rawBtn;
  }
  else if (rawBtn == 2 && !isLongPressHandled && (millis() - btnPressTime > 1000))
  {
    longPressEvent = true;
    isLongPressHandled = true;
  }

  switch (menuState)
  {
  case 0:
    display.setSegments(MSG_CAL);
    if (rawBtn == 0)
      menuState = 1;
    break;

  case 1:
    display.setSegments(MSG_CAL);
    if (clickEvent == 2)
      menuState = 2;
    break;

  case 2:
    if (currentMenu == 1)
      display.setSegments(MSG_URT);
    else if (currentMenu == 2)
      display.setSegments(MSG_ART);
    else if (currentMenu == 3)
      display.setSegments(MSG_C1);
    else if (currentMenu == 4)
      display.setSegments(MSG_C2);
    else if (currentMenu == 5)
      display.setSegments(MSG_SAVE);

    if (clickEvent == 1 && currentMenu < 5)
      currentMenu++;
    if (clickEvent == 3 && currentMenu > 1)
      currentMenu--;

    if (clickEvent == 2)
    {
      if (currentMenu == 5)
      {
        display.setSegments(MSG_OK);
        saveParams();
        delay(1000);
        menuState = 2;
      }
      else
      {
        if (currentMenu == 3)
          editValue = params.voltageMultiplier_x100;
        else if (currentMenu == 4)
          editValue = params.currentMultiplier_x100;
        menuState = 3;
      }
    }
    break;

  case 3:
  {
    static unsigned long lastLiveUpdate = 0;
    static int displayedLiveVal = 0;

    if (millis() - lastLiveUpdate >= 250)
    {
      lastLiveUpdate = millis();
      if (currentMenu == 1)
        displayedLiveVal = (int)sysVolt;
      else if (currentMenu == 2)
        displayedLiveVal = (int)(sysAmp * 1000.0f);
      else if (currentMenu == 3 || currentMenu == 4)
        displayedLiveVal = editValue * 10; // show 342 as 3420
    }

    display.showNumberDec(displayedLiveVal, false, 4, 0);

    if (currentMenu == 3 || currentMenu == 4)
    {
      if (clickEvent == 1)
        editValue += 1;
      if (clickEvent == 3)
        editValue -= 1;
      if (editValue > 500)
        editValue = 500;
      if (editValue < 1)
        editValue = 1;
    }

    if (clickEvent == 2 || longPressEvent)
    {
      if (currentMenu == 3)
        params.voltageMultiplier_x100 = (uint8_t)editValue;
      if (currentMenu == 4)
        params.currentMultiplier_x100 = (uint8_t)editValue;
      menuState = 2;
    }
    break;
  }
  }
}