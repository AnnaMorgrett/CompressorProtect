#include "display_manager.h"

static const uint8_t SEG_DIGITS[] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

#define SEG_MINUS  0x40
#define SEG_BLANK  0x00
#define SEG_P      0x73
#define SEG_E      0x79
#define SEG_L      0x38
#define SEG_O      0x3F
#define SEG_C      0x39
#define SEG_K      0x76
#define SEG_S      0x6D
#define SEG_A      0x77
#define SEG_U      0x3E
#define SEG_N      0x37
#define SEG_R      0x50

struct ParamDef {
  uint16_t minVal;
  uint16_t maxVal;
  uint8_t  stepVal;
};

static const ParamDef PARAM_DEFS[5] = {
  { CFG_OVP_MIN,          CFG_OVP_MAX,          CFG_OVP_STEP },
  { CFG_UVP_MIN,          CFG_UVP_MAX,          CFG_UVP_STEP },
  { CFG_OCP_RUN_MIN_X10,  CFG_OCP_RUN_MAX_X10,  CFG_OCP_RUN_STEP_X10 },
  { CFG_OCP_LRA_MIN_X10,  CFG_OCP_LRA_MAX_X10,  CFG_OCP_LRA_STEP_X10 },
  { CFG_DELAY_MIN_X10,    CFG_DELAY_MAX_X10,    CFG_DELAY_STEP_X10 }
};

static DisplayMode g_dispMode = DISP_MODE_NORMAL_ROTATE;
static uint8_t g_homePage = 0;
static unsigned long g_lastPageRotateTime = 0;
static unsigned long g_lastFaultToggleTime = 0;
static bool g_faultShowCode = true;

static uint8_t g_menuParamIndex = 1;
static uint16_t g_menuEditValue = 0;
static unsigned long g_saveConfirmTimer = 0;

static inline void clkHigh() { digitalWrite(PIN_TM_CLK, HIGH); }
static inline void clkLow()  { digitalWrite(PIN_TM_CLK, LOW); }
static inline void dioHigh() { digitalWrite(PIN_TM_DIO, HIGH); }
static inline void dioLow()  { digitalWrite(PIN_TM_DIO, LOW); }

static inline void dioOutput() { pinMode(PIN_TM_DIO, OUTPUT); }
static inline void dioInput()  { pinMode(PIN_TM_DIO, INPUT_PULLUP); }

static void __attribute__((noinline)) tm_delay() {
  for (volatile uint8_t i = 0; i < 4; i++);
}

static void tm_start() {
  dioOutput();
  dioHigh();
  clkHigh();
  tm_delay();
  dioLow();
  tm_delay();
  clkLow();
  tm_delay();
}

static void tm_stop() {
  dioOutput();
  clkLow();
  dioLow();
  tm_delay();
  clkHigh();
  tm_delay();
  dioHigh();
  tm_delay();
}

static void tm_writeByte(uint8_t b) {
  dioOutput();
  for (uint8_t i = 0; i < 8; i++) {
    clkLow();
    tm_delay();
    if (b & 0x01) dioHigh(); else dioLow();
    tm_delay();
    clkHigh();
    tm_delay();
    b >>= 1;
  }
  clkLow();
  dioInput();
  tm_delay();
  clkHigh();
  tm_delay();
  clkLow();
  dioOutput();
  tm_delay();
}

static void tm_setSegments(const uint8_t segments[4]) {
  tm_start();
  tm_writeByte(0x40);
  tm_stop();

  tm_start();
  tm_writeByte(0xC0);
  for (uint8_t i = 0; i < 4; i++) {
    tm_writeByte(segments[i]);
  }
  tm_stop();

  tm_start();
  tm_writeByte(0x8F);
  tm_stop();
}

static void __attribute__((noinline)) renderNumber4(uint16_t val, uint8_t dpIndex, uint8_t segs[4], bool leadingZeros) {
  for (int8_t i = 3; i >= 0; i--) {
    uint8_t d = val % 10;
    val /= 10;
    if (d == 0 && val == 0 && i < 3 && !leadingZeros) {
      segs[i] = SEG_BLANK;
    } else {
      segs[i] = SEG_DIGITS[d];
    }
    if (dpIndex == (uint8_t)i) {
      segs[i] |= 0x80;
    }
  }
}

static uint8_t charToSeg(char c) {
  if (c >= '0' && c <= '9') return SEG_DIGITS[c - '0'];
  if (c == 'E') return SEG_E;
  if (c == 'L') return SEG_L;
  if (c == 'O') return SEG_O;
  if (c == 'C') return SEG_C;
  if (c == 'K') return SEG_K;
  if (c == 'N') return SEG_N;
  if (c == '-') return SEG_MINUS;
  return SEG_BLANK;
}

