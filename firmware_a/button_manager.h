#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "pins.h"

/**
 * ============================================================================
 * COMPRESSOR PROTECT - 3-BUTTON RESISTOR LADDER MANAGER (PD6 / AIN6)
 * ============================================================================
 */

enum ButtonId {
  BTN_ID_NONE,
  BTN_ID_PLUS,
  BTN_ID_MENU,
  BTN_ID_MINUS
};

enum ButtonEvent {
  BTN_EVENT_NONE,
  BTN_EVENT_PLUS_SHORT,
  BTN_EVENT_PLUS_LONG,
  BTN_EVENT_PLUS_REPEAT,
  BTN_EVENT_MINUS_SHORT,
  BTN_EVENT_MINUS_LONG,
  BTN_EVENT_MINUS_REPEAT,
  BTN_EVENT_MENU_SHORT,
  BTN_EVENT_MENU_LONG
};

// Initializes button inputs
void buttonInit();

// Non-blocking update called periodically (~10 ms)
void buttonUpdate(uint16_t rawKeyAdc);

// Consumes and returns the next pending button event (or BTN_EVENT_NONE)
ButtonEvent buttonGetEvent();

// Checks if the Plus button is currently held continuously (e.g. for 3s delay bypass)
bool buttonIsPlusHeld(unsigned long requiredHoldDurationMs);

#endif // BUTTON_MANAGER_H

