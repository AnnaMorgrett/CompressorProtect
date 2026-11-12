#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <Arduino.h>

/**
 * ============================================================================
 * COMPRESSOR PROTECT - SYSTEM STATE MACHINE ENUMERATIONS
 * ============================================================================
 */

enum SystemState {
  STATE_BOOT_SAFE,        // Relay forced OFF, GPIO initialized, watchdog primed
  STATE_SELF_TEST,        // Memory, sensor plausibility, and configuration validation
  STATE_POWER_QUALIFY,    // Measuring line voltage stability before starting timer
  STATE_WAIT_MIN_OFF,     // Enforcing minimum compressor OFF countdown (300s)
  STATE_STANDBY,          // System qualified and ready for compressor engagement
  STATE_STARTING,         // Relay engaged, monitoring startup inrush vs locked rotor
  STATE_NORMAL_RUN,       // Compressor operating under continuous safety supervision
  STATE_NORMAL_STOP,      // Controlled compressor disengagement
  STATE_FAULT_RECOVERY,   // Fault detected, relay de-energized, waiting for safe conditions
  STATE_SENSOR_FAULT,     // Critical sensor integrity failure preventing safe operation
  STATE_LOCKOUT           // Retries exhausted or critical safety violation; requires manual reset
};

#endif // SYSTEM_STATE_H
