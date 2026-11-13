#include "Sensors.h"

void initSensors() {
  pinMode(PIN_VOLTAGE_SENS, INPUT);
  pinMode(PIN_CURRENT_SENS, INPUT);
}

// Fast integer square root to replace sqrtf()
uint32_t isqrt32(uint32_t n) {
  uint32_t c = 0x40000000;
  uint32_t g = 0;
  while (c != 0) {
    if (n >= (g + c)) {
      n -= (g + c);
      g = (g >> 1) + c;
    } else {
      g >>= 1;
    }
    c >>= 2;
  }
  return g;
}

uint16_t getTrueRMS(uint8_t pin) {
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

  if (count == 0) return 0;

  int32_t mean = sum / (int32_t)count;
  uint32_t meanSq = sumSq / count;
  int32_t variance = (int32_t)meanSq - (mean * mean);
  if (variance < 0) variance = 0;
  
  return (uint16_t)isqrt32((uint32_t)variance);
}

uint16_t readVoltage() {
  uint16_t rmsADC = getTrueRMS(PIN_VOLTAGE_SENS);
  if (rmsADC < 5) return 0;

  uint16_t trueVoltage = (rmsADC * params.voltageMultiplier_x100) / 100;

  static uint16_t filteredV = 0;
  if (filteredV == 0) { filteredV = trueVoltage; return filteredV; }

  int16_t diff = (int16_t)trueVoltage - (int16_t)filteredV;
  if (diff < 0) diff = -diff;
  
  if (diff > 3) filteredV = trueVoltage;
  else          filteredV = (filteredV * 85 + trueVoltage * 15) / 100; // 85:15 integer filter

  if (filteredV < 30) filteredV = 0;
  return filteredV;
}

uint16_t readCurrent() {
  uint16_t rmsADC = getTrueRMS(PIN_CURRENT_SENS);
  
  // Current in mA: (rmsADC * multiplier_x100) / 10
  uint16_t trueCurrent_mA = (rmsADC * params.currentMultiplier_x100) / 10;

  static uint16_t filteredA = 0;
  if (trueCurrent_mA < 100) { filteredA = 0; return 0; }
  if (filteredA == 0)       { filteredA = trueCurrent_mA; return filteredA; }

  int16_t diff = (int16_t)trueCurrent_mA - (int16_t)filteredA;
  if (diff < 0) diff = -diff;
  
  if (diff > 100) filteredA = trueCurrent_mA;
  else            filteredA = (filteredA * 85 + trueCurrent_mA * 15) / 100;

  return filteredA;
}