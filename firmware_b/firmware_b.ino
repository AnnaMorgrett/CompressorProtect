#include <ch32v00x_iwdg.h>
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"
#include "UI.h"

#define ENABLE_SERIAL_DEBUG 0

#if ENABLE_SERIAL_DEBUG
#define DBG_BEGIN() Serial.begin(9600)
#define DBG_PRINT(x) Serial.print(x)
#define DBG_PRINTLN(x) Serial.println(x)
#else
#define DBG_BEGIN()
#define DBG_PRINT(x) \
  do { \
  } while (0)
#define DBG_PRINTLN(x) \
  do { \
  } while (0)
#endif

// ==========================================================
// GLOBAL SYSTEM VARIABLES
// ==========================================================
OperatingMode currentMode = MODE_NORMAL_PROTECTION;
SystemState currentState = STATE_STARTUP_DELAY;
FaultType currentFault = FAULT_NONE;

float sysVolt = 0.0f;
float sysAmp = 0.0f;

unsigned long stateTimer = 0;
unsigned long runningTimer = 0;
unsigned long orpTimer = 0;
unsigned long faultConditionTimer = 0;

uint8_t lraRetryCount = 0;

void restartSystem();

// ==========================================================
// WATCHDOG
// ==========================================================
static void initWatchdog() {
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_64);
  IWDG_SetReload(3999);
  IWDG_ReloadCounter();
  IWDG_Enable();
}

// ==========================================================
// SETUP
// ==========================================================
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

  if (digitalRead(PIN_CALIBRATION) == LOW) {
    currentMode = MODE_CALIBRATION;
    digitalWrite(PIN_RELAY, HIGH);
  } else {
    int buttonAdc = analogRead(PIN_BUTTONS);
    if (buttonAdc >= 240 && buttonAdc <= 420) {
      currentMode = MODE_SETTING;
      digitalWrite(PIN_RELAY, LOW);
    } else {
      currentMode = MODE_NORMAL_PROTECTION;
      digitalWrite(PIN_RELAY, LOW);
      stateTimer = millis();
    }
  }

  initWatchdog();
}

// ==========================================================
// LOOP
// ==========================================================
void loop() {
  IWDG_ReloadCounter();

  sysVolt = readVoltage();
  sysAmp = readCurrent();

  if (currentMode == MODE_CALIBRATION) {
    handleCalibrationMode();
    return;
  }
  if (currentMode == MODE_SETTING) {
    handleSettingMode();
    return;
  }

  if (sysVolt >= PARAM_POWER_SURGE_LIMIT) triggerFault(FAULT_SURGE);
  if (sysVolt <= PARAM_BLACKOUT_LIMIT) triggerFault(FAULT_BLACKOUT);

  switch (currentState) {

    case STATE_STARTUP_DELAY:
      digitalWrite(PIN_RELAY, LOW);
      if (!isVoltageSafe(sysVolt)) {
        stateTimer = millis();
      } else if (millis() - stateTimer >= (params.timeDelay * 1000UL)) {
        currentFault = FAULT_NONE;
        runningTimer = millis();
        orpTimer = millis();
        currentState = STATE_RUNNING;
        digitalWrite(PIN_RELAY, HIGH);
      }
      break;

    case STATE_RUNNING:
      {
        if (sysVolt > params.ovpLimit) checkFaultDelay(FAULT_OVP, params.ovpUvpDelay);
        else if (sysVolt < params.uvpLimit) checkFaultDelay(FAULT_UVP, params.ovpUvpDelay);
        else faultConditionTimer = millis();

        unsigned long runDuration = millis() - runningTimer;
        if (runDuration <= (params.lraTime * 1000UL)) {
          if (sysAmp >= (params.lraLimit_x100 * 0.01f)) {
            lraRetryCount++;
            if (lraRetryCount >= params.lraRetryLimit) {
              currentState = STATE_LOCKOUT;
              digitalWrite(PIN_RELAY, LOW);
            } else {
              triggerFault(FAULT_LRA);
            }
          }
        } else {
          lraRetryCount = 0;
          if (sysAmp >= (params.ocpLimit_x100 * 0.01f)) {
            checkFaultDelay(FAULT_OCP, params.ocpDelay);
          }
        }

        if (sysAmp < (params.minCurrentThreshold_x100 * 0.01f)) {
          orpTimer = millis();
        } else if (millis() - orpTimer >= (420UL * 60000UL)) {  // 420 min = 7 h, was P-12 (removed)
          currentState = STATE_ORP_REST;
          stateTimer = millis();
          digitalWrite(PIN_RELAY, LOW);
        }
        break;
      }

    case STATE_FAULT:
      {
        digitalWrite(PIN_RELAY, LOW);
        bool readyToRecover = false;
        if (currentFault == FAULT_OVP && sysVolt <= (params.ovpLimit - params.voltageHysteresis)) readyToRecover = true;
        else if (currentFault == FAULT_UVP && sysVolt >= (params.uvpLimit + params.voltageHysteresis)) readyToRecover = true;
        else if (currentFault != FAULT_OVP && currentFault != FAULT_UVP) readyToRecover = true;

        if (readyToRecover) {
          currentState = STATE_STARTUP_DELAY;
          stateTimer = millis();
        }
        break;
      }

    case STATE_ORP_REST:
      digitalWrite(PIN_RELAY, LOW);
      if (millis() - stateTimer >= (30UL * 60000UL)) {  // hardcoded 30 min — P-13 removed from menu
        currentState = STATE_STARTUP_DELAY;
        stateTimer = millis();
      }
      break;

    case STATE_LOCKOUT:
      digitalWrite(PIN_RELAY, LOW);
      break;
  }

  updateDisplay(sysVolt, sysAmp);
  handleIndicators();
}

