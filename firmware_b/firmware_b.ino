#include <ch32v00x_iwdg.h>
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"
#include "UI.h"

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

OperatingMode currentMode = MODE_NORMAL_PROTECTION;
SystemState currentState = STATE_STARTUP_DELAY;
FaultType currentFault = FAULT_NONE;

uint16_t sysVolt = 0;
uint16_t sysAmp_mA = 0;

unsigned long stateTimer = 0;
unsigned long runningTimer = 0;
unsigned long orpTimer = 0;
unsigned long faultConditionTimer = 0;
unsigned long serialTelemetryTimer = 0;

uint8_t lraRetryCount = 0;

void restartSystem();
void beepMs(uint16_t ms);
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

  DBG_PRINTLN("[SYS] PARAM LOADED");

  if (digitalRead(PIN_CALIBRATION) == LOW) {
    currentMode = MODE_CALIBRATION;
    digitalWrite(PIN_RELAY, HIGH);
    DBG_PRINTLN(">>> MODE: CALIBRATION <<<");
  } else {
    int buttonAdc = analogRead(PIN_BUTTONS);
    if (buttonAdc >= 240 && buttonAdc <= 420) {
      currentMode = MODE_SETTING;
      digitalWrite(PIN_RELAY, LOW);
      DBG_PRINTLN(">>> MODE: SETTING <<<");
    } else {
      currentMode = MODE_NORMAL_PROTECTION;
      digitalWrite(PIN_RELAY, LOW);
      stateTimer = millis();
      DBG_PRINTLN(">>> MODE: PROTECT <<<");
    }
  }

  initWatchdog();
}

void loop() {
  IWDG_ReloadCounter();

  if (currentMode != MODE_SETTING) {
    sysVolt   = readVoltage();
    sysAmp_mA = readCurrent();
  }

  handleIndicators(); 

  if (millis() - serialTelemetryTimer >= 1000) {
    serialTelemetryTimer = millis();
    sendSerialTelemetry();
  }

  if (currentMode == MODE_CALIBRATION) { handleCalibrationMode(); return; }
  if (currentMode == MODE_SETTING)     { handleSettingMode();     return; }

  if (sysVolt >= PARAM_POWER_SURGE_LIMIT) triggerFault(FAULT_SURGE);
  if (sysVolt <= PARAM_BLACKOUT_LIMIT)    triggerFault(FAULT_BLACKOUT);

  switch (currentState) {

    case STATE_STARTUP_DELAY:
      digitalWrite(PIN_RELAY, LOW);
      if (!isVoltageSafe(sysVolt)) {
        stateTimer = millis();
      } else if (millis() - stateTimer >= (params.timeDelay_mins * 60000UL)) {
        currentFault = FAULT_NONE;
        runningTimer = millis();
        orpTimer     = millis();
        currentState = STATE_RUNNING;
        digitalWrite(PIN_RELAY, HIGH);
      }
      break;

    case STATE_RUNNING: {
      if (sysVolt > params.ovpLimit)      checkFaultDelay(FAULT_OVP, params.ovpUvpDelay);
      else if (sysVolt < params.uvpLimit) checkFaultDelay(FAULT_UVP, params.ovpUvpDelay);
      else                                faultConditionTimer = millis();

      unsigned long runDuration = millis() - runningTimer;
      if (runDuration <= (params.lraTime * 1000UL)) {
        if (sysAmp_mA >= params.lraLimit_mA) {
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
        if (sysAmp_mA >= params.ocpLimit_mA) {
          checkFaultDelay(FAULT_OCP, params.ocpDelay);
        }
        if (params.ucpLimit_x10 > 0 && sysAmp_mA <= (params.ucpLimit_x10 * 10)) {
          checkFaultDelay(FAULT_UCP, 60);
        }
      }

      if (sysAmp_mA < (params.minCurrentThreshold_x10 * 10)) {
        orpTimer = millis();
      } else if (millis() - orpTimer >= (params.orpTimeLimit_hours * 3600000UL)) {
        currentState = STATE_ORP_REST;
        stateTimer   = millis();
        digitalWrite(PIN_RELAY, LOW);
      }
      break;
    }

    case STATE_FAULT: {
      digitalWrite(PIN_RELAY, LOW);
      bool readyToRecover = false;
      if      (currentFault == FAULT_OVP && sysVolt <= (params.ovpLimit - params.voltageHysteresis)) readyToRecover = true;
      else if (currentFault == FAULT_UVP && sysVolt >= (params.uvpLimit + params.voltageHysteresis)) readyToRecover = true;
      else if (currentFault != FAULT_OVP && currentFault != FAULT_UVP) readyToRecover = true;

      if (readyToRecover) {
        currentState = STATE_STARTUP_DELAY;
        stateTimer   = millis();
      }
      break;
    }

    case STATE_ORP_REST:
      digitalWrite(PIN_RELAY, LOW);
      if (millis() - stateTimer >= (params.orpRestDelay_mins * 60000UL)) {
        currentState = STATE_STARTUP_DELAY;
        stateTimer   = millis();
      }
      break;

    case STATE_LOCKOUT:
      digitalWrite(PIN_RELAY, LOW);
      break;
  }

  updateDisplay(sysVolt, sysAmp_mA);
}

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

bool isVoltageSafe(uint16_t v) {
  return (v > params.uvpLimit && v < params.ovpLimit);
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
  Serial.print("V=");  Serial.print(sysVolt);
  Serial.print(" A="); Serial.print(sysAmp_mA); // mA
  Serial.print(" ST="); Serial.println(currentState);
#endif
}

void handleIndicators() {
  static unsigned long lastLedTimer = 0;
  static uint8_t blinkCount = 0;
  static bool isBlinking = false;
  static bool ledState = false;

  static unsigned long lastBuzzerInterval = millis() - 1800000UL;
  static unsigned long nextBeepTime = 0;
  static uint8_t beepCount = 0;
  static bool isBeeping = false;

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