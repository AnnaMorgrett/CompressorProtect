#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "Pins.h"
#include "Params.h"

void initSensors();
int readVoltage();
int readCurrent();
int readTemp1();
int readTemp2();

#endif