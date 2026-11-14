#include <ch32v00x_iwdg.h>
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"
#include "UI.h"

// Set to 0 if the flash gets too full (removes all Serial output)
#define ENABLE_SERIAL_DEBUG 1

#if ENABLE_SERIAL_DEBUG
#define DBG_BEGIN() Serial.begin(9600)
#define DBG_PRINT(x) Serial.print(x)
#define DBG_PRINTLN(x) Serial.println(x)
#else
#define DBG_BEGIN()
#define DBG_PRINT(x)    do {} while (0)
#define DBG_PRINTLN(x)  do {} while (0)
#endif

// ==========================================
// GLOBAL SYSTEM VARIABLES
// ==========================================
OperatingMode currentMode  = MODE_NORMAL_PROTECTION;
SystemState   currentState = STATE_STARTUP_DELAY;
FaultType     currentFault = FAULT_NONE;

uint16_t sysVolt   = 0;
uint16_t sysAmp_mA = 0;
int16_t  sysTemp1  = 0;
int16_t  sysTemp2  = 0;

unsigned long stateTimer = 0;
unsigned long runningTimer = 0;
unsigned long orpTimer = 0;
unsigned long faultConditionTimer = 0;
unsigned long serialTelemetryTimer = 0;

uint8_t lraRetryCount  = 0;
uint8_t flowRetryCount = 0;

int16_t initialTempNTC2 = 0;
bool    isFlowChecked   = false;

// ==========================================
// FORWARD DECLARATIONS
// ==========================================
void restartSystem();
void beepMs(uint16_t ms);
void triggerButtonChirp();
void triggerFault(FaultType fault);
void checkFaultDelay(FaultType fault, uint8_t delaySeconds);
bool isVoltageSafe(uint16_t v);
void sendSerialTelemetry();
void handleIndicators();

void triggerButtonChirp() { beepMs(40); }

void beepMs(uint16_t ms) {
#if BUZZER_PASSIVE
  const uint16_t halfPeriodUs = 500000UL / BUZZER_FREQ_HZ;
  unsigned long end = millis() + ms;
  while ((long)(millis() - end) < 0) {
    digitalWrite(PIN_BUZZER, HIGH);
    delayMicroseconds(halfPeriodUs);
    digitalWrite(PIN_BUZZER, LOW);
    delayMicroseconds(halfPeriodUs);
    IWDG_ReloadCounter();
  }
#else
  digitalWrite(PIN_BUZZER, HIGH);
  unsigned long end = millis() + ms;
  while ((long)(millis() - end) < 0) {
    IWDG_ReloadCounter();
    delay(5);
  }
#endif
  digitalWrite(PIN_BUZZER, LOW);
}

static void initWatchdog() {
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_64);
  IWDG_SetReload(3999);
  IWDG_ReloadCounter();
  IWDG_Enable();
}

