#include "sensor_manager.h"

static SensorData g_sensorData;
static float g_voltCal = DEFAULT_VOLT_CAL;
static float g_currentCal = DEFAULT_CURRENT_CAL;

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

void sensorInit() {
  pinMode(PIN_VOLTAGE, INPUT);
  pinMode(PIN_CURRENT, INPUT);
  pinMode(PIN_KEY, INPUT);

  g_sensorData.voltageFastRMS = 220;
  g_sensorData.voltageDisplayRMS = 220;
  g_sensorData.voltagePeakToPeak = 135;
  g_sensorData.currentMilliAmps = 0;
  g_sensorData.currentVariance = 0;

  g_sensorData.ntc1ShellTempC = NTC_TEMP_UNAVAILABLE;
  g_sensorData.ntc1Status = SENSOR_STATUS_UNAVAILABLE_NO_ADC;
  g_sensorData.ntc2LiquidLineTempC = NTC_TEMP_UNAVAILABLE;
  g_sensorData.ntc2Status = SENSOR_STATUS_UNAVAILABLE_NO_ADC;

  g_sensorData.keyRawADC = 1023;
  g_sensorData.voltageValid = false;
  g_sensorData.currentValid = true;
}

void sensorUpdate() {
  static unsigned long lastCycleStartUs = 0;
  static uint16_t minV = 1023;
  static uint16_t maxV = 0;
  static uint32_t sumI = 0;
  static uint32_t sumISq = 0;
  static uint16_t sampleCount = 0;

  unsigned long nowUs = micros();

  // Sample every ~250 microseconds
  static unsigned long lastSampleUs = 0;
  if ((nowUs - lastSampleUs) >= 250) {
    lastSampleUs = nowUs;

    uint16_t vAdc = analogRead(PIN_VOLTAGE);
    uint16_t iAdc = analogRead(PIN_CURRENT);

    if (vAdc < minV) minV = vAdc;
    if (vAdc > maxV) maxV = vAdc;

    sumI += iAdc;
    sumISq += ((uint32_t)iAdc * (uint32_t)iAdc);
    sampleCount++;
  }

  // 20 ms window (~1 complete 50 Hz AC cycle)
  if ((nowUs - lastCycleStartUs) >= 20000UL) {
    lastCycleStartUs = nowUs;

    if (sampleCount > 20) {
      // 1. Process Voltage
      uint16_t vpp = (maxV >= minV) ? (maxV - minV) : 0;
      g_sensorData.voltagePeakToPeak = vpp;
      uint16_t fastV = (uint16_t)(((float)vpp * g_voltCal) + 0.5f);
      g_sensorData.voltageFastRMS = fastV;

      // Filtered display voltage: (old * 7 + new) / 8
      g_sensorData.voltageDisplayRMS = (uint16_t)(((uint32_t)g_sensorData.voltageDisplayRMS * 7 + fastV + 4) >> 3);
      g_sensorData.voltageValid = (fastV >= 50 && fastV <= 320);

      // 2. Process Current
      uint32_t meanI = sumI / sampleCount;
      uint32_t meanISq = sumISq / sampleCount;
      uint32_t variance = 0;
      uint32_t meanSq = meanI * meanI;
      if (meanISq > meanSq) {
        variance = meanISq - meanSq;
      }
      g_sensorData.currentVariance = (uint16_t)variance;

      uint32_t rmsCounts = isqrt32(variance);
      // Current in mA: rmsCounts * (g_currentCal * 1000)
      uint16_t i_mA = (uint16_t)(((float)rmsCounts * (g_currentCal * 1000.0f)) + 0.5f);
      if (i_mA < 50) { // Below 50 mA treated as noise floor
        i_mA = 0;
      }
      g_sensorData.currentMilliAmps = i_mA;
      g_sensorData.currentValid = (i_mA <= 20000);
    }

    minV = 1023;
    maxV = 0;
    sumI = 0;
    sumISq = 0;
    sampleCount = 0;

    // 3. Sample Button Ladder (PD6)
    g_sensorData.keyRawADC = analogRead(PIN_KEY);
  }
}

const SensorData& sensorGetData() {
  return g_sensorData;
}

void sensorSetVoltageCal(float cal) {
  if (cal > 0.5f && cal < 5.0f) {
    g_voltCal = cal;
  }
}

void sensorSetCurrentCal(float cal) {
  if (cal > 0.001f && cal < 0.1f) {
    g_currentCal = cal;
  }
}

float sensorGetVoltageCal() {
  return g_voltCal;
}

float sensorGetCurrentCal() {
  return g_currentCal;
}