void displayInit() {
  pinMode(PIN_TM_CLK, OUTPUT);
  pinMode(PIN_TM_DIO, OUTPUT);
  clkHigh();
  dioHigh();

  uint8_t bootSegs[4] = { SEG_MINUS, SEG_MINUS, SEG_MINUS, SEG_MINUS };
  tm_setSegments(bootSegs);
}

bool displayIsInMenu() {
  return (g_dispMode == DISP_MODE_MENU_PARAM || g_dispMode == DISP_MODE_MENU_EDIT || g_dispMode == DISP_MODE_SAVE_CONFIRM);
}

void displayExitMenu() {
  g_dispMode = DISP_MODE_NORMAL_ROTATE;
}

bool displayHandleButton(ButtonEvent ev) {
  if (ev == BTN_EVENT_NONE) return false;

  // 1. Home Telemetry Pages Mode
  if (g_dispMode == DISP_MODE_NORMAL_ROTATE) {
    if (ev == BTN_EVENT_PLUS_SHORT) {
      g_homePage = (g_homePage + 1) % 5;
      g_lastPageRotateTime = millis();
      return true;
    }
    if (ev == BTN_EVENT_MINUS_SHORT) {
      g_homePage = (g_homePage == 0) ? 4 : (g_homePage - 1);
      g_lastPageRotateTime = millis();
      return true;
    }
    if (ev == BTN_EVENT_MENU_LONG) {
      g_dispMode = DISP_MODE_MENU_PARAM;
      g_menuParamIndex = 1;
      return true;
    }
    return false;
  }

  // 2. Menu Parameter Select (P-01 .. P-05)
  if (g_dispMode == DISP_MODE_MENU_PARAM) {
    if (ev == BTN_EVENT_PLUS_SHORT || ev == BTN_EVENT_PLUS_REPEAT) {
      g_menuParamIndex = (g_menuParamIndex >= 5) ? 1 : (g_menuParamIndex + 1);
      return true;
    }
    if (ev == BTN_EVENT_MINUS_SHORT || ev == BTN_EVENT_MINUS_REPEAT) {
      g_menuParamIndex = (g_menuParamIndex <= 1) ? 5 : (g_menuParamIndex - 1);
      return true;
    }
    if (ev == BTN_EVENT_MENU_SHORT) {
      g_dispMode = DISP_MODE_MENU_EDIT;
      SystemConfig& cfg = storageGetConfig();
      switch (g_menuParamIndex) {
        case 1: g_menuEditValue = cfg.ovpCutoff; break;
        case 2: g_menuEditValue = cfg.uvpCutoff; break;
        case 3: g_menuEditValue = cfg.currentRunX10; break;
        case 4: g_menuEditValue = cfg.currentLraX10; break;
        case 5: g_menuEditValue = cfg.delayTimerX10; break;
      }
      return true;
    }
    if (ev == BTN_EVENT_MENU_LONG) {
      g_dispMode = DISP_MODE_NORMAL_ROTATE;
      return true;
    }
  }

  // 3. Menu Parameter Edit Value
  if (g_dispMode == DISP_MODE_MENU_EDIT) {
    const ParamDef& def = PARAM_DEFS[g_menuParamIndex - 1];
    if (ev == BTN_EVENT_PLUS_SHORT || ev == BTN_EVENT_PLUS_REPEAT) {
      if ((g_menuEditValue + def.stepVal) <= def.maxVal) g_menuEditValue += def.stepVal;
      return true;
    }
    if (ev == BTN_EVENT_MINUS_SHORT || ev == BTN_EVENT_MINUS_REPEAT) {
      if (g_menuEditValue >= (def.minVal + def.stepVal)) g_menuEditValue -= def.stepVal;
      return true;
    }
    if (ev == BTN_EVENT_MENU_SHORT) {
      SystemConfig& cfg = storageGetConfig();
      switch (g_menuParamIndex) {
        case 1: cfg.ovpCutoff = (uint8_t)g_menuEditValue; break;
        case 2: cfg.uvpCutoff = (uint8_t)g_menuEditValue; break;
        case 3: cfg.currentRunX10 = (uint8_t)g_menuEditValue; break;
        case 4: cfg.currentLraX10 = (uint8_t)g_menuEditValue; break;
        case 5: cfg.delayTimerX10 = (uint8_t)g_menuEditValue; break;
      }
      storageSaveConfig();
      g_dispMode = DISP_MODE_SAVE_CONFIRM;
      g_saveConfirmTimer = millis();
      return true;
    }
    if (ev == BTN_EVENT_MENU_LONG) {
      g_dispMode = DISP_MODE_MENU_PARAM;
      return true;
    }
  }

  return false;
}

