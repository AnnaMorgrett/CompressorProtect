#include <Arduino.h>
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"
#include "UI.h"
#include "SoftSerial.h"

#define ENABLE_SERIAL_DEBUG 1

#if ENABLE_SERIAL_DEBUG
static SoftSerial DebugSerial(PIN_DEBUG_TX, PIN_DEBUG_RX);
#define DBG_BEGIN() DebugSerial.begin(9600)
#define DBG_ENABLE_RX() DebugSerial.enableRx(PIN_DEBUG_RX)
#define DBG_PRINT(x) DebugSerial.print(x)
#define DBG_PRINTLN(x) DebugSerial.println(x)
#else
#define DBG_BEGIN() \
  do                \
  {                 \
  } while (0)
#define DBG_ENABLE_RX() \
  do                    \
  {                     \
  } while (0)
#define DBG_PRINT(x) \
  do                 \
  {                  \
  } while (0)
#define DBG_PRINTLN(x) \
  do                   \
  {                    \
  } while (0)
#endif

// ==========================================
// GLOBAL SYSTEM VARIABLES
// ==========================================
OperatingMode currentMode = MODE_NORMAL_PROTECTION;
SystemState currentState = STATE_STARTUP_DELAY;
FaultType currentFault = FAULT_NONE;

uint16_t sysVolt = 0;
uint16_t sysAmp_mA = 0;
int16_t sysTemp1 = 0;
int16_t sysTemp2 = 0;

unsigned long stateTimer = 0;
unsigned long runningTimer = 0;
unsigned long orpTimer = 0;
unsigned long faultConditionTimer = 0;
unsigned long serialTelemetryTimer = 0;

uint8_t lraRetryCount = 0;
uint8_t flowRetryCount = 0;
int16_t initialTempNTC2 = 0;
bool isFlowChecked = false;

// Serial Menu State
bool serialMenuActive = false;
bool serialMenuStream = false;
char serialCmdBuf[24];
uint8_t serialCmdIdx = 0;

// ==========================================
// WATCHDOG & CORE HELPERS
// ==========================================
static inline void kickWatchdog()
{
  IWDG->CTLR = 0xAAAA;
}

static void initWatchdog()
{
  IWDG->CTLR = 0x5555; // Enable register write access
  IWDG->PSCR = 4;      // Prescaler /64 (40 kHz / 64 = 625 Hz)
  IWDG->RLDR = 3999;   // ~6.4 s timeout
  IWDG->CTLR = 0xAAAA; // Reload counter
  IWDG->CTLR = 0xCCCC; // Start IWDG
}

bool serialRxIncoming()
{
#if ENABLE_SERIAL_DEBUG
  return DebugSerial.rxStartDetected();
#else
  return false;
#endif
}

void beepMs(uint16_t ms)
{
#if BUZZER_PASSIVE
  const uint16_t halfPeriodUs = 500000UL / BUZZER_FREQ_HZ;
  unsigned long end = millis() + ms;
  while ((long)(millis() - end) < 0)
  {
    digitalWrite(PIN_BUZZER, HIGH);
    delayMicroseconds(halfPeriodUs);
    digitalWrite(PIN_BUZZER, LOW);
    delayMicroseconds(halfPeriodUs);
    kickWatchdog();
  }
#else
  digitalWrite(PIN_BUZZER, HIGH);
  unsigned long end = millis() + ms;
  while ((long)(millis() - end) < 0)
  {
    kickWatchdog();
    delay(5);
  }
#endif
  digitalWrite(PIN_BUZZER, LOW);
}

void triggerButtonChirp()
{
  beepMs(40);
}

void restartSystem()
{
  DBG_PRINTLN("[Reboot]");
  delay(100);
  IWDG->CTLR = 0x5555;
  IWDG->RLDR = 0;
  IWDG->CTLR = 0xAAAA;
  while (1)
  {
  }
}

