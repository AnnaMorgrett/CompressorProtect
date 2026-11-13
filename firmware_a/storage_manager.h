#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "fault_codes.h"
#include "EEPROM.h"

/**
 * ============================================================================
 * COMPRESSOR PROTECT - PERSISTENT STORAGE SUBSYSTEM (Option Bytes)
 * ============================================================================
 */

struct SystemConfig {
  uint8_t magic;          // STORAGE_MAGIC_BYTE (0xC3)
  uint8_t version;        // STORAGE_VERSION (0x01)
  uint8_t ovpCutoff;      // Over-Voltage Trip Limit (V)
  uint8_t uvpCutoff;      // Under-Voltage Trip Limit (V)
  uint8_t currentRunX10;  // Max Running Current (x10 A, e.g. 12 = 1.2 A)
  uint8_t currentLraX10;  // Max Stalled / LRA Current (x10 A, e.g. 16 = 1.6 A)
  uint8_t delayTimerX10;  // Startup Delay Timer (x10 s, e.g. 30 = 300 s)
  uint8_t lastFaultCode;  // Last recorded fault code (FaultCode enum)
  uint8_t checksum;       // XOR checksum of bytes 0..7
};

// Initializes storage, checks Option Bytes integrity, loads defaults if corrupted
bool storageInit();

// Retrieves the active system configuration
SystemConfig& storageGetConfig();

// Computes XOR checksum and commits parameters to Option Bytes
bool storageSaveConfig();

// Records the last recorded fault code to non-volatile memory
void storageRecordFault(FaultCode code);

// Restores factory defaults
void storageLoadFactoryDefaults();

#endif // STORAGE_MANAGER_H

