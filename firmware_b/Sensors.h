#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "Pins.h"
#include "Params.h"

void initSensors();
uint16_t readVoltage();
uint16_t readCurrent();

#endif