#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "Pins.h"
#include "Params.h"

void initSensors();
float readVoltage();
float readCurrent();
float readTemp1();
float readTemp2();

#endif