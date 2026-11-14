#ifndef SENSORS_H
#define SENSORS_H

#include "sys_time.h"
#include "params.h"

void     sensors_init(void);
uint16_t sensors_read_adc(uint8_t pin);
uint16_t sensors_read_voltage_dv(void); // High-res decivolts (2205 = 220.5 V)
uint16_t sensors_read_current_ma(void); // High-res milliamps (1 mA resolution)
int16_t  sensors_read_temp1(void);
int16_t  sensors_read_temp2(void);

#endif