#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <TM1637Display.h>

// Pindahkan Enum dari file utama ke sini agar bisa diakses oleh modul UI
enum OperatingMode {
  MODE_NORMAL_PROTECTION,
  MODE_CALIBRATION,
  MODE_SETTING
};

enum SystemState {
  STATE_STARTUP_DELAY,
  STATE_RUNNING,
  STATE_FAULT,
  STATE_LOCKOUT,
  STATE_ORP_REST
};

enum FaultType {
  FAULT_NONE,
  FAULT_SURGE,
  FAULT_BLACKOUT,
  FAULT_OVP,
  FAULT_UVP,
  FAULT_LRA,
  FAULT_OCP,
  FAULT_UCP,
  FAULT_OTP,
  FAULT_REF_FLOW
};

// Deklarasi variabel global dari main.ino (extern) agar UI bisa membacanya
extern OperatingMode currentMode;
extern SystemState currentState;
extern FaultType currentFault;
extern unsigned long stateTimer;
extern uint8_t lraRetryCount;
extern uint8_t flowRetryCount;

extern float sysVolt;
extern float sysAmp;
extern float sysTemp1;
extern float sysTemp2;

// Deklarasi fungsi UI
void initUI();
void updateDisplay(float currentV, float currentA);
void handleSettingMode();
void handleCalibrationMode();
extern void restartSystem();

#endif