// ==========================================
// SETUP
// ==========================================
void setup() {
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

  DBG_PRINTLN("[SYSTEM] Parameter Loaded from EEPROM");

  if (digitalRead(PIN_CALIBRATION) == LOW) {
    currentMode = MODE_CALIBRATION;
    digitalWrite(PIN_RELAY, HIGH);   // relay ON so V / A can be calibrated live
    DBG_PRINTLN(">>> MODE: CALIBRATION (RELAY ON) <<<");
  } else {
    int buttonAdc = analogRead(PIN_BUTTONS);
    if (buttonAdc >= 240 && buttonAdc <= 420) {   // Menu button held at boot
      currentMode = MODE_SETTING;
      digitalWrite(PIN_RELAY, LOW);
      DBG_PRINTLN(">>> MODE: SETTING <<<");
    } else {
      currentMode = MODE_NORMAL_PROTECTION;
      digitalWrite(PIN_RELAY, LOW);
      stateTimer = millis();
      DBG_PRINTLN(">>> MODE: PROTECTION <<<");
    }
  }

  initWatchdog();
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  IWDG_ReloadCounter();

  if (currentMode != MODE_SETTING) {
    sysVolt   = readVoltage();
    sysAmp_mA = readCurrent();
    sysTemp1  = readTemp1();
    sysTemp2  = readTemp2();
  }

  if (millis() - serialTelemetryTimer >= 1000) {
    serialTelemetryTimer = millis();
    sendSerialTelemetry();
  }

  if (currentMode == MODE_CALIBRATION) { handleCalibrationMode(); return; }
  if (currentMode == MODE_SETTING)     { handleSettingMode();     return; }

  // 1. Instant protection (surge / blackout)
  if (sysVolt >= PARAM_POWER_SURGE_LIMIT) triggerFault(FAULT_SURGE);
  if (sysVolt <= PARAM_BLACKOUT_LIMIT)    triggerFault(FAULT_BLACKOUT);

  // 2. State machine
  switch (currentState) {

    case STATE_STARTUP_DELAY: {
      digitalWrite(PIN_RELAY, LOW);

      // Hold the timer while voltage is unsafe or the compressor is still hot (NTC1)
      bool compressorHot = NTC1_ENABLED && (sysTemp1 > (params.otpLimit - params.otpHysteresis));
      if (!isVoltageSafe(sysVolt) || compressorHot) {
        stateTimer = millis();
      } else if (millis() - stateTimer >= (params.timeDelay_mins * 60000UL)) {
        currentFault    = FAULT_NONE;
        initialTempNTC2 = sysTemp2;
        isFlowChecked   = false;
        runningTimer    = millis();
        orpTimer        = millis();
        currentState    = STATE_RUNNING;
        digitalWrite(PIN_RELAY, HIGH);
        DBG_PRINTLN("[SYSTEM] STARTING COMPRESSOR -> RELAY ON");
      }
      break;
    }

    case STATE_RUNNING: {
      // A. OVP / UVP with delay
      if (sysVolt > OVP_LIMIT)            checkFaultDelay(FAULT_OVP, params.ovpUvpDelay);
      else if (sysVolt < params.uvpLimit) checkFaultDelay(FAULT_UVP, params.ovpUvpDelay);
      else                                faultConditionTimer = millis();

      // B. Compressor over-temperature (NTC1)
      if (NTC1_ENABLED && sysTemp1 >= params.otpLimit) {
        triggerFault(FAULT_OTP);
      }

      // C. Current: LRA during inrush, OCP / UCP afterwards
      unsigned long runDuration = millis() - runningTimer;
      if (runDuration <= (params.lraTime * 1000UL)) {
        if (sysAmp_mA >= params.lraLimit_mA) {
          lraRetryCount++;
          DBG_PRINT("[WARNING] LRA trip, retry: ");
          DBG_PRINTLN(lraRetryCount);
          if (lraRetryCount >= params.lraRetryLimit) {
            currentFault = FAULT_LRA;          // so the display shows Err5 + LOC1
            currentState = STATE_LOCKOUT;
            digitalWrite(PIN_RELAY, LOW);
            DBG_PRINTLN("[FATAL] LRA retry exceeded -> LOCKOUT");
          } else {
            triggerFault(FAULT_LRA);
          }
        }
      } else {
        lraRetryCount = 0;
        if (sysAmp_mA >= params.ocpLimit_mA) {
          checkFaultDelay(FAULT_OCP, params.ocpDelay);
        }
        if (params.ucpLimit_x10 > 0 && sysAmp_mA <= (params.ucpLimit_x10 * 10)) {
          checkFaultDelay(FAULT_UCP, 60);
        }
      }

      // D. Refrigerant flow (NTC2)
      if (NTC2_ENABLED && !isFlowChecked && runDuration >= (params.refFlowDelay_mins * 60000UL)) {
        isFlowChecked = true;
        if (sysTemp2 < (initialTempNTC2 + 5)) {      // minimum rise of 5 C, same as LGT default
          flowRetryCount++;
          DBG_PRINT("[WARNING] Ref flow error, retry: ");
          DBG_PRINTLN(flowRetryCount);
          if (flowRetryCount >= params.flowFaultRetryLimit) {
            currentFault = FAULT_REF_FLOW;     // Err9 + LOC2
            currentState = STATE_LOCKOUT;
            digitalWrite(PIN_RELAY, LOW);
            DBG_PRINTLN("[FATAL] Ref flow retry exceeded -> LOCKOUT");
          } else {
            triggerFault(FAULT_REF_FLOW);
          }
        } else {
          flowRetryCount = 0;
        }
      }

      // E. ORP (over running)
      if (sysAmp_mA < (params.minCurrentThreshold_x10 * 10)) {
        orpTimer = millis();                          // thermostat off
      } else if (millis() - orpTimer >= (params.orpTimeLimit_hours * 3600000UL)) {
        currentState = STATE_ORP_REST;
        stateTimer   = millis();
        digitalWrite(PIN_RELAY, LOW);
        DBG_PRINTLN("[SYSTEM] ORP triggered -> rest");
      }
      break;
    }

    case STATE_FAULT: {
      digitalWrite(PIN_RELAY, LOW);
      bool readyToRecover = false;
      if      (currentFault == FAULT_OVP && sysVolt <= (OVP_LIMIT - params.voltageHysteresis)) readyToRecover = true;
      else if (currentFault == FAULT_UVP && sysVolt >= (params.uvpLimit + params.voltageHysteresis)) readyToRecover = true;
      else if (currentFault == FAULT_OTP && sysTemp1 <= (params.otpLimit - params.otpHysteresis)) readyToRecover = true;
      else if (currentFault != FAULT_OVP && currentFault != FAULT_UVP && currentFault != FAULT_OTP) readyToRecover = true;

      if (readyToRecover) {
        DBG_PRINTLN("[SYSTEM] Fault recovered -> startup delay");
        currentState = STATE_STARTUP_DELAY;
        stateTimer   = millis();
      }
      break;
    }

    case STATE_ORP_REST: {
      digitalWrite(PIN_RELAY, LOW);
      bool tempCool = (!NTC1_ENABLED || sysTemp1 <= 60);
      if ((millis() - stateTimer >= (params.orpRestDelay_mins * 60000UL)) && tempCool) {
        DBG_PRINTLN("[SYSTEM] ORP rest done -> startup delay");
        currentState = STATE_STARTUP_DELAY;
        stateTimer   = millis();
      }
      break;
    }

    case STATE_LOCKOUT:
      digitalWrite(PIN_RELAY, LOW);   // locked until power is cycled
      break;
  }

  // 3. Display and indicators (normal mode only, same as LGT)
  updateDisplay(sysVolt, sysAmp_mA);
  handleIndicators();
}

