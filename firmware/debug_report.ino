/**
 * ============================================================================
 * COMPRESSOR PROTECT - ARDUINO IDE SECOND TAB (debug_report.ino)
 * ============================================================================
 * Provides detailed telemetry when ENABLE_DEBUG_LOG is set to 1 in config.h.
 * Disabled by default to prevent software UART from interfering with PC7 (CAL).
 */

#include "config.h"

#if ENABLE_DEBUG_LOG

#include "debug_tx.h"
#include "sensor_manager.h"
#include "protection.h"

const bool DEBUG_PLOT = false;

void debugReport() {
  static unsigned long lastCallUs = 0;
  static unsigned long maxLoopUs = 0;

  unsigned long nowUs = micros();
  unsigned long loopUs = (lastCallUs != 0) ? (nowUs - lastCallUs) : 0;
  lastCallUs = nowUs;
  if (loopUs > maxLoopUs) {
    maxLoopUs = loopUs;
  }

  dbgService();
  dbgSetPlot(DEBUG_PLOT);

  if (dbgStart(1000)) {
    unsigned long now = millis();
    const SensorData& s = sensorGetData();

    dbgVal("state", (long)protectionGetState());
    dbgVal("fault", (long)protectionGetActiveFault());
    dbgVal("relay", protectionIsRelayActive() ? 1 : 0);
    dbgVal1("V", (long)(s.voltageFastRMS * 10.0f));
    dbgVal3("A", (long)(s.currentRMS * 1000.0f));
    dbgVal("key", s.keyRawADC);
    dbgVal("retry", protectionGetRetryCount());
    dbgVal("rem_s", protectionGetTimeRemainingMs() / 1000);
    dbgVal("loop_us", loopUs);
    dbgVal("max_us", maxLoopUs);
    dbgVal("up_s", now / 1000);
    dbgEnd();
    maxLoopUs = 0;
  }
}

#else

// No-op placeholder when debug log is disabled
void debugReport() {
}

#endif // ENABLE_DEBUG_LOG
