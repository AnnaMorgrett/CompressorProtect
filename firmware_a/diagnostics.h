#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <Arduino.h>
#include "config.h"
#include "system_state.h"
#include "fault_codes.h"
#include "sensor_manager.h"
#include "protection.h"

/**
 * ============================================================================
 * COMPRESSOR PROTECT - SYSTEM DIAGNOSTICS & TELEMETRY
 * ============================================================================
 */

void diagnosticsInit();

// Periodic telemetry output call (non-blocking, disabled by default)
void diagnosticsUpdate(SystemState state, FaultCode fault, const SensorData& sensors);

#endif // DIAGNOSTICS_H

