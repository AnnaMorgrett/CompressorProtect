// ==========================================================
// PIN DEFINITIONS & HARDWARE MAPPING
// MCU: CH32V003 (SOP-16 / TSSOP-20)
// ==========================================================
#ifndef PINS_H
#define PINS_H

// --- Digital Inputs & Outputs ---
#define PIN_RELAY          PD4   // Pin 8  (SOP-16) - Compressor relay (Active HIGH)
#define PIN_LED_STATUS     PD5   // Pin 9  (SOP-16) - Status LED (Active HIGH)[cite: 7]
#define PIN_BUZZER         PC0   // Pin 16 (SOP-16) - Buzzer (Active HIGH)[cite: 7]

// --- TM1637 4-Digit 7-Segment Display ---
#define PIN_TM_DAT         PC3   // Pin 3  (SOP-16) - Data[cite: 7]
#define PIN_TM_CLK         PC4   // Pin 4  (SOP-16) - Clock[cite: 7]

// --- Analog Inputs (10-bit ADC, Vref = VDD) ---
#define PIN_CURRENT_SENS   PA1   // AIN1 - Pin 12 (SOP-16) - ZMCT103 current[cite: 7]
#define PIN_VOLTAGE_SENS   PA2   // AIN0 - Pin 13 (SOP-16) - ZMPT101 voltage[cite: 7]
#define PIN_BUTTONS        PD6   // AIN6 - Pin 10 (SOP-16) - 3-button resistor ladder[cite: 7]

// --- NTC Temperature Sensors ---
// HARDWARE LIMITATION: PC1 and PC2 have NO ADC mux on CH32V003.[cite: 7]
#define PIN_TEMP_DRYER     PC1   // Pin 1  (SOP-16) - NTC2 (Liquid-line) - no ADC[cite: 7]
#define PIN_TEMP_COMP      PC2   // Pin 2  (SOP-16) - NTC1 (Compressor shell) - no ADC[cite: 7]

// --- Header H1 / Calibration / UART ---
#define PIN_DEBUG_TX       PC7   // Pin 6  (SOP-16) - TX on header H1 pin 2 (to USB-TTL RXD)[cite: 7]
#define PIN_DEBUG_RX       PC6   // Pin 5  (SOP-16) - RX on header H1 pin 3 (to USB-TTL TXD)[cite: 7]
#define PIN_CALIBRATION    PC6   // Pin 5  (SOP-16) - CAL jumper at boot, INPUT_PULLUP, Active LOW[cite: 7]
#define PIN_SWIO_DEBUG     PD1   // Pin 7  (SOP-16) - SWIO single-wire debug / flashing (PROG)[cite: 7]

// --- Buzzer type ---
// 0 = ACTIVE buzzer (built-in oscillator, beeps on plain DC)[cite: 7]
// 1 = PASSIVE buzzer (needs a square wave, bit-banged at BUZZER_FREQ_HZ)[cite: 7]
#define BUZZER_PASSIVE     0
#define BUZZER_FREQ_HZ     2700

#endif