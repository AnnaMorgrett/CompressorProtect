#include "protection.h"

static SystemState g_state = STATE_BOOT_SAFE;
static FaultCode g_activeFault = FAULT_NONE;
static uint8_t g_retryCount = 0;

static unsigned long g_stateStartTime = 0;
static unsigned long g_stateTargetDurationMs = 0;

static unsigned long g_voltageAbnormalTimer = 0;
static unsigned long g_voltageQualifyTimer = 0;
static unsigned long g_currentOverloadTimer = 0;
static unsigned long g_lraTimer = 0;
static unsigned long g_continuousRunStartTime = 0;

static inline void relayOff() {
  digitalWrite(PIN_RELAY, LOW);
}

static inline void relayOn() {
  digitalWrite(PIN_RELAY, HIGH);
}

void protectionInit() {
  pinMode(PIN_RELAY, OUTPUT);
  relayOff();

  g_state = STATE_BOOT_SAFE;
  g_activeFault = FAULT_NONE;
  g_retryCount = 0;
  g_stateStartTime = millis();
}

SystemState protectionGetState() {
  return g_state;
}

FaultCode protectionGetActiveFault() {
  return g_activeFault;
}

uint8_t protectionGetRetryCount() {
  return g_retryCount;
}

bool protectionIsRelayActive() {
  return digitalRead(PIN_RELAY) == HIGH;
}

unsigned long protectionGetTimeRemainingMs() {
  unsigned long now = millis();
  unsigned long elapsed = now - g_stateStartTime;
  if (elapsed >= g_stateTargetDurationMs) {
    return 0;
  }
  return (g_stateTargetDurationMs - elapsed);
}

static void triggerTrip(FaultCode fault) {
  relayOff();
  g_activeFault = fault;
  storageRecordFault(fault);

  g_voltageAbnormalTimer = 0;
  g_currentOverloadTimer = 0;
  g_lraTimer = 0;

  if (fault == FAULT_RUNNING_OVERCURRENT || fault == FAULT_START_OVERCURRENT || fault == FAULT_LOCKED_ROTOR) {
    g_retryCount++;
    if (g_retryCount >= OCP_MAX_RETRIES) {
      g_state = STATE_LOCKOUT;
      g_stateStartTime = millis();
      g_stateTargetDurationMs = 0;
      return;
    }
  }

  g_state = STATE_FAULT_RECOVERY;
  g_stateStartTime = millis();
  g_stateTargetDurationMs = 0;
}

bool protectionRequestReset() {
  if (g_state == STATE_LOCKOUT) {
    const SensorData& s = sensorGetData();
    const SystemConfig& cfg = storageGetConfig();

    uint16_t vRecLow = (uint16_t)cfg.uvpCutoff + DEFAULT_UVP_REC_HYST;
    uint16_t vRecHigh = (uint16_t)cfg.ovpCutoff - DEFAULT_OVP_REC_HYST;

    if (!s.voltageValid || s.voltageFastRMS < vRecLow || s.voltageFastRMS > vRecHigh) {
      return false; // Voltage outside recovery window: reject reset
    }

    g_retryCount = 0;
    g_activeFault = FAULT_NONE;
    g_state = STATE_POWER_QUALIFY;
    g_stateStartTime = millis();
    g_voltageQualifyTimer = millis();
    return true;
  }
  return false;
}

bool protectionRequestBypass() {
  if (g_state == STATE_WAIT_MIN_OFF) {
    const SensorData& s = sensorGetData();
    const SystemConfig& cfg = storageGetConfig();

    uint16_t vRecLow = (uint16_t)cfg.uvpCutoff + DEFAULT_UVP_REC_HYST;
    uint16_t vRecHigh = (uint16_t)cfg.ovpCutoff - DEFAULT_OVP_REC_HYST;

    if (s.voltageValid && s.voltageFastRMS >= vRecLow && s.voltageFastRMS <= vRecHigh) {
      g_state = STATE_STARTING;
      g_stateStartTime = millis();
      g_stateTargetDurationMs = 0;
      relayOn();
      return true;
    }
  }
  return false;
}