void triggerFault(FaultType fault)
{
  if (currentState == STATE_FAULT && currentFault == fault)
    return;
  currentFault = fault;
  currentState = STATE_FAULT;
  digitalWrite(PIN_RELAY, LOW);
  faultConditionTimer = millis();
  DBG_PRINT("\n[FAULT] ");
  DBG_PRINTLN((int)fault);
}

void checkFaultDelay(FaultType fault, uint8_t delaySeconds)
{
  if (millis() - faultConditionTimer >= (delaySeconds * 1000UL))
  {
    triggerFault(fault);
  }
}

bool isVoltageSafe(uint16_t v)
{
  return (v > params.uvpLimit && v < OVP_LIMIT);
}

#if ENABLE_SERIAL_DEBUG
static void printOneDecimal(int16_t value)
{
  DebugSerial.print(value);
  DebugSerial.print(".0");
}

static void printAmps(uint16_t mA)
{
  uint16_t hundredths = (mA + 5) / 10;
  DebugSerial.print(hundredths / 100);
  DebugSerial.print(".");
  if (hundredths % 100 < 10)
    DebugSerial.print("0");
  DebugSerial.print(hundredths % 100);
}
#endif

void sendSerialTelemetry()
{
#if ENABLE_SERIAL_DEBUG
  DebugSerial.print("TM1637:[");
  DebugSerial.print(currentDisplayStr);
  DebugSerial.print("] | MODE:");
  DebugSerial.print(currentMode);
  DebugSerial.print(" | ST:");
  DebugSerial.print(currentState);
  DebugSerial.print(" | FLT:");
  DebugSerial.print(currentFault);
  DebugSerial.print(" | V:");
  printOneDecimal(sysVolt);
  DebugSerial.print(" | A:");
  printAmps(sysAmp_mA);
  DebugSerial.print(" | T1:");
  printOneDecimal(sysTemp1);
  DebugSerial.print(" | T2:");
  printOneDecimal(sysTemp2);
  DebugSerial.println();
#endif
}

void handleIndicators()
{
  static unsigned long lastLedTimer = 0;
  static uint8_t blinkCount = 0;
  static bool isBlinking = false;
  static bool ledState = false;

  static unsigned long lastBuzzerInterval = millis() - 1800000UL;
  static unsigned long nextBeepTime = 0;
  static uint8_t beepCount = 0;
  static bool isBeeping = false;

  unsigned long currentMillis = millis();

  if (currentState == STATE_RUNNING)
  {
    digitalWrite(PIN_LED_STATUS, HIGH);
  }
  else if (currentState == STATE_STARTUP_DELAY)
  {
    if (currentMillis - lastLedTimer >= 500)
    {
      lastLedTimer = currentMillis;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
    }
  }
  else if (currentState == STATE_FAULT || currentState == STATE_LOCKOUT || currentState == STATE_ORP_REST)
  {
    uint8_t maxBlinks = currentFault;
    if (currentState == STATE_ORP_REST)
      maxBlinks = 10;
    if (currentState == STATE_LOCKOUT)
      maxBlinks = (currentFault == FAULT_LRA) ? 1 : 5;
    if (maxBlinks == 0)
      maxBlinks = 1;

    if (!isBlinking)
    {
      if (currentMillis - lastLedTimer >= 1500)
      {
        isBlinking = true;
        blinkCount = 0;
        lastLedTimer = currentMillis;
      }
      digitalWrite(PIN_LED_STATUS, LOW);
    }
    else
    {
      if (currentMillis - lastLedTimer >= (ledState ? 200 : 500))
      {
        lastLedTimer = currentMillis;
        ledState = !ledState;
        digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
        if (!ledState)
        {
          blinkCount++;
          if (blinkCount >= maxBlinks)
            isBlinking = false;
        }
      }
    }
  }

  if (currentState == STATE_FAULT || currentState == STATE_LOCKOUT)
  {
    unsigned long interval = (currentState == STATE_LOCKOUT) ? 900000UL : 1800000UL;
    if (!isBeeping && (currentMillis - lastBuzzerInterval >= interval))
    {
      isBeeping = true;
      beepCount = 0;
      nextBeepTime = currentMillis;
    }
    if (isBeeping && (long)(currentMillis - nextBeepTime) >= 0)
    {
      beepMs(300);
      beepCount++;
      nextBeepTime = millis() + 300;
      if (beepCount >= 5)
      {
        isBeeping = false;
        lastBuzzerInterval = millis();
      }
    }
  }
  else
  {
    digitalWrite(PIN_BUZZER, LOW);
    isBeeping = false;
    lastBuzzerInterval = currentMillis - 1800000UL;
  }
}

