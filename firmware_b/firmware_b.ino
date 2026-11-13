#include "Pins.h"
#include "Params.h"
#include "Sensors.h"  
#include "UI.h"       

OperatingMode currentMode = MODE_NORMAL_PROTECTION;
SystemState currentState = STATE_STARTUP_DELAY;
FaultType currentFault = FAULT_NONE;

int sysVolt = 0, sysAmp = 0, sysTemp1 = 0, sysTemp2 = 0;
unsigned long stateTimer = 0, runningTimer = 0, orpTimer = 0;
unsigned long faultConditionTimer = 0, serialTelemetryTimer = 0;
uint8_t lraRetryCount = 0, flowRetryCount = 0;
int initialTempNTC2 = 0;
bool isFlowChecked = false;

void restartSystem();

void setup() {
  Serial.begin(9600);
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
  Serial.println(F("SYS: OK")); // Di-compress

  if (digitalRead(PIN_CALIBRATION) == LOW) {
    currentMode = MODE_CALIBRATION;
    digitalWrite(PIN_RELAY, HIGH); 
    Serial.println(F("MOD: CAL"));
  } 
  else {
    int buttonAdc = analogRead(PIN_BUTTONS);
    if (buttonAdc >= 800 && buttonAdc <= 1600) {
      currentMode = MODE_SETTING;
      digitalWrite(PIN_RELAY, LOW);
      Serial.println(F("MOD: SET"));
    } 
    else {
      currentMode = MODE_NORMAL_PROTECTION;
      digitalWrite(PIN_RELAY, LOW);
      stateTimer = millis(); 
      Serial.println(F("MOD: PRT"));
    }
  }
}

void loop() {
  sysVolt = readVoltage();
  sysAmp = readCurrent();
  sysTemp1 = readTemp1();
  sysTemp2 = readTemp2();

  if (millis() - serialTelemetryTimer >= 1000) {
    serialTelemetryTimer = millis();
    sendSerialTelemetry(sysVolt, sysAmp, sysTemp1, sysTemp2);
  }

  if (currentMode == MODE_CALIBRATION) { handleCalibrationMode(); return; } 
  else if (currentMode == MODE_SETTING) { handleSettingMode(); return; }
  
  if (sysVolt >= params.powerSurgeLimit) triggerFault(FAULT_SURGE);
  if (sysVolt <= params.blackoutLimit) triggerFault(FAULT_BLACKOUT);

  switch (currentState) {
    case STATE_STARTUP_DELAY: {
      digitalWrite(PIN_RELAY, LOW);
      
      if (!isVoltageSafe(sysVolt) || (params.ntc1Enable && sysTemp1 > (params.otpLimit - params.otpHysteresis))) {
        stateTimer = millis(); 
      } 
      else if (millis() - stateTimer >= (params.timeDelay * 1000UL)) {
        currentFault = FAULT_NONE;
        initialTempNTC2 = sysTemp2; 
        isFlowChecked = false;
        runningTimer = millis();
        orpTimer = millis();
        
        currentState = STATE_RUNNING;
        digitalWrite(PIN_RELAY, HIGH); 
        Serial.println(F("RLY: ON"));
      }
      break;
    }

    case STATE_RUNNING: {
      if (sysVolt > params.ovpLimit) checkFaultDelay(FAULT_OVP, params.ovpUvpDelay);
      else if (sysVolt < params.uvpLimit) checkFaultDelay(FAULT_UVP, params.ovpUvpDelay);
      else faultConditionTimer = millis(); 

      if (params.ntc1Enable && sysTemp1 >= params.otpLimit) triggerFault(FAULT_OTP);

      unsigned long runDuration = millis() - runningTimer;
      
      if (runDuration <= (params.lraTime * 1000UL)) {
        if (sysAmp >= params.lraLimit) {
          lraRetryCount++;
          Serial.print(F("LRA Trip: ")); Serial.println(lraRetryCount);
          
          if (lraRetryCount >= params.lraRetryLimit) {
            currentState = STATE_LOCKOUT;
            digitalWrite(PIN_RELAY, LOW);
            Serial.println(F("SYS: LRA LOCKOUT"));
          } else triggerFault(FAULT_LRA);
        }
      } 
      else {
        lraRetryCount = 0; 
        if (sysAmp >= params.ocpLimit) checkFaultDelay(FAULT_OCP, params.ocpDelay);
        if (params.ucpLimit > 0 && sysAmp <= params.ucpLimit) checkFaultDelay(FAULT_UCP, 60);
      }

      if (params.ntc2Enable && !isFlowChecked && runDuration >= (params.refFlowDelay * 1000UL)) {
        isFlowChecked = true;
        if (sysTemp2 < (initialTempNTC2 + params.refDeltaTemp)) {
          flowRetryCount++;
          Serial.print(F("FLW Err: ")); Serial.println(flowRetryCount);
          
          if (flowRetryCount >= params.flowFaultRetryLimit) {
            currentState = STATE_LOCKOUT;
            digitalWrite(PIN_RELAY, LOW);
            Serial.println(F("SYS: FLW LOCKOUT"));
          } else triggerFault(FAULT_REF_FLOW);
        } else flowRetryCount = 0; 
      }

      if (sysAmp < params.minCurrentThreshold) orpTimer = millis(); 
      else if (millis() - orpTimer >= (params.orpTimeLimit * 60000UL)) {
        currentState = STATE_ORP_REST;
        stateTimer = millis(); 
        digitalWrite(PIN_RELAY, LOW);
        Serial.println(F("SYS: ORP REST"));
      }
      break;
    }

    case STATE_FAULT: {
      digitalWrite(PIN_RELAY, LOW);
      
      bool readyToRecover = false;
      if (currentFault == FAULT_OVP && sysVolt <= (params.ovpLimit - params.voltageHysteresis)) readyToRecover = true;
      else if (currentFault == FAULT_UVP && sysVolt >= (params.uvpLimit + params.voltageHysteresis)) readyToRecover = true;
      else if (currentFault == FAULT_OTP && sysTemp1 <= (params.otpLimit - params.otpHysteresis)) readyToRecover = true;
      else if (currentFault != FAULT_OVP && currentFault != FAULT_UVP && currentFault != FAULT_OTP) readyToRecover = true;

      if (readyToRecover) {
        Serial.println(F("SYS: FLT RECOVERED"));
        currentState = STATE_STARTUP_DELAY;
        stateTimer = millis(); 
      }
      break;
    }

    case STATE_ORP_REST: {
      digitalWrite(PIN_RELAY, LOW);
      bool tempCool = (!params.ntc1Enable || sysTemp1 <= 60);
      
      if ((millis() - stateTimer >= (params.orpRestDelay * 60000UL)) && tempCool) {
        Serial.println(F("SYS: ORP DONE"));
        currentState = STATE_STARTUP_DELAY;
        stateTimer = millis();
      }
      break;
    }

    case STATE_LOCKOUT: {
      digitalWrite(PIN_RELAY, LOW);
      break;
    }
  }
  
  updateDisplay(sysVolt, sysAmp);
  handleIndicators();
}

