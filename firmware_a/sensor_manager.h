#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "pins.h"

/**
 * ============================================================================
 * COMPRESSOR PROTECT - SENSOR ACQUISITION & VALIDATION SUBSYSTEM
 * ============================================================================
 */

enum SensorStatus {
  SENSOR_STATUS_OK,
  SENSOR_STATUS_OUT_OF_RANGE,
  SENSOR_STATUS_UNAVAILABLE_NO_ADC
};

struct SensorData {
  uint16_t voltageFastRMS;     // Fast 20ms voltage (Volts AC RMS, e.g. 220)
  uint16_t voltageDisplayRMS;  // Filtered smooth voltage (Volts AC RMS)
  uint16_t voltagePeakToPeak;  // Raw ADC peak-to-peak amplitude

  uint16_t currentMilliAmps;   // AC current in mA (e.g. 1200 = 1.20 A)
  uint16_t currentVariance;    // Raw variance accumulator

  int16_t  ntc1ShellTempC;     // Compressor body temp (Celsius, Sentinel -999)
  SensorStatus ntc1Status;     // SENSOR_STATUS_UNAVAILABLE_NO_ADC on CH32V003

  int16_t  ntc2LiquidLineTempC;// Line temp (Celsius, Sentinel -999)
  SensorStatus ntc2Status;     // SENSOR_STATUS_UNAVAILABLE_NO_ADC on CH32V003

  uint16_t keyRawADC;          // Raw ADC from resistor ladder
  bool voltageValid;           // True if voltage reading is 50V - 320V
  bool currentValid;           // True if current reading is 0 - 20A

  // Convenience float accessors
  float getVoltageFastRMS() const { return (float)voltageFastRMS; }
  float getVoltageDisplayRMS() const { return (float)voltageDisplayRMS; }
  float getCurrentRMS() const { return (float)currentMilliAmps * 0.001f; }
};

// Initializes ADC hardware registers and pin modes
void sensorInit();

// Fast periodic update call (non-blocking, samples AC cycle windows)
void sensorUpdate();

// Retrieves the latest validated sensor telemetry
const SensorData& sensorGetData();

// Allows dynamic calibration adjustments
void sensorSetVoltageCal(float cal);
void sensorSetCurrentCal(float cal);
float sensorGetVoltageCal();
float sensorGetCurrentCal();

// Pure integer square-root routine
uint32_t isqrt32(uint32_t n);

#endif // SENSOR_MANAGER_H