// ==========================================
// LIGHTWEIGHT GRBL-STYLE SERIAL MENU CLI
// ==========================================
#if ENABLE_SERIAL_DEBUG
static const char *const paramNames[20] = {
    "Delay(m)[2..10]",
    "OVP(V)[230..260]",
    "UVP(V)[150..200]",
    "V-Dly(s)[3..10]",
    "V-Hys(V)[10..40]",
    "LRA(mA)[800..5000]",
    "LRA-T(s)[1..5]",
    "LRA-R[2..5]",
    "OCP(mA)[300..4500]",
    "OCP-D(s)[2..10]",
    "UCP(mA)[0,200..1000]",
    "Min-A(mA)[0,150..300]",
    "NTC1[0=lock]",
    "OTP(C)[70..90]",
    "OTP-H(C)[5..20]",
    "NTC2[0=lock]",
    "Flw-D(m)[2..10]",
    "Flw-R[2..5]",
    "ORP-T(h)[2..12]",
    "ORP-R(m)[20..60]"};

static bool cmdEq(const char *a, const char *b)
{
  while (*a && *b)
  {
    char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
    if (ca != cb)
      return false;
    a++;
    b++;
  }
  return (*a == '\0' && *b == '\0');
}

static int32_t parseUint(const char *s, bool *ok)
{
  if (*s < '0' || *s > '9')
  {
    *ok = false;
    return 0;
  }
  int32_t v = 0;
  while (*s >= '0' && *s <= '9')
  {
    v = (v * 10) + (*s - '0');
    s++;
  }
  *ok = (*s == '\0');
  return v;
}

static void enterSerialMenu()
{
  if (!serialMenuActive)
  {
    serialMenuActive = true;
    serialMenuStream = false;
    digitalWrite(PIN_RELAY, LOW);
    showSerialMenuDisplay();
    DBG_PRINTLN("\n=== MENU MODE (TX PAUSED) ===");
    DBG_PRINTLN("Cmd: -h (help), $$ (list), $x=val, run");
  }
}

static void exitSerialMenu()
{
  serialMenuActive = false;
  serialMenuStream = false;
  currentState = STATE_STARTUP_DELAY;
  stateTimer = millis();
  DBG_PRINTLN("\n[RUN] Resuming compressed TX...\n");
}

static void printSerialHelp()
{
  DBG_PRINTLN("\n--- HELP & EXAMPLES ---");
  DBG_PRINTLN("menu        Pause TX & enter menu");
  DBG_PRINTLN("-h | help   Show this help");
  DBG_PRINTLN("$$          View all settings ($1..$22)");
  DBG_PRINTLN("$<id>=<val> Edit & save parameter");
  DBG_PRINTLN("read        Read 1 sensor snapshot");
  DBG_PRINTLN("stream      Toggle live TX in menu");
  DBG_PRINTLN("defaults    Reset factory defaults");
  DBG_PRINTLN("run | exit  Exit menu & resume TX");
  DBG_PRINTLN("reboot      Restart controller");
  DBG_PRINTLN("\nExamples:");
  DBG_PRINTLN("  $1=5      -> Startup delay = 5 min");
  DBG_PRINTLN("  $2=250    -> OVP limit = 250 V");
  DBG_PRINTLN("  $6=1500   -> LRA limit = 1500 mA");
  DBG_PRINTLN("  $11=0     -> UCP Off");
  DBG_PRINTLN("  $C1=3420  -> Voltage cal ($21)");
  DBG_PRINTLN("  $C2=1880  -> Current cal ($22)");
  DBG_PRINTLN("-----------------------\n");
}

