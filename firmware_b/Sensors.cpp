#include "Sensors.h"

// Algoritma Integer Square Root Pengganti <math.h>
uint32_t isqrt32(uint32_t n) {
  uint32_t root = 0;
  uint32_t bit = 1UL << 30;
  while (bit > n) bit >>= 2;
  while (bit != 0) {
    if (n >= root + bit) {
      n -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return root;
}

void initSensors() {
  analogReadResolution(12);
  pinMode(PIN_VOLTAGE_SENS, INPUT);
  pinMode(PIN_CURRENT_SENS, INPUT);
  pinMode(PIN_TEMP_COMP, INPUT);
  pinMode(PIN_TEMP_DRYER, INPUT);
}

int getTrueRMS(uint8_t pin) {
  analogRead(pin);
  delayMicroseconds(100);
  analogRead(pin);
  delayMicroseconds(50);

  unsigned long startMicros = micros(); 
  int32_t offset = analogRead(pin); 
  int32_t sum = 0;
  uint32_t sumSq = 0; // OPTIMASI: Turun dari uint64_t ke uint32_t untuk hemat Flash
  uint32_t count = 0;

  while (micros() - startMicros < 40000UL) {
    // Downscale /2 sebelum di-kuadratkan untuk mencegah limit uint32_t overflow (max 4.29M)
    int32_t val = (analogRead(pin) - offset) >> 1; 
    sum += val;
    sumSq += (uint32_t)(val * val);
    count++;
  }

  if (count == 0) return 0;

  int32_t mean = sum / (int32_t)count;
  uint32_t meanSq = sumSq / count;
  int32_t variance = meanSq - (mean * mean);
  
  if (variance < 0) variance = 0; 

  return isqrt32(variance) << 1; // Restore scale *2 ke ukuran normal
}

int readVoltage() {
  int rmsADC = getTrueRMS(PIN_VOLTAGE_SENS);
  if (rmsADC < 5) return 0;

  int trueVoltage = (rmsADC * params.voltageMultiplier) / 1000; 
  static int filteredV = 0;
  
  if (filteredV == 0) {
    filteredV = trueVoltage;
    return filteredV;
  }
  
  int diff = abs(trueVoltage - filteredV);
  if (diff > 3) filteredV = trueVoltage;
  else filteredV = ((filteredV * 85) + (trueVoltage * 15)) / 100;

  if (filteredV < 30) filteredV = 0;
  return filteredV;
}

int readCurrent() {
  int rmsADC = getTrueRMS(PIN_CURRENT_SENS);
  int trueCurrent_mA = (rmsADC * params.currentMultiplier) / 100; 
  static int filteredA_mA = 0;

  if (trueCurrent_mA < 100) { 
    filteredA_mA = 0;
    return 0;
  }
  if (filteredA_mA == 0) {
    filteredA_mA = trueCurrent_mA;
    return filteredA_mA;
  }
  
  int diff = abs(trueCurrent_mA - filteredA_mA);
  if (diff > 100) filteredA_mA = trueCurrent_mA;
  else filteredA_mA = ((filteredA_mA * 85) + (trueCurrent_mA * 15)) / 100;
  
  return filteredA_mA;
}

// Lookup Table NTC 3950K
const int8_t ntcTable[32] = {
  120, 110, 100, 92, 85, 79, 74, 69, 65, 61, 57, 54, 51, 48, 45, 42, 
  39, 36, 33, 30, 27, 24, 21, 18, 14, 11, 7, 3, -1, -7, -15, -30
};

int getTempNTC(uint8_t pin) {
  int adcVal = analogRead(pin);
  if (adcVal <= 0) return 120;
  if (adcVal >= 4095) return -30;
  
  uint8_t index = adcVal / 128; 
  if(index > 31) index = 31;
  return ntcTable[index];
}

int readTemp1() {
  if (!params.ntc1Enable) return 0; 
  return getTempNTC(PIN_TEMP_COMP) + params.tempOffset;
}

int readTemp2() {
  if (!params.ntc2Enable) return 0;
  return getTempNTC(PIN_TEMP_DRYER) + params.tempOffset;
}