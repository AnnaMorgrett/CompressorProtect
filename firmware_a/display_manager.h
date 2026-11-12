#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "system_state.h"
#include "fault_codes.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include "button_manager.h"

/**
 * ============================================================================
 * COMPRESSOR PROTECT - TM1637 4-DIGIT 7-SEGMENT DISPLAY SUBSYSTEM
 * ============================================================================
 */

enum DisplayMode {
  DISP_MODE_NORMAL_ROTATE, // Rotating telemetry pages (Voltage, Current, Temps, Countdown)
  DISP_MODE_FAULT,         // Alternating fault code and time remaining / LOCK
  DISP_MODE_MENU_PARAM,    // Menu parameter select (P-01 .. P-05)
  DISP_MODE_MENU_EDIT,     // Menu parameter value editing
  DISP_MODE_SAVE_CONFIRM   // "SAUE" acknowledgement
};

void displayInit();

// Periodic display update routine (non-blocking)
void displayUpdate(SystemState state, FaultCode fault, const SensorData& sensors, unsigned long timerRemainingMs);

// Handles UI button events for menu navigation and page cycling
bool displayHandleButton(ButtonEvent ev);

// Checks if display is currently in settings menu
bool displayIsInMenu();

// Exits menu back to home screen
void displayExitMenu();

#endif // DISPLAY_MANAGER_H