static void printSerialSettings()
{
  DBG_PRINTLN("\n--- SETTINGS (GRBL) ---");
  for (uint8_t i = 0; i < 20; i++)
  {
    DBG_PRINT('$');
    DBG_PRINT(i + 1);
    if (i + 1 < 10)
      DBG_PRINT(' ');
    DBG_PRINT(" = ");
    int32_t v = getParamVal(i);
    DBG_PRINT(v);
    for (int32_t t = (v == 0 ? 1 : v); t < 1000; t *= 10)
      DBG_PRINT(' ');
    DBG_PRINT(" | ");
    DBG_PRINTLN(paramNames[i]);
  }
  DBG_PRINT("$21 = ");
  DBG_PRINT(params.voltageMultiplier_x1000);
  DBG_PRINTLN(" | C-1 Volt Cal");
  DBG_PRINT("$22 = ");
  DBG_PRINT(params.currentMultiplier_x1000);
  DBG_PRINTLN(" | C-2 Curr Cal");
  DBG_PRINTLN("-----------------------\n");
}

static void handleGrblAssign(const char *cmd)
{
  const char *eq = cmd + 1;
  while (*eq && *eq != '=')
    eq++;
  if (*eq != '=' || eq == cmd + 1 || *(eq + 1) == '\0')
  {
    DBG_PRINTLN("[ERR] Use $id=val (ex: $2=250)");
    return;
  }

  bool okVal = false;
  int32_t val = parseUint(eq + 1, &okVal);
  if (!okVal)
  {
    DBG_PRINTLN("[ERR] Value must be a number");
    return;
  }

  char idBuf[8];
  uint8_t idLen = (uint8_t)(eq - (cmd + 1));
  if (idLen >= sizeof(idBuf))
  {
    DBG_PRINTLN("[ERR] Bad ID");
    return;
  }
  for (uint8_t i = 0; i < idLen; i++)
    idBuf[i] = cmd[1 + i];
  idBuf[idLen] = '\0';

  int32_t id = 0;
  if ((idBuf[0] == 'c' || idBuf[0] == 'C') && idBuf[2] == '\0')
  {
    if (idBuf[1] == '1')
      id = 21;
    else if (idBuf[1] == '2')
      id = 22;
  }
  else
  {
    bool okId = false;
    id = parseUint(idBuf, &okId);
    if (!okId)
      id = 0;
  }

  if (id == 21 || id == 22)
  {
    if (val < 1 || val > 5000)
    {
      DBG_PRINTLN("[ERR] Range: 1..5000");
      return;
    }
    if (id == 21)
      params.voltageMultiplier_x1000 = (uint16_t)val;
    else
      params.currentMultiplier_x1000 = (uint16_t)val;
    saveParams();
    DBG_PRINT("[OK] $");
    DBG_PRINT(id);
    DBG_PRINT('=');
    DBG_PRINTLN(val);
    return;
  }

  if (id < 1 || id > 20)
  {
    DBG_PRINTLN("[ERR] Valid: $1..$22 or $C1/$C2");
    return;
  }

  uint8_t idx = (uint8_t)(id - 1);
  const ParamDef &d = menuTable[idx];
  if (d.size == 0)
  {
    DBG_PRINTLN("[ERR] Locked in hardware");
    return;
  }

  bool valid = (d.zeroOff && val == 0) ||
               (val >= d.min_ui && val <= d.max_ui && (val % d.scale == 0));
  if (!valid)
  {
    DBG_PRINT("[ERR] Range: ");
    if (d.zeroOff)
      DBG_PRINT("0 or ");
    DBG_PRINT(d.min_ui);
    DBG_PRINT("..");
    DBG_PRINTLN(d.max_ui);
    return;
  }

  setParamVal(idx, val);
  saveParams();
  DBG_PRINT("[OK] $");
  DBG_PRINT(id);
  DBG_PRINT('=');
  DBG_PRINTLN(val);
}

