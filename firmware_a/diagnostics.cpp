#include "diagnostics.h"

#if ENABLE_DEBUG_LOG
#include "debug_tx.h"
#endif

void diagnosticsInit() {
#if ENABLE_DEBUG_LOG
  dbgBegin();
#endif
}

void diagnosticsUpdate(SystemState state, FaultCode fault, const SensorData& sensors) {
#if ENABLE_DEBUG_LOG
  static unsigned long lastLogTime = 0;
  unsigned long now = millis();

  dbgService();

  if ((now - lastLogTime) >= 1000) {
    lastLogTime = now;
    if (dbgStart(1000)) {
      dbgVal("state", (long)state);
      dbgVal("fault", (long)fault);
      dbgVal("relay", protectionIsRelayActive() ? 1 : 0);
      dbgVal1("V", (long)(sensors.voltageFastRMS * 10.0f));
      dbgVal3("A", (long)(sensors.currentRMS * 1000.0f));
      dbgVal("vpp", sensors.voltagePeakToPeak);
      dbgVal("key", sensors.keyRawADC);
      dbgVal("retry", protectionGetRetryCount());
      dbgVal("rem_s", protectionGetTimeRemainingMs() / 1000);
      dbgVal("up_s", now / 1000);
      dbgEnd();
    }
  }
#else
  // Debug telemetry compiled out in production release to save flash and avoid PC7 conflict
  (void)state;
  (void)fault;
  (void)sensors;
#endif
}

