#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "system_state.h"
#include "fault_codes.h"

/**
 * ============================================================================
 * COMPRESSOR PROTECT - ALARM, LED & BUZZER SUBSYSTEM (Non-Blocking)
 * ============================================================================
 */

void alarmInit();

// Non-blocking update called periodically in loop()
void alarmUpdate(SystemState state, FaultCode fault);

// Short audible feedback for user button interaction
void alarmButtonChirp();

// Silences the active audible buzzer alarm without resetting safety trip
void alarmMuteBuzzer();

// Checks if buzzer is currently muted
bool alarmIsBuzzerMuted();

#endif // ALARM_MANAGER_H