static void executeSerialCmd(char *cmd)
{
  while (*cmd == ' ' || *cmd == '\t')
    cmd++;
  uint8_t len = 0;
  while (cmd[len])
    len++;
  while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t'))
    cmd[--len] = '\0';
  if (len == 0)
    return;

  if (cmdEq(cmd, "menu") || cmdEq(cmd, "m"))
  {
    enterSerialMenu();
  }
  else if (cmdEq(cmd, "-h") || cmdEq(cmd, "help") || cmdEq(cmd, "?"))
  {
    enterSerialMenu();
    printSerialHelp();
  }
  else if (cmdEq(cmd, "$$"))
  {
    enterSerialMenu();
    printSerialSettings();
  }
  else if (cmdEq(cmd, "read") || cmdEq(cmd, "status"))
  {
    enterSerialMenu();
    DBG_PRINT("\nV:");
    DBG_PRINT(readVoltage());
    DBG_PRINT("V | A:");
    DBG_PRINT(readCurrent());
    DBG_PRINTLN("mA\n");
  }
  else if (cmdEq(cmd, "stream"))
  {
    enterSerialMenu();
    serialMenuStream = !serialMenuStream;
    DBG_PRINT("[MENU] Stream: ");
    DBG_PRINTLN(serialMenuStream ? "ON" : "PAUSED");
  }
  else if (cmdEq(cmd, "defaults") || cmdEq(cmd, "$RST=*"))
  {
    enterSerialMenu();
    resetToDefaultParams();
    DBG_PRINTLN("[OK] Defaults restored.");
    printSerialSettings();
  }
  else if (cmdEq(cmd, "run") || cmdEq(cmd, "exit"))
  {
    if (serialMenuActive)
      exitSerialMenu();
  }
  else if (cmdEq(cmd, "reboot"))
  {
    restartSystem();
  }
  else if (cmd[0] == '$')
  {
    enterSerialMenu();
    handleGrblAssign(cmd);
  }
  else
  {
    enterSerialMenu();
    DBG_PRINTLN("[ERR] Unknown cmd (type -h)");
  }
}

void pollSerialCli()
{
  while (DebugSerial.available() > 0)
  {
    char c = (char)DebugSerial.read();
    if (c == '\r' || c == '\n')
    {
      if (serialCmdIdx > 0)
      {
        serialCmdBuf[serialCmdIdx] = '\0';
        executeSerialCmd(serialCmdBuf);
        serialCmdIdx = 0;
      }
    }
    else if (c == '\b' || c == 127)
    {
      if (serialCmdIdx > 0)
        serialCmdIdx--;
    }
    else if (serialCmdIdx < sizeof(serialCmdBuf) - 1)
    {
      serialCmdBuf[serialCmdIdx++] = c;
    }
  }
}
#else
void pollSerialCli() {}
#endif

// ==========================================
// SETUP & LOOP
// ==========================================
void setup()
{
  DBG_BEGIN();
  initSensors();

  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_CALIBRATION, INPUT_PULLUP);
  delay(50);

  initUI();
  loadParams();

  if (digitalRead(PIN_CALIBRATION) == LOW)
  {
    currentMode = MODE_CALIBRATION;
    digitalWrite(PIN_RELAY, HIGH);
    DBG_PRINTLN(">>> MODE: KALIBRASI (RELAY ON) <<<");
  }
  else
  {
    int buttonAdc = analogRead(PIN_BUTTONS);
    if (buttonAdc >= 240 && buttonAdc <= 420)
    {
      currentMode = MODE_SETTING;
      digitalWrite(PIN_RELAY, LOW);
      DBG_PRINTLN(">>> MODE: SETTING <<<");
    }
    else
    {
      currentMode = MODE_NORMAL_PROTECTION;
      digitalWrite(PIN_RELAY, LOW);
      stateTimer = millis();
      DBG_PRINTLN(">>> PROTEKSI AKTIF (ketik 'menu' / '-h') <<<");
    }
    DBG_ENABLE_RX();
  }

  initWatchdog();
}

