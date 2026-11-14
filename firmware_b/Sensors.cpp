#include "Sensors.h"

void initSensors() {
  pinMode(PIN_VOLTAGE_SENS, INPUT);
  pinMode(PIN_CURRENT_SENS, INPUT);
}

// Integer square root (replaces sqrt(), keeps float math out of the flash)
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

// True RMS over exactly 40 ms (2 cycles of 50 Hz), same method as the LGT version
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

  int32_t  mean     = sum / (int32_t)count;
  uint32_t meanSq   = sumSq / count;
  int32_t  variance = (int32_t)meanSq - (mean * mean);
  if (variance < 0) variance = 0;

  return (uint16_t)isqrt32((uint32_t)variance);
}

uint16_t readVoltage() {
  uint16_t rmsADC = getTrueRMS(PIN_VOLTAGE_SENS);

  // multiplier is x1000 (3420 = 3.420 V per ADC count)
  uint16_t trueVoltage = ((uint32_t)rmsADC * params.voltageMultiplier_x1000) / 1000;

  // Hard noise gate (unplugged / blackout -> 0 immediately)
  if (trueVoltage < 5) return 0;

  static uint16_t filteredV = 0;
  if (filteredV == 0) { filteredV = trueVoltage; return filteredV; }

  int16_t diff = (int16_t)trueVoltage - (int16_t)filteredV;
  if (diff < 0) diff = -diff;

  // Big jump (> 3 V): follow instantly. Small fluctuation: 85:15 smoothing
  if (diff > 3) filteredV = trueVoltage;
  else          filteredV = (filteredV * 85 + trueVoltage * 15) / 100;

  if (filteredV < 30) filteredV = 0;
  return filteredV;
}

uint16_t readCurrent() {
  uint16_t rmsADC = getTrueRMS(PIN_CURRENT_SENS);

  // multiplier is x1000 (1880 = 18.8 mA per ADC count): mA = count * mult / 100
  uint16_t trueCurrent_mA = ((uint32_t)rmsADC * params.currentMultiplier_x1000) / 100;

  static uint16_t filteredA = 0;
  if (trueCurrent_mA < 100) { filteredA = 0; return 0; }          // noise gate 0.10 A
  if (filteredA == 0)       { filteredA = trueCurrent_mA; return filteredA; }

  int16_t diff = (int16_t)trueCurrent_mA - (int16_t)filteredA;
  if (diff < 0) diff = -diff;

  // Jump > 100 mA: follow instantly. Otherwise 85:15 smoothing
  if (diff > 100) filteredA = trueCurrent_mA;
  else            filteredA = (filteredA * 85 + trueCurrent_mA * 15) / 100;

  return filteredA;
}

// NTC: PC1 / PC2 have no ADC on the CH32V003 -> not readable yet.
// Replace these two functions when the hardware is changed.
int16_t readTemp1() { return 0; }
int16_t readTemp2() { return 0; }
