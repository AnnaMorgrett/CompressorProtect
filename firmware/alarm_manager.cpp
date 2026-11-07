#include "alarm_manager.h"

static bool g_buzzerMuted = false;
static unsigned long g_lastPeriodicBuzzerTime = 0;
static unsigned long g_buttonChirpUntilMs = 0;

void alarmInit() {
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  g_buzzerMuted = false;
}

void alarmButtonChirp() {
  g_buttonChirpUntilMs = millis() + 15; // 15 ms audible click
  digitalWrite(PIN_BUZZER, HIGH);
}

void alarmMuteBuzzer() {
  g_buzzerMuted = true;
  digitalWrite(PIN_BUZZER, LOW);
}

bool alarmIsBuzzerMuted() {
  return g_buzzerMuted;
}

void alarmUpdate(SystemState state, FaultCode fault) {
  unsigned long now = millis();

  // Reset mute status when transitioning back to normal run or standby
  if (state == STATE_NORMAL_RUN || state == STATE_STANDBY) {
    g_buzzerMuted = false;
  }

  // 1. Button click override
  if (g_buttonChirpUntilMs > 0) {
    if (now < g_buttonChirpUntilMs) {
      digitalWrite(PIN_BUZZER, HIGH);
    } else {
      digitalWrite(PIN_BUZZER, LOW);
      g_buttonChirpUntilMs = 0;
    }
  }

  // 2. LED Status Patterns
  switch (state) {
    case STATE_BOOT_SAFE:
    case STATE_SELF_TEST:
      digitalWrite(PIN_LED, HIGH);
      break;

    case STATE_POWER_QUALIFY:
    case STATE_WAIT_MIN_OFF:
    case STATE_STANDBY: {
      // Slow 1 Hz blink (500 ms ON / 500 ms OFF)
      bool ledOn = (now % 1000) < 500;
      digitalWrite(PIN_LED, ledOn ? HIGH : LOW);
      break;
    }

    case STATE_STARTING:
    case STATE_NORMAL_RUN:
      digitalWrite(PIN_LED, HIGH); // Solid ON during normal compressor operation
      break;

    case STATE_FAULT_RECOVERY: {
      // Differentiated blink pattern based on fault type
      if (fault == FAULT_OVERVOLTAGE || fault == FAULT_UNDERVOLTAGE || fault == FAULT_CRITICAL_VOLTAGE_SAG) {
        // Double pulse every 2 seconds: ON 100ms, OFF 100ms, ON 100ms, OFF 1700ms
        uint16_t phase = now % 2000;
        bool on = (phase < 100) || (phase >= 200 && phase < 300);
        digitalWrite(PIN_LED, on ? HIGH : LOW);
      } else {
        // Triple pulse every 2 seconds: ON 100ms, OFF 100ms, ON 100ms, OFF 100ms, ON 100ms, OFF 1500ms
        uint16_t phase = now % 2000;
        bool on = (phase < 100) || (phase >= 200 && phase < 300) || (phase >= 400 && phase < 500);
        digitalWrite(PIN_LED, on ? HIGH : LOW);
      }
      break;
    }

    case STATE_LOCKOUT: {
      // Rapid continuous strobe (5 Hz: 100 ms ON / 100 ms OFF)
      bool ledOn = (now % 200) < 100;
      digitalWrite(PIN_LED, ledOn ? HIGH : LOW);
      break;
    }

    default:
      digitalWrite(PIN_LED, LOW);
      break;
  }

  // 3. Buzzer Alarm Patterns
  if (g_buttonChirpUntilMs == 0) { // Do not interfere with short button chirp
    if (g_buzzerMuted) {
      digitalWrite(PIN_BUZZER, LOW);
      return;
    }

    if (state == STATE_LOCKOUT) {
      // Lockout periodic reminder: 2 short beeps every 15 minutes
      if ((now - g_lastPeriodicBuzzerTime) >= BUZZER_INT_LOCKOUT_MS) {
        g_lastPeriodicBuzzerTime = now;
      }
      uint32_t elapsed = now - g_lastPeriodicBuzzerTime;
      bool beep = (elapsed < 120) || (elapsed >= 240 && elapsed < 360);
      digitalWrite(PIN_BUZZER, beep ? HIGH : LOW);
    } else if (state == STATE_FAULT_RECOVERY) {
      // Fault periodic reminder: 1 short beep every 30 minutes
      if ((now - g_lastPeriodicBuzzerTime) >= BUZZER_INT_ERROR_MS) {
        g_lastPeriodicBuzzerTime = now;
      }
      uint32_t elapsed = now - g_lastPeriodicBuzzerTime;
      bool beep = (elapsed < 150);
      digitalWrite(PIN_BUZZER, beep ? HIGH : LOW);
    } else {
      digitalWrite(PIN_BUZZER, LOW);
    }
  }
}