void protectionUpdate(const SensorData& sensors) {
  unsigned long now = millis();
  const SystemConfig& cfg = storageGetConfig();

  uint16_t vFast = sensors.voltageFastRMS;
  uint16_t vOverTrip = (uint16_t)cfg.ovpCutoff;
  uint16_t vOverRec  = (uint16_t)cfg.ovpCutoff - DEFAULT_OVP_REC_HYST;
  uint16_t vUnderTrip = (uint16_t)cfg.uvpCutoff;
  uint16_t vUnderRec  = (uint16_t)cfg.uvpCutoff + DEFAULT_UVP_REC_HYST;

  uint16_t iRunLimit_mA = (uint16_t)cfg.currentRunX10 * 100;
  uint16_t iLraLimit_mA = (uint16_t)cfg.currentLraX10 * 100;
  uint16_t iMeas_mA = sensors.currentMilliAmps;

  switch (g_state) {
    case STATE_BOOT_SAFE:
      relayOff();
      if ((now - g_stateStartTime) >= 200) {
        g_state = STATE_SELF_TEST;
        g_stateStartTime = now;
      }
      break;

    case STATE_SELF_TEST:
      relayOff();
      if ((now - g_stateStartTime) >= 500) {
        g_state = STATE_POWER_QUALIFY;
        g_stateStartTime = now;
        g_voltageQualifyTimer = now;
      }
      break;

    case STATE_POWER_QUALIFY: {
      relayOff();
      if (sensors.voltageValid && vFast >= vUnderRec && vFast <= vOverRec) {
        if ((now - g_voltageQualifyTimer) >= VOLTAGE_QUALIFY_TIME_MS) {
          g_state = STATE_WAIT_MIN_OFF;
          g_stateStartTime = now;
          g_stateTargetDurationMs = (unsigned long)cfg.delayTimerX10 * 10000UL;
        }
      } else {
        g_voltageQualifyTimer = now;
      }
      break;
    }

    case STATE_WAIT_MIN_OFF: {
      relayOff();
      if (sensors.voltageValid && (vFast < vUnderTrip || vFast > vOverTrip)) {
        g_state = STATE_POWER_QUALIFY;
        g_stateStartTime = now;
        g_voltageQualifyTimer = now;
        break;
      }

      if ((now - g_stateStartTime) >= g_stateTargetDurationMs) {
        g_state = STATE_STARTING;
        g_stateStartTime = now;
        g_stateTargetDurationMs = 0;
        relayOn();
      }
      break;
    }

    case STATE_STARTING: {
      if (sensors.voltageValid && vFast < (uint16_t)VOLTAGE_SAG_CRITICAL) {
        triggerTrip(FAULT_CRITICAL_VOLTAGE_SAG);
        break;
      }

      unsigned long startElapsed = now - g_stateStartTime;
      if (startElapsed >= STARTUP_INRUSH_WINDOW_MS) {
        if (iMeas_mA > iLraLimit_mA) {
          if (g_lraTimer == 0) g_lraTimer = now;
          if ((now - g_lraTimer) >= CURRENT_LRA_TIME_MS) {
            triggerTrip(FAULT_LOCKED_ROTOR);
            break;
          }
        } else {
          g_lraTimer = 0;
          g_state = STATE_NORMAL_RUN;
          g_stateStartTime = now;
          g_continuousRunStartTime = now;
        }
      }
      break;
    }

    case STATE_NORMAL_RUN: {
      // 1. Critical Voltage Sag (< 160V immediate)
      if (sensors.voltageValid && vFast < (uint16_t)VOLTAGE_SAG_CRITICAL) {
        triggerTrip(FAULT_CRITICAL_VOLTAGE_SAG);
        break;
      }

      // 2. Over-Voltage Protection (OVP)
      if (sensors.voltageValid && vFast > vOverTrip) {
        if (g_voltageAbnormalTimer == 0) g_voltageAbnormalTimer = now;
        if ((now - g_voltageAbnormalTimer) >= VOLTAGE_FAULT_TIME_MS) {
          triggerTrip(FAULT_OVERVOLTAGE);
          break;
        }
      }
      // 3. Under-Voltage Protection (UVP)
      else if (sensors.voltageValid && vFast < vUnderTrip) {
        if (g_voltageAbnormalTimer == 0) g_voltageAbnormalTimer = now;
        if ((now - g_voltageAbnormalTimer) >= VOLTAGE_FAULT_TIME_MS) {
          triggerTrip(FAULT_UNDERVOLTAGE);
          break;
        }
      } else {
        g_voltageAbnormalTimer = 0;
      }

      // 4. Stalled Current (LRA)
      if (iMeas_mA > iLraLimit_mA) {
        if (g_lraTimer == 0) g_lraTimer = now;
        if ((now - g_lraTimer) >= CURRENT_LRA_TIME_MS) {
          triggerTrip(FAULT_LOCKED_ROTOR);
          break;
        }
      } else {
        g_lraTimer = 0;
      }

      // 5. Running Overcurrent (OCP)
      if (iMeas_mA > iRunLimit_mA) {
        if (g_currentOverloadTimer == 0) g_currentOverloadTimer = now;
        if ((now - g_currentOverloadTimer) >= CURRENT_OCP_RUN_DELAY_MS) {
          triggerTrip(FAULT_RUNNING_OVERCURRENT);
          break;
        }
      } else {
        g_currentOverloadTimer = 0;
      }

      // 6. Rolling Retry Reset: 10 min continuous run clears retry counter
      if ((now - g_continuousRunStartTime) >= OCP_RETRY_RESET_TIME_MS) {
        g_retryCount = 0;
      }
      break;
    }

    case STATE_FAULT_RECOVERY: {
      relayOff();
      if (sensors.voltageValid && vFast >= vUnderRec && vFast <= vOverRec) {
        if (g_voltageQualifyTimer == 0) g_voltageQualifyTimer = now;
        if ((now - g_voltageQualifyTimer) >= VOLTAGE_QUALIFY_TIME_MS) {
          g_activeFault = FAULT_NONE;
          g_state = STATE_WAIT_MIN_OFF;
          g_stateStartTime = now;
          g_stateTargetDurationMs = (unsigned long)cfg.delayTimerX10 * 10000UL;
          g_voltageQualifyTimer = 0;
        }
      } else {
        g_voltageQualifyTimer = 0;
      }
      break;
    }

    case STATE_LOCKOUT:
      relayOff();
      break;

    default:
      relayOff();
      g_state = STATE_BOOT_SAFE;
      break;
  }
}
