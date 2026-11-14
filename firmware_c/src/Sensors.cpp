#include "Sensors.h"

extern bool serialRxIncoming();

// CH32V003 10-bit ADC1 single-conversion reader for PA2 (AIN0), PA1 (AIN1), PD6 (AIN6)
int analogRead(pin_size_t pin)
{
  uint8_t ch = 0;
  if (pin == PA2)
    ch = 0; // AIN0
  else if (pin == PA1)
    ch = 1; // AIN1
  else if (pin == PD6)
    ch = 6; // AIN6
  else
    return 0;

  ADC1->RSQR3 = ch;
  ADC1->CTLR2 |= (1U << 0) | (1U << 22); // ADON + SWSTART
  while (!(ADC1->STATR & (1U << 1)))
    ; // Wait for EOC
  return (int)(ADC1->RDATAR & 0x03FF);
}

void initSensors()
{
  // Enable GPIOA, GPIOD, and ADC1 clocks
  RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOD | RCC_APB2Periph_ADC1;
  // Set ADC clock prescaler = PCLK2 / 6 (48 MHz / 6 = 8 MHz, max allowed is 14 MHz)
  RCC->CFGR0 = (RCC->CFGR0 & ~(0x1FU << 11)) | (0x02U << 11);

  // Configure PA1 (AIN1), PA2 (AIN0), PD6 (AIN6) as Analog Input (CNF=00, MODE=00)
  GPIOA->CFGLR &= ~((0x0FU << (1 * 4)) | (0x0FU << (2 * 4)));
  GPIOD->CFGLR &= ~(0x0FU << (6 * 4));

  // Reset & calibrate ADC1
  RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
  RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;

  ADC1->CTLR2 = (1U << 20) | (0x07U << 17) | (1U << 0);                         // EXTTRIG + SWSTART trigger + ADON
  ADC1->SAMPTR2 = (0x03U << (0 * 3)) | (0x03U << (1 * 3)) | (0x03U << (6 * 3)); // 15-cycle sample time

  ADC1->CTLR2 |= (1U << 3); // RSTCAL
  while (ADC1->CTLR2 & (1U << 3))
    ;
  ADC1->CTLR2 |= (1U << 2); // CAL
  while (ADC1->CTLR2 & (1U << 2))
    ;
}

uint32_t isqrt32(uint32_t n)
{
  uint32_t c = 0x00100000;
  uint32_t g = 0;
  while (c != 0)
  {
    if (n >= (g + c))
    {
      n -= (g + c);
      g = (g >> 1) + c;
    }
    else
    {
      g >>= 1;
    }
    c >>= 2;
  }
  return g;
}

uint16_t getTrueRMS(uint8_t pin)
{
  analogRead(pin);
  delayMicroseconds(100);
  analogRead(pin);
  delayMicroseconds(50);

  unsigned long startMicros = micros();
  int32_t offset = analogRead(pin);
  int32_t sum = 0;
  uint32_t sumSq = 0;
  uint32_t count = 0;

  while (micros() - startMicros < 40000UL)
  {
    if (serialRxIncoming())
      return 0;
    int32_t val = (int32_t)analogRead(pin) - offset;
    sum += val;
    sumSq += (uint32_t)(val * val);
    count++;
  }

  if (count == 0)
    return 0;

  int32_t mean = sum / (int32_t)count;
  uint32_t meanSq = sumSq / count;
  int32_t variance = (int32_t)meanSq - (mean * mean);
  if (variance < 0)
    variance = 0;

  return (uint16_t)isqrt32((uint32_t)variance);
}

uint16_t readVoltage()
{
  uint16_t rmsADC = getTrueRMS(PIN_VOLTAGE_SENS);
  uint16_t trueVoltage = ((uint32_t)rmsADC * params.voltageMultiplier_x1000) / 1000;

  if (trueVoltage < 5)
    return 0;

  static uint16_t filteredV = 0;
  if (filteredV == 0)
  {
    filteredV = trueVoltage;
    return filteredV;
  }

  int16_t diff = (int16_t)trueVoltage - (int16_t)filteredV;
  if (diff < 0)
    diff = -diff;

  if (diff > 3)
    filteredV = trueVoltage;
  else
    filteredV = (filteredV * 85 + trueVoltage * 15) / 100;

  if (filteredV < 30)
    filteredV = 0;
  return filteredV;
}

uint16_t readCurrent()
{
  uint16_t rmsADC = getTrueRMS(PIN_CURRENT_SENS);
  uint16_t trueCurrent_mA = ((uint32_t)rmsADC * params.currentMultiplier_x1000) / 100;

  static uint16_t filteredA = 0;
  if (trueCurrent_mA < 100)
  {
    filteredA = 0;
    return 0;
  }
  if (filteredA == 0)
  {
    filteredA = trueCurrent_mA;
    return filteredA;
  }

  int16_t diff = (int16_t)trueCurrent_mA - (int16_t)filteredA;
  if (diff < 0)
    diff = -diff;

  if (diff > 100)
    filteredA = trueCurrent_mA;
  else
    filteredA = (filteredA * 85 + trueCurrent_mA * 15) / 100;

  return filteredA;
}

int16_t readTemp1() { return 0; }
int16_t readTemp2() { return 0; }