// ==========================================================
// HELPERS
// ==========================================================
void triggerFault(FaultType fault) {
  if (currentState == STATE_FAULT && currentFault == fault) return;
  currentFault = fault;
  currentState = STATE_FAULT;
  digitalWrite(PIN_RELAY, LOW);
  faultConditionTimer = millis();
}

void checkFaultDelay(FaultType fault, uint8_t delaySeconds) {
  if (millis() - faultConditionTimer >= (delaySeconds * 1000UL)) triggerFault(fault);
}

bool isVoltageSafe(float v) {
  return (v > params.uvpLimit && v < params.ovpLimit);
}

void restartSystem() {
  delay(100);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetReload(0);
  IWDG_ReloadCounter();
  while (1) {}
}

// ==========================================================
// LED + BUZZER (unchanged)
// ==========================================================
void handleIndicators() {
  static unsigned long lastLedTimer = 0;
  static uint8_t blinkCount = 0;
  static bool isBlinking = false;
  static bool ledState = false;

  static unsigned long lastBuzzerInterval = millis() - 1800000UL;
  static unsigned long lastBuzzerToggle = 0;
  static uint8_t beepCount = 0;
  static bool isBeeping = false;
  static bool buzzerState = false;

  unsigned long currentMillis = millis();

  if (currentState == STATE_RUNNING) {
    digitalWrite(PIN_LED_STATUS, HIGH);
  } else if (currentState == STATE_STARTUP_DELAY) {
    if (currentMillis - lastLedTimer >= 500) {
      lastLedTimer = currentMillis;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState);
    }
  } else if (currentState == STATE_FAULT || currentState == STATE_LOCKOUT || currentState == STATE_ORP_REST) {
    uint8_t maxBlinks = (currentState == STATE_ORP_REST) ? 10 : currentFault;
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

  if (currentState == STATE_FAULT || currentState == STATE_LOCKOUT) {
    unsigned long interval = (currentState == STATE_LOCKOUT) ? 900000UL : 1800000UL;
    if (!isBeeping && (currentMillis - lastBuzzerInterval >= interval)) {
      isBeeping = true;
      beepCount = 0;
      lastBuzzerToggle = currentMillis;
      buzzerState = true;
      digitalWrite(PIN_BUZZER, HIGH);
    }
    if (isBeeping) {
      if (currentMillis - lastBuzzerToggle >= 300) {
        lastBuzzerToggle = currentMillis;
        buzzerState = !buzzerState;
        digitalWrite(PIN_BUZZER, buzzerState);
        if (!buzzerState) {
          beepCount++;
          if (beepCount >= 5) {
            isBeeping = false;
            lastBuzzerInterval = currentMillis;
          }
        }
      }
    }
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    isBeeping = false;
    lastBuzzerInterval = currentMillis - 1800000UL;
  }
}