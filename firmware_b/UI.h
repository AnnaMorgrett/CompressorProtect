#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <TM1637Display.h>

enum OperatingMode { MODE_NORMAL_PROTECTION, MODE_CALIBRATION, MODE_SETTING };
enum SystemState { STATE_STARTUP_DELAY, STATE_RUNNING, STATE_FAULT, STATE_LOCKOUT, STATE_ORP_REST };
enum FaultType { FAULT_NONE, FAULT_SURGE, FAULT_BLACKOUT, FAULT_OVP, FAULT_UVP, FAULT_LRA, FAULT_OCP, FAULT_UCP, FAULT_OTP, FAULT_REF_FLOW };

extern OperatingMode currentMode;
extern SystemState   currentState;
extern FaultType     currentFault;
extern unsigned long stateTimer;
extern uint8_t       lraRetryCount;
extern uint8_t       flowRetryCount;

extern uint16_t sysVolt;
extern uint16_t sysAmp_mA;
extern int16_t  sysTemp1;
extern int16_t  sysTemp2;

void initUI();
void updateDisplay(uint16_t currentV, uint16_t currentA_mA);
void handleSettingMode();
void handleCalibrationMode();
extern void triggerButtonChirp();
extern void restartSystem();

#endif