// ==========================================
// HELPERS
// ==========================================
void triggerFault(FaultType fault) {
  if (currentState == STATE_FAULT && currentFault == fault) return;
  currentFault = fault;
  currentState = STATE_FAULT;
  digitalWrite(PIN_RELAY, LOW);
  faultConditionTimer = millis();
  DBG_PRINT("[TRIP FAULT] Code: ");
  DBG_PRINTLN((int)fault);
}

void checkFaultDelay(FaultType fault, uint8_t delaySeconds) {
  if (millis() - faultConditionTimer >= (delaySeconds * 1000UL)) triggerFault(fault);
}

bool isVoltageSafe(uint16_t v) {
  return (v > params.uvpLimit && v < OVP_LIMIT);
}

void restartSystem() {
  delay(100);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetReload(0);
  IWDG_ReloadCounter();
  while (1) {}
}

void sendSerialTelemetry() {
#if ENABLE_SERIAL_DEBUG
  Serial.print("MODE:");  Serial.print(currentMode);
  Serial.print(" | ST:"); Serial.print(currentState);
  Serial.print(" | FLT:"); Serial.print(currentFault);
  Serial.print(" | V:");  Serial.print(sysVolt);
  Serial.print(" | mA:"); Serial.print(sysAmp_mA);
  Serial.print(" | T1:"); Serial.print(sysTemp1);
  Serial.print(" | T2:"); Serial.println(sysTemp2);
#endif
}

void handleIndicators() {
  static unsigned long lastLedTimer = 0;
  static uint8_t blinkCount = 0;
  static bool isBlinking = false;
  static bool ledState = false;

  static unsigned long lastBuzzerInterval = millis() - 1800000UL;  // first error beeps at once
  static unsigned long nextBeepTime = 0;
  static uint8_t beepCount = 0;
  static bool isBeeping = false;

  unsigned long currentMillis = millis();

  // ---- LED ----
  if (currentState == STATE_RUNNING) {
    digitalWrite(PIN_LED_STATUS, HIGH);
  } else if (currentState == STATE_STARTUP_DELAY) {
    if (currentMillis - lastLedTimer >= 500) {
      lastLedTimer = currentMillis;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState);
    }
  } else if (currentState == STATE_FAULT || currentState == STATE_LOCKOUT || currentState == STATE_ORP_REST) {
    // Blinks = error number. ORP rest = 10. Lockout: LOC1 = 1 blink, LOC2 = 5 blinks
    uint8_t maxBlinks = currentFault;
    if (currentState == STATE_ORP_REST) maxBlinks = 10;
    if (currentState == STATE_LOCKOUT)  maxBlinks = (currentFault == FAULT_LRA) ? 1 : 5;
    if (maxBlinks == 0) maxBlinks = 1;

    if (!isBlinking) {
      if (currentMillis - lastLedTimer >= 1500) {
        isBlinking = true;
        blinkCount = 0;
        lastLedTimer = currentMillis;
      }
      digitalWrite(PIN_LED_STATUS, LOW);
    } else {
      if (currentMillis - lastLedTimer >= (ledState ? 200 : 500)) {
        lastLedTimer = currentMillis;
        ledState = !ledState;
        digitalWrite(PIN_LED_STATUS, ledState);
        if (!ledState) {
          blinkCount++;
          if (blinkCount >= maxBlinks) isBlinking = false;
        }
      }
    }
  }

  // ---- BUZZER: 5 beeps, every 30 min (fault) / 15 min (lockout) ----
  if (currentState == STATE_FAULT || currentState == STATE_LOCKOUT) {
    unsigned long interval = (currentState == STATE_LOCKOUT) ? 900000UL : 1800000UL;
    if (!isBeeping && (currentMillis - lastBuzzerInterval >= interval)) {
      isBeeping = true;
      beepCount = 0;
      nextBeepTime = currentMillis;
    }
    if (isBeeping && (long)(currentMillis - nextBeepTime) >= 0) {
      beepMs(300);
      beepCount++;
      nextBeepTime = millis() + 300;
      if (beepCount >= 5) {
        isBeeping = false;
        lastBuzzerInterval = millis();
      }
    }
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    isBeeping = false;
    lastBuzzerInterval = currentMillis - 1800000UL;
  }
}
