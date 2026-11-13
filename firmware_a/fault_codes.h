#ifndef FAULT_CODES_H
#define FAULT_CODES_H

#include <Arduino.h>

/**
 * ============================================================================
 * COMPRESSOR PROTECT - FAULT CODE DEFINITIONS & METADATA
 * ============================================================================
 */

enum FaultSeverity {
  SEVERITY_NONE,
  SEVERITY_WARNING,
  SEVERITY_TRIP,
  SEVERITY_LOCKOUT
};

enum FaultCode {
  FAULT_NONE                  = 0,
  FAULT_OVERVOLTAGE           = 1,  // E-01: AC Voltage > OVP limit for > 5s
  FAULT_UNDERVOLTAGE          = 2,  // E-02: AC Voltage < UVP limit for > 5s
  FAULT_CRITICAL_VOLTAGE_SAG  = 3,  // E-03: AC Voltage < 160V immediate cutoff
  FAULT_RUNNING_OVERCURRENT   = 4,  // E-04: Sustained running current > I_RUN for > 4s
  FAULT_START_OVERCURRENT     = 5,  // E-05: High starting current beyond inrush window
  FAULT_LOCKED_ROTOR          = 6,  // E-06: Current > I_LRA for > 2s (stalled compressor)
  FAULT_NTC1_UNAVAILABLE      = 7,  // NC-1: Shell thermistor hardware ADC missing (Warning)
  FAULT_NTC2_UNAVAILABLE      = 8,  // NC-2: Liquid-line thermistor ADC missing (Warning)
  FAULT_CONFIG_INVALID        = 9,  // E-09: Option bytes checksum mismatch / corruption
  FAULT_LOCKOUT               = 10  // LOCK: 3 failed start attempts within retry window
};

// Returns a 4-character string representation formatted for 7-segment display
inline const char* faultGetDisplayCode(FaultCode code) {
  switch (code) {
    case FAULT_OVERVOLTAGE:          return "E-01";
    case FAULT_UNDERVOLTAGE:         return "E-02";
    case FAULT_CRITICAL_VOLTAGE_SAG: return "E-03";
    case FAULT_RUNNING_OVERCURRENT:  return "E-04";
    case FAULT_START_OVERCURRENT:    return "E-05";
    case FAULT_LOCKED_ROTOR:         return "E-06";
    case FAULT_NTC1_UNAVAILABLE:     return "NC-1";
    case FAULT_NTC2_UNAVAILABLE:     return "NC-2";
    case FAULT_CONFIG_INVALID:       return "E-09";
    case FAULT_LOCKOUT:              return "LOCK";
    default:                         return "    ";
  }
}

// Determines if the fault mandates immediate relay de-energization
inline bool faultRequiresShutdown(FaultCode code) {
  return (code >= FAULT_OVERVOLTAGE && code <= FAULT_LOCKED_ROTOR) ||
         (code == FAULT_CONFIG_INVALID || code == FAULT_LOCKOUT);
}

// Determines if automatic retry and countdown is permitted
inline bool faultAllowsAutoRetry(FaultCode code) {
  return (code >= FAULT_OVERVOLTAGE && code <= FAULT_LOCKED_ROTOR);
}

#endif // FAULT_CODES_H