void loop()
{
  kickWatchdog();
  pollSerialCli();

  // In Serial Menu: pause sensor polling & compressed TX by default
  if (serialMenuActive)
  {
    digitalWrite(PIN_RELAY, LOW);
    if (serialMenuStream && (millis() - serialTelemetryTimer >= 1000))
    {
      serialTelemetryTimer = millis();
      sysVolt = readVoltage();
      sysAmp_mA = readCurrent();
      sendSerialTelemetry();
    }
    return;
  }

  bool rxInterrupted = false;
  if (currentMode != MODE_SETTING)
  {
    sysVolt = readVoltage();
    if (serialRxIncoming())
    {
      rxInterrupted = true;
      pollSerialCli();
    }
    else
    {
      sysAmp_mA = readCurrent();
      if (serialRxIncoming())
      {
        rxInterrupted = true;
        pollSerialCli();
      }
    }
#if NTC1_ENABLED
    sysTemp1 = readTemp1();
#endif
#if NTC2_ENABLED
    sysTemp2 = readTemp2();
#endif
  }

  if (serialMenuActive || rxInterrupted)
    return;

  if (millis() - serialTelemetryTimer >= 1000)
  {
    serialTelemetryTimer = millis();
    sendSerialTelemetry();
  }

  if (currentMode == MODE_CALIBRATION)
  {
    handleCalibrationMode();
    return;
  }
  if (currentMode == MODE_SETTING)
  {
    handleSettingMode();
    return;
  }

  // 1. Instant protection (surge / blackout)
  if (sysVolt >= PARAM_POWER_SURGE_LIMIT)
    triggerFault(FAULT_SURGE);
  if (sysVolt <= PARAM_BLACKOUT_LIMIT)
    triggerFault(FAULT_BLACKOUT);

  // 2. State machine
  switch (currentState)
  {
  case STATE_STARTUP_DELAY:
  {
    digitalWrite(PIN_RELAY, LOW);
#if NTC1_ENABLED
    bool compressorHot = (sysTemp1 > (params.otpLimit - params.otpHysteresis));
#else
    bool compressorHot = false;
#endif
    if (!isVoltageSafe(sysVolt) || compressorHot)
    {
      stateTimer = millis();
    }
    else if (millis() - stateTimer >= (params.timeDelay_mins * 60000UL))
    {
      currentFault = FAULT_NONE;
#if NTC2_ENABLED
      initialTempNTC2 = sysTemp2;
      isFlowChecked = false;
#endif
      runningTimer = millis();
      orpTimer = millis();
      currentState = STATE_RUNNING;
      digitalWrite(PIN_RELAY, HIGH);
      DBG_PRINTLN("[SYS] RELAY ON");
    }
    break;
  }

  case STATE_RUNNING:
  {
    if (sysVolt > OVP_LIMIT)
      checkFaultDelay(FAULT_OVP, params.ovpUvpDelay);
    else if (sysVolt < params.uvpLimit)
      checkFaultDelay(FAULT_UVP, params.ovpUvpDelay);
    else
      faultConditionTimer = millis();

#if NTC1_ENABLED
    if (sysTemp1 >= params.otpLimit)
    {
      triggerFault(FAULT_OTP);
    }
#endif

    unsigned long runDuration = millis() - runningTimer;
    if (runDuration <= (params.lraTime * 1000UL))
    {
      if (sysAmp_mA >= params.lraLimit_mA)
      {
        lraRetryCount++;
        if (lraRetryCount >= params.lraRetryLimit)
        {
          currentFault = FAULT_LRA;
          currentState = STATE_LOCKOUT;
          digitalWrite(PIN_RELAY, LOW);
          DBG_PRINTLN("[LOCKOUT] LRA");
        }
        else
        {
          triggerFault(FAULT_LRA);
        }
      }
    }
    else
    {
      lraRetryCount = 0;
      if (sysAmp_mA >= params.ocpLimit_mA)
      {
        checkFaultDelay(FAULT_OCP, params.ocpDelay);
      }
      if (params.ucpLimit_x10 > 0 && sysAmp_mA <= (params.ucpLimit_x10 * 10))
      {
        checkFaultDelay(FAULT_UCP, 60);
      }
    }

#if NTC2_ENABLED
    if (!isFlowChecked && runDuration >= (params.refFlowDelay_mins * 60000UL))
    {
      isFlowChecked = true;
      if (sysTemp2 < (initialTempNTC2 + 5))
      {
        flowRetryCount++;
        if (flowRetryCount >= params.flowFaultRetryLimit)
        {
          currentFault = FAULT_REF_FLOW;
          currentState = STATE_LOCKOUT;
          digitalWrite(PIN_RELAY, LOW);
          DBG_PRINTLN("[LOCKOUT] FLOW");
        }
        else
        {
          triggerFault(FAULT_REF_FLOW);
        }
      }
      else
      {
        flowRetryCount = 0;
      }
    }
#endif

    if (sysAmp_mA < (params.minCurrentThreshold_x10 * 10))
    {
      orpTimer = millis();
    }
    else if (millis() - orpTimer >= (params.orpTimeLimit_hours * 3600000UL))
    {
      currentState = STATE_ORP_REST;
      stateTimer = millis();
      digitalWrite(PIN_RELAY, LOW);
      DBG_PRINTLN("[SYS] ORP REST");
    }
    break;
  }

  case STATE_FAULT:
  {
    digitalWrite(PIN_RELAY, LOW);
    bool readyToRecover = false;
    if (currentFault == FAULT_OVP && sysVolt <= (OVP_LIMIT - params.voltageHysteresis))
      readyToRecover = true;
    else if (currentFault == FAULT_UVP && sysVolt >= (params.uvpLimit + params.voltageHysteresis))
      readyToRecover = true;
    else if (currentFault == FAULT_BLACKOUT && sysVolt >= (params.uvpLimit + params.voltageHysteresis))
      readyToRecover = true;
    else if (currentFault == FAULT_SURGE && sysVolt <= (OVP_LIMIT - params.voltageHysteresis))
      readyToRecover = true;
#if NTC1_ENABLED
    else if (currentFault == FAULT_OTP && sysTemp1 <= (params.otpLimit - params.otpHysteresis))
      readyToRecover = true;
#endif
    else if (currentFault != FAULT_OVP && currentFault != FAULT_UVP && currentFault != FAULT_BLACKOUT && currentFault != FAULT_SURGE && currentFault != FAULT_OTP)
      readyToRecover = true;

    if (readyToRecover)
    {
      currentState = STATE_STARTUP_DELAY;
      currentFault = FAULT_NONE;
      stateTimer = millis();
    }
    break;
  }

  case STATE_ORP_REST:
  {
    digitalWrite(PIN_RELAY, LOW);
#if NTC1_ENABLED
    bool tempCool = (sysTemp1 <= 60);
#else
    bool tempCool = true;
#endif
    if ((millis() - stateTimer >= (params.orpRestDelay_mins * 60000UL)) && tempCool)
    {
      currentState = STATE_STARTUP_DELAY;
      stateTimer = millis();
    }
    break;
  }

  case STATE_LOCKOUT:
    digitalWrite(PIN_RELAY, LOW);
    break;
  }

  updateDisplay(sysVolt, sysAmp_mA);
  handleIndicators();
}