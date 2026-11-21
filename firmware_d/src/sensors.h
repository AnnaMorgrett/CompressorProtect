#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

void     sensors_init(void);
uint16_t sensors_read_adc(uint8_t pin);
uint16_t sensors_read_voltage_dv(void); // Decivolts (0.1 V)
uint16_t sensors_read_current_ma(void); // Milliamps (1 mA)
int16_t  sensors_read_temp1(void);
int16_t  sensors_read_temp2(void);

#endif
