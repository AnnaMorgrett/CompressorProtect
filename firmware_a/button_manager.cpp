#include "button_manager.h"

static ButtonId g_currentRawId = BTN_ID_NONE;
static ButtonId g_debouncedId = BTN_ID_NONE;
static ButtonId g_activeId = BTN_ID_NONE;

static unsigned long g_lastDebounceTime = 0;
static unsigned long g_pressStartTime = 0;
static unsigned long g_lastRepeatTime = 0;
static bool g_longPressDispatched = false;

static ButtonEvent g_pendingEvent = BTN_EVENT_NONE;

static ButtonId decodeRawAdc(uint16_t adc) {
  if (adc >= KEY_ADC_PLUS_MIN && adc <= KEY_ADC_PLUS_MAX) {
    return BTN_ID_PLUS;
  }
  if (adc > KEY_ADC_MENU_MIN && adc <= KEY_ADC_MENU_MAX) {
    return BTN_ID_MENU;
  }
  if (adc > KEY_ADC_MIN_MIN && adc <= KEY_ADC_MIN_MAX) {
    return BTN_ID_MINUS;
  }
  if (adc >= KEY_ADC_IDLE_MIN) {
    return BTN_ID_NONE;
  }
  return BTN_ID_NONE; // Ambiguous transient state
}

void buttonInit() {
  pinMode(PIN_KEY, INPUT);
  g_currentRawId = BTN_ID_NONE;
  g_debouncedId = BTN_ID_NONE;
  g_activeId = BTN_ID_NONE;
  g_pendingEvent = BTN_EVENT_NONE;
}

void buttonUpdate(uint16_t rawKeyAdc) {
  unsigned long now = millis();
  ButtonId rawId = decodeRawAdc(rawKeyAdc);

  // 1. Debounce state changes
  if (rawId != g_currentRawId) {
    g_currentRawId = rawId;
    g_lastDebounceTime = now;
  }

  if ((now - g_lastDebounceTime) >= KEY_DEBOUNCE_MS) {
    if (g_debouncedId != g_currentRawId) {
      g_debouncedId = g_currentRawId;

      // Transition: Press detected
      if (g_debouncedId != BTN_ID_NONE && g_activeId == BTN_ID_NONE) {
        g_activeId = g_debouncedId;
        g_pressStartTime = now;
        g_lastRepeatTime = now;
        g_longPressDispatched = false;
      }
      // Transition: Release detected
      else if (g_debouncedId == BTN_ID_NONE && g_activeId != BTN_ID_NONE) {
        if (!g_longPressDispatched) {
          // Short press released
          if (g_activeId == BTN_ID_PLUS)  g_pendingEvent = BTN_EVENT_PLUS_SHORT;
          if (g_activeId == BTN_ID_MENU)  g_pendingEvent = BTN_EVENT_MENU_SHORT;
          if (g_activeId == BTN_ID_MINUS) g_pendingEvent = BTN_EVENT_MINUS_SHORT;
        }
        g_activeId = BTN_ID_NONE;
      }
    }
  }

  // 2. Held and Long-Press Processing
  if (g_activeId != BTN_ID_NONE) {
    unsigned long holdDuration = now - g_pressStartTime;

    // Check Long Press
    if (!g_longPressDispatched && holdDuration >= KEY_LONG_PRESS_MS) {
      g_longPressDispatched = true;
      if (g_activeId == BTN_ID_PLUS)  g_pendingEvent = BTN_EVENT_PLUS_LONG;
      if (g_activeId == BTN_ID_MENU)  g_pendingEvent = BTN_EVENT_MENU_LONG;
      if (g_activeId == BTN_ID_MINUS) g_pendingEvent = BTN_EVENT_MINUS_LONG;
    }

    // Check Auto-Repeat (only for Plus and Minus)
    if (g_activeId == BTN_ID_PLUS || g_activeId == BTN_ID_MINUS) {
      if (holdDuration >= KEY_REPEAT_START_MS) {
        if ((now - g_lastRepeatTime) >= KEY_REPEAT_INTERVAL_MS) {
          g_lastRepeatTime = now;
          if (g_activeId == BTN_ID_PLUS)  g_pendingEvent = BTN_EVENT_PLUS_REPEAT;
          if (g_activeId == BTN_ID_MINUS) g_pendingEvent = BTN_EVENT_MINUS_REPEAT;
        }
      }
    }
  }
}

ButtonEvent buttonGetEvent() {
  ButtonEvent ev = g_pendingEvent;
  g_pendingEvent = BTN_EVENT_NONE;
  return ev;
}

bool buttonIsPlusHeld(unsigned long requiredHoldDurationMs) {
  if (g_activeId == BTN_ID_PLUS) {
    return (millis() - g_pressStartTime) >= requiredHoldDurationMs;
  }
  return false;
}

