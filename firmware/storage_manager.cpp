#include "storage_manager.h"

static SystemConfig g_config;

static uint8_t computeChecksum(const SystemConfig& cfg) {
  uint8_t c = 0xAA;
  const uint8_t* p = (const uint8_t*)&cfg;
  for (uint8_t i = 0; i < 8; i++) {
    c ^= p[i];
  }
  return c;
}

void storageLoadFactoryDefaults() {
  g_config.magic = STORAGE_MAGIC_BYTE;
  g_config.version = STORAGE_VERSION;
  g_config.ovpCutoff = DEFAULT_OVP_TRIP;
  g_config.uvpCutoff = DEFAULT_UVP_TRIP;
  g_config.currentRunX10 = DEFAULT_CURRENT_RUN_X10;
  g_config.currentLraX10 = DEFAULT_CURRENT_LRA_X10;
  g_config.delayTimerX10 = DEFAULT_DELAY_TIMER_X10;
  g_config.lastFaultCode = FAULT_NONE;
  g_config.checksum = computeChecksum(g_config);
}

bool storageInit() {
  EEPROM.begin();

  uint8_t* p = (uint8_t*)&g_config;
  for (uint8_t i = 0; i < sizeof(SystemConfig); i++) {
    p[i] = EEPROM.read(i);
  }

  uint8_t expectedChecksum = computeChecksum(g_config);

  bool valid = (g_config.magic == STORAGE_MAGIC_BYTE) &&
               (g_config.version == STORAGE_VERSION) &&
               (g_config.checksum == expectedChecksum) &&
               (g_config.ovpCutoff >= CFG_OVP_MIN && g_config.ovpCutoff <= CFG_OVP_MAX) &&
               (g_config.uvpCutoff >= CFG_UVP_MIN && g_config.uvpCutoff <= CFG_UVP_MAX) &&
               (g_config.currentRunX10 >= CFG_OCP_RUN_MIN_X10 && g_config.currentRunX10 <= CFG_OCP_RUN_MAX_X10) &&
               (g_config.currentLraX10 >= CFG_OCP_LRA_MIN_X10 && g_config.currentLraX10 <= CFG_OCP_LRA_MAX_X10) &&
               (g_config.delayTimerX10 >= CFG_DELAY_MIN_X10 && g_config.delayTimerX10 <= CFG_DELAY_MAX_X10);

  if (!valid) {
    storageLoadFactoryDefaults();
    storageSaveConfig();
    return false;
  }
  return true;
}

SystemConfig& storageGetConfig() {
  return g_config;
}

bool storageSaveConfig() {
  g_config.checksum = computeChecksum(g_config);

  const uint8_t* p = (const uint8_t*)&g_config;
  for (uint8_t i = 0; i < sizeof(SystemConfig); i++) {
    EEPROM.update(i, p[i]);
  }

  return EEPROM.commit();
}

void storageRecordFault(FaultCode code) {
  if (g_config.lastFaultCode != (uint8_t)code) {
    g_config.lastFaultCode = (uint8_t)code;
    storageSaveConfig();
  }
}
