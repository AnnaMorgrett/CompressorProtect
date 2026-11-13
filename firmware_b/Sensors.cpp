#include "Sensors.h"
#include <math.h>

void initSensors() {
  pinMode(PIN_VOLTAGE_SENS, INPUT);
  pinMode(PIN_CURRENT_SENS, INPUT);
  // NTC pins PC1/PC2 have no ADC on CH32V003 — not configured.
}

float getTrueRMS(uint8_t pin) {
  analogRead(pin);
  delayMicroseconds(100);
  analogRead(pin);
  delayMicroseconds(50);

  unsigned long startMicros = micros();
  int32_t  offset = analogRead(pin);
  int32_t  sum    = 0;
  uint32_t sumSq  = 0;
  uint32_t count  = 0;

  while (micros() - startMicros < 40000UL) {
    int32_t val = analogRead(pin) - offset;
    sum   += val;
    sumSq += (uint32_t)(val * val);
    count++;
  }

  if (count == 0) return 0.0f;

  float mean     = (float)sum / (float)count;
  float meanSq   = (float)(int32_t)sumSq / (float)count;
  float variance = meanSq - (mean * mean);
  if (variance < 0.0f) variance = 0.0f;
  return sqrtf(variance);
}

float readVoltage() {
  float rmsADC = getTrueRMS(PIN_VOLTAGE_SENS);
  if (rmsADC < 5.0f) return 0.0f;

  float trueVoltage = rmsADC * params.voltageMultiplier_x1000 / 1000.0f;

  static float filteredV = 0.0f;
  if (filteredV == 0.0f) { filteredV = trueVoltage; return filteredV; }

  float diff = trueVoltage - filteredV;
  if (diff < 0.0f) diff = -diff;
  if (diff > 3.0f) filteredV = trueVoltage;
  else             filteredV = (0.85f * filteredV) + (0.15f * trueVoltage);

  if (filteredV < 30.0f) filteredV = 0.0f;
  return filteredV;
}

float readCurrent() {
  float rmsADC = getTrueRMS(PIN_CURRENT_SENS);
  float trueCurrent = rmsADC * params.currentMultiplier_x1000 / 100000.0f;

  static float filteredA = 0.0f;
  if (trueCurrent < 0.10f) { filteredA = 0.0f; return 0.0f; }
  if (filteredA == 0.0f)   { filteredA = trueCurrent; return filteredA; }

  float diff = trueCurrent - filteredA;
  if (diff < 0.0f) diff = -diff;
  if (diff > 0.10f) filteredA = trueCurrent;
  else              filteredA = (0.85f * filteredA) + (0.15f * trueCurrent);

  return filteredA;
}