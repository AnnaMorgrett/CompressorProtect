#ifndef PROTECTION_H
#define PROTECTION_H

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
 * COMPRESSOR PROTECT - SAFETY SUPERVISOR & PROTECTION FINITE-STATE MACHINE
 * ============================================================================
 */

void protectionInit();

// Primary safety supervisor execution routine (highest priority, non-blocking)
void protectionUpdate(const SensorData& sensors);

// State inspection
SystemState protectionGetState();
FaultCode protectionGetActiveFault();
uint8_t protectionGetRetryCount();
unsigned long protectionGetTimeRemainingMs();
bool protectionIsRelayActive();

// Manual user interactions
bool protectionRequestReset();
bool protectionRequestBypass();

#endif // PROTECTION_H