void triggerFault(FaultType fault) {
  if (currentState == STATE_FAULT && currentFault == fault) return; 

  currentFault = fault;
  currentState = STATE_FAULT;
  digitalWrite(PIN_RELAY, LOW);
  faultConditionTimer = millis();
  
  Serial.print(F("\nFLT: ")); Serial.println(fault);
}

void checkFaultDelay(FaultType fault, uint8_t delaySeconds) {
  if (millis() - faultConditionTimer >= (delaySeconds * 1000UL)) triggerFault(fault);
}

bool isVoltageSafe(int v) {
  return (v > params.uvpLimit && v < params.ovpLimit);
}

void sendSerialTelemetry(int v, int a, int t1, int t2) {
  Serial.print(F("MODE:")); Serial.print(currentMode);
  Serial.print(F(" | ST:")); Serial.print(currentState);
  Serial.print(F(" | FLT:")); Serial.print(currentFault);
  Serial.print(F(" | V:")); Serial.print(v);
  Serial.print(F(" | A(mA):")); Serial.print(a); 
  Serial.print(F(" | T1:")); Serial.print(t1);
  Serial.print(F(" | T2:")); Serial.println(t2);
}

void restartSystem() {
  Serial.println(F("SYS: REBOOT"));
  delay(100);
  NVIC_SystemReset();
}

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
  } 
  else if (currentState == STATE_STARTUP_DELAY) {
    if (currentMillis - lastLedTimer >= 500) {
      lastLedTimer = currentMillis;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState);
    }
  } 
  else if (currentState == STATE_FAULT || currentState == STATE_LOCKOUT || currentState == STATE_ORP_REST) {
    uint8_t maxBlinks = (currentState == STATE_ORP_REST) ? 10 : currentFault;
    if (maxBlinks == 0) maxBlinks = 1;

    if (!isBlinking) {
      if (currentMillis - lastLedTimer >= 1500) { 
        isBlinking = true; blinkCount = 0; lastLedTimer = currentMillis;
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
      isBeeping = true; beepCount = 0; lastBuzzerToggle = currentMillis;
      buzzerState = true; digitalWrite(PIN_BUZZER, HIGH);
    }
    if (isBeeping) {
      if (currentMillis - lastBuzzerToggle >= 300) { 
        lastBuzzerToggle = currentMillis;
        buzzerState = !buzzerState;
        digitalWrite(PIN_BUZZER, buzzerState);

        if (!buzzerState) {
          beepCount++;
          if (beepCount >= 5) { isBeeping = false; lastBuzzerInterval = currentMillis; }
        }
      }
    }
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    isBeeping = false; lastBuzzerInterval = currentMillis - 1800000UL; 
  }
}