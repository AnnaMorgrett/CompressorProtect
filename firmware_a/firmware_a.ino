/**
 * ============================================================================
 * COMPRESSOR PROTECT - REFRIGERATOR COMPRESSOR PROTECTION CONTROLLER
 * Target Architecture: WCH CH32V003 (QingKe 32-bit RISC-V RV32EC, 48 MHz HSI)
 * Hardware Reference : archive_old/CH32Version.png (SOP-16 / TSSOP-20)
 * ============================================================================
 */

#include "pins.h"
#include "config.h"
#include "fault_codes.h"
#include "system_state.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include "button_manager.h"
#include "display_manager.h"
#include "alarm_manager.h"
#include "protection.h"
#include "diagnostics.h"

#include <ch32v00x_iwdg.h>

static void initWatchdog() {
  // LSI clock ~128 kHz. Prescaler /64 -> 2000 Hz. Reload 3999 -> 2.000s timeout.
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_64);
  IWDG_SetReload(3999);
  IWDG_ReloadCounter();
  IWDG_Enable();
}

void setup() {
  // 1. Force compressor relay OFF immediately on boot/reset
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  // 2. Initialize Hardware Subsystems
  alarmInit();
  storageInit();
  sensorInit();
  buttonInit();
  displayInit();
  protectionInit();
  diagnosticsInit();

  // 3. Arm Hardware Independent Watchdog (~2.0s timeout)
  initWatchdog();
}

void loop() {
  // 1. Sensor Acquisition & Digital Filtering
  sensorUpdate();
  const SensorData& sensors = sensorGetData();

  // 2. Button Sampling & Event Generation
  buttonUpdate(sensors.keyRawADC);
  ButtonEvent btnEvent = buttonGetEvent();

  SystemState currentState = protectionGetState();

  if (btnEvent != BTN_EVENT_NONE) {
    alarmButtonChirp();
  }

  // 3. User Interface & State Interactions
  if (currentState == STATE_LOCKOUT) {
    if (btnEvent == BTN_EVENT_MENU_SHORT || btnEvent == BTN_EVENT_MENU_LONG) {
      if (protectionRequestReset()) {
        displayExitMenu();
      }
    } else if (btnEvent == BTN_EVENT_PLUS_SHORT || btnEvent == BTN_EVENT_MINUS_SHORT) {
      alarmMuteBuzzer();
    }
  } else if (currentState == STATE_WAIT_MIN_OFF && buttonIsPlusHeld(QUICK_BYPASS_HOLD_MS)) {
    protectionRequestBypass();
  } else if (currentState == STATE_FAULT_RECOVERY && btnEvent == BTN_EVENT_MENU_SHORT) {
    alarmMuteBuzzer();
  } else {
    displayHandleButton(btnEvent);
  }

  // 4. Primary Safety Supervisor (Highest Priority)
  protectionUpdate(sensors);

  // 5. TM1637 Display Telemetry Refresh
  displayUpdate(protectionGetState(), protectionGetActiveFault(), sensors, protectionGetTimeRemainingMs());

  // 6. Status LED & Alarm Buzzer Patterns
  alarmUpdate(protectionGetState(), protectionGetActiveFault());

  // 7. System Diagnostics & Telemetry Logging
  diagnosticsUpdate(protectionGetState(), protectionGetActiveFault(), sensors);

  // 8. Service Hardware Independent Watchdog (IWDG)
  IWDG_ReloadCounter();
}
