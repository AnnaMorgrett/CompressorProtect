#ifndef BOARD_H
#define BOARD_H

#include "ch32fun.h"
#include <stdint.h>
#include <stdbool.h>

// --- Digital Inputs & Outputs ---
#define PIN_RELAY PD4      // Pin 8  (SOP-16) - Compressor relay (Active HIGH)
#define PIN_LED_STATUS PD5 // Pin 9  (SOP-16) - Status LED (Active HIGH)
#define PIN_BUZZER PC0     // Pin 16 (SOP-16) - Buzzer (Active HIGH)

// --- TM1637 4-Digit 7-Segment Display ---
#define PIN_TM_DAT PC3 // Pin 3  (SOP-16) - Data
#define PIN_TM_CLK PC4 // Pin 4  (SOP-16) - Clock

// --- Analog Inputs (10-bit ADC, Vref = VDD) ---
#define PIN_CURRENT_SENS PA1 // AIN1 - Pin 12 (SOP-16) - ZMCT103 current
#define PIN_VOLTAGE_SENS PA2 // AIN0 - Pin 13 (SOP-16) - ZMPT101 voltage
#define PIN_BUTTONS PD6      // AIN6 - Pin 10 (SOP-16) - 3-button resistor ladder

// --- NTC Temperature Sensors (no ADC mux on PC1/PC2) ---
#define PIN_TEMP_DRYER PC1 // Pin 1  (SOP-16) - NTC2
#define PIN_TEMP_COMP PC2  // Pin 2  (SOP-16) - NTC1

// --- Header H1 / Calibration / Debug UART ---
#define PIN_DEBUG_TX PC7    // Pin 6  (SOP-16) - TX on header H1 pin 2
#define PIN_DEBUG_RX PC6    // Pin 5  (SOP-16) - RX on header H1 pin 3
#define PIN_CALIBRATION PC6 // Pin 5  (SOP-16) - CAL jumper at boot, Active LOW
#define PIN_SWIO_DEBUG PD1  // Pin 7  (SOP-16) - SWIO single-wire debug / flashing

// --- Hardware Feature Switches ---
#define ENABLE_SERIAL_DEBUG 1
#define BUZZER_PASSIVE 0
#define BUZZER_FREQ_HZ 2700

#endif