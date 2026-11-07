// debug_report.ino - second tab of the sketch: the detailed debug log.
// Put this file in the same folder as CH32V003protector.ino (it becomes a tab).
//
// Needs in the main file:   #include "debug_tx.h"   at the top
//                           dbgBegin();              in setup()   (replaces Serial.begin)
//                           debugReport();           in loop()    (replaces cetakDataSerial)
//
// DEBUG_PLOT = false : text log, one line per second, to copy and paste
// DEBUG_PLOT = true  : numbers only, 5 lines per second, for the Serial Plotter
// Baud rate in the monitor / plotter: 19200.
//
// If the compiler says a name below is unknown, your sketch calls it something
// else: change the name or delete that one line.

const bool DEBUG_PLOT = false;

void debugReport() {
  static unsigned long lastCallUs = 0;
  static unsigned long maxLoopUs = 0;
  static long lastState = -1;
  static long lastError = -1;
  static long lastRelay = -1;

  // How long one pass through loop() takes (includes the debug sending itself).
  unsigned long nowUs = micros();
  unsigned long loopUs = 0;
  if (lastCallUs != 0) {              // the very first call has nothing to compare with
    loopUs = nowUs - lastCallUs;
  }
  lastCallUs = nowUs;
  if (loopUs > maxLoopUs) {
    maxLoopUs = loopUs;
  }

  dbgService();                       // sends one queued character
  dbgSetPlot(DEBUG_PLOT);

  long relayNow = digitalRead(PIN_RELAY);

  // Events: a line is written the moment state, error or relay changes.
  if (lastState >= 0) {
    if ((long)currentState != lastState) dbgEvent("STATE", lastState, currentState);
    if ((long)currentError != lastError) dbgEvent("ERROR", lastError, currentError);
    if (relayNow != lastRelay)           dbgEvent("RELAY", lastRelay, relayNow);
  }
  lastState = currentState;
  lastError = currentError;
  lastRelay = relayNow;

  if (DEBUG_PLOT) {
    if (dbgStart(200)) {
      dbgVal("V", (long)teganganRMS);
      dbgVal("mA", (long)(arusRMS * 1000.0f));
      dbgEnd();
    }
    return;
  }

  if (dbgStart(1000)) {
    unsigned long now = millis();
    dbgVal("state", currentState);          // 0 init, 1 run, 2 error, 3 lockout
    dbgVal("err", currentError);            // 0 none, 1 OVP, 2 UVP, 3 OCP
    dbgVal("relay", relayNow);
    dbgVal1("V", (long)(teganganRMS * 10.0f));
    dbgVal3("A", (long)(arusRMS * 1000.0f));
    dbgVal("vpp", rawV_PeakToPeak);         // raw ADC peak-to-peak, voltage
    dbgVal("ipp", rawI_PeakToPeak);         // raw ADC value, current
    dbgVal("adcV", analogRead(PIN_VOLTAGE));
    dbgVal("adcI", analogRead(PIN_CURRENT));
    dbgVal("key", analogRead(PD6));         // 3-button ladder, 1023 = no key pressed
    dbgVal("retry", ocpRetryCount);
    dbgVal("st_s", (now - stateTimer) / 1000);                       // seconds in this state
    dbgVal("vbad", voltageBadTimer ? (long)(now - voltageBadTimer) : 0);   // ms voltage out of range
    dbgVal("lra", lraTimer ? (long)(now - lraTimer) : 0);            // ms above the stalled-current limit
    dbgVal("ocp", ocpRunTimer ? (long)(now - ocpRunTimer) : 0);      // ms above the running-current limit
    dbgVal("loop", loopUs);                 // microseconds for this pass of loop()
    dbgVal("lmax", maxLoopUs);              // slowest pass since the last line
    dbgVal("drop", dbgDropped);             // characters lost (queue full)
    dbgVal("up_s", now / 1000);
    dbgEnd();
    maxLoopUs = 0;
  }
}