void displayUpdate(SystemState state, FaultCode fault, const SensorData& sensors, unsigned long timerRemainingMs) {
  static unsigned long lastRenderTime = 0;
  unsigned long now = millis();

  if ((now - lastRenderTime) < 200) {
    return;
  }
  lastRenderTime = now;

  uint8_t segs[4] = { SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK };

  // 1. Save Confirmation "SAUE"
  if (g_dispMode == DISP_MODE_SAVE_CONFIRM) {
    segs[0] = SEG_S; segs[1] = SEG_A; segs[2] = SEG_U; segs[3] = SEG_E;
    tm_setSegments(segs);
    if ((now - g_saveConfirmTimer) >= 1200) {
      g_dispMode = DISP_MODE_MENU_PARAM;
    }
    return;
  }

  // 2. Menu Parameter Select "P-01"
  if (g_dispMode == DISP_MODE_MENU_PARAM) {
    segs[0] = SEG_P;
    segs[1] = SEG_MINUS;
    segs[2] = SEG_DIGITS[0];
    segs[3] = SEG_DIGITS[g_menuParamIndex];
    tm_setSegments(segs);
    return;
  }

  // 3. Menu Parameter Edit Value
  if (g_dispMode == DISP_MODE_MENU_EDIT) {
    if (g_menuParamIndex == 1 || g_menuParamIndex == 2) {
      renderNumber4(g_menuEditValue, 0xFF, segs, false);
    } else if (g_menuParamIndex == 3 || g_menuParamIndex == 4) {
      renderNumber4(g_menuEditValue * 10, 1, segs, false);
    } else if (g_menuParamIndex == 5) {
      renderNumber4(g_menuEditValue * 10, 0xFF, segs, false);
    }
    tm_setSegments(segs);
    return;
  }

  // 4. Fault / Lockout Mode
  if (state == STATE_FAULT_RECOVERY || state == STATE_LOCKOUT || fault != FAULT_NONE) {
    if ((now - g_lastFaultToggleTime) >= 1500) {
      g_lastFaultToggleTime = now;
      g_faultShowCode = !g_faultShowCode;
    }

    if (g_faultShowCode) {
      const char* codeStr = faultGetDisplayCode(fault);
      for (uint8_t i = 0; i < 4; i++) {
        segs[i] = charToSeg(codeStr[i]);
      }
    } else {
      if (state == STATE_LOCKOUT) {
        segs[0] = SEG_L; segs[1] = SEG_O; segs[2] = SEG_C; segs[3] = SEG_K;
      } else {
        uint16_t totalSec = timerRemainingMs / 1000;
        uint8_t mins = totalSec / 60;
        uint8_t secs = totalSec % 60;
        segs[0] = SEG_DIGITS[mins / 10];
        segs[1] = SEG_DIGITS[mins % 10] | 0x80;
        segs[2] = SEG_DIGITS[secs / 10];
        segs[3] = SEG_DIGITS[secs % 10];
      }
    }
    tm_setSegments(segs);
    return;
  }

  // 5. Delay Countdown (MM:SS)
  if (state == STATE_WAIT_MIN_OFF || state == STATE_POWER_QUALIFY) {
    uint16_t totalSec = timerRemainingMs / 1000;
    uint8_t mins = totalSec / 60;
    uint8_t secs = totalSec % 60;
    bool colon = (now % 1000) < 500;

    segs[0] = SEG_DIGITS[mins / 10];
    segs[1] = SEG_DIGITS[mins % 10] | (colon ? 0x80 : 0);
    segs[2] = SEG_DIGITS[secs / 10];
    segs[3] = SEG_DIGITS[secs % 10];
    tm_setSegments(segs);
    return;
  }

  // 6. Normal Rotating Telemetry Pages
  if ((now - g_lastPageRotateTime) >= 4000) {
    g_lastPageRotateTime = now;
    g_homePage = (g_homePage + 1) % 5;
  }

  switch (g_homePage) {
    case 0:
      renderNumber4(sensors.voltageDisplayRMS, 0xFF, segs, false);
      break;

    case 1:
      renderNumber4(sensors.currentMilliAmps / 10, 1, segs, false);
      break;

    case 2:
    case 3:
      segs[0] = SEG_BLANK; segs[1] = SEG_MINUS; segs[2] = SEG_N; segs[3] = SEG_C;
      break;

    case 4:
      segs[0] = SEG_BLANK; segs[1] = SEG_R; segs[2] = SEG_U; segs[3] = SEG_N;
      break;
  }

  tm_setSegments(segs);
}
