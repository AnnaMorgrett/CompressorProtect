#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "Pins.h"
#include "Params.h"

void initSensors();
uint16_t readVoltage();   // Volts[cite: 7]
uint16_t readCurrent();   // mA[cite: 7]
int16_t  readTemp1();     // deg C, NTC1 compressor (not available yet -> 0)[cite: 7]
int16_t  readTemp2();     // deg C, NTC2 dryer      (not available yet -> 0)[cite: 7]

#endif