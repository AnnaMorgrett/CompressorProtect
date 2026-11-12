#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "Pins.h"
#include "Params.h"

// Deklarasi fungsi-fungsi sensor agar bisa dipanggil di file utama
void initSensors();
float readVoltage();
float readCurrent();
float readTemp1();
float readTemp2();

#endif
