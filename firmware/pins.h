#ifndef PINS_H
#define PINS_H

#include <Arduino.h>

/**
 * ============================================================================
 * COMPRESSOR PROTECT - HARDWARE PIN DEFINITIONS
 * Target MCU : CH32V003A4M6 (SOP-16 Package) / CH32V003F4P6 (TSSOP-20 Compatible)
 * Schematic  : archive_old/CH32Version.png (Source of Truth)
 * ============================================================================
 * 
 * Pin Mapping Summary:
 * ----------------------------------------------------------------------------
 * Net Name | Port | SOP16 Pin | TSSOP20 Pin | Function / Peripherals
 * ---------+------+-----------+-------------+---------------------------------
 * BUZZ     | PC1  | Pin 1     | Pin 5       | Buzzer Driver (S8050 NPN, Active HIGH)
 * NTC2     | PC2  | Pin 2     | Pin 6       | Liquid-Line Thermistor (NO ADC - Digital Only!)
 * NTC1     | PC3  | Pin 3     | Pin 9       | Shell Thermistor (NO ADC - Digital Only!)
 * DIO      | PC4  | Pin 4     | Pin 10      | TM1637 Display Bi-Directional Data
 * CLK      | PC6  | Pin 5     | Pin 12      | TM1637 Display Clock
 * CAL      | PC7  | Pin 6     | Pin 13      | Calibration Header H1 Pin 3 (Optional Debug TX)
 * TX / SWIO| PD1  | Pin 7     | Pin 15      | SWIO Single-Wire Programming / Header H1
 * RELAY    | PD4  | Pin 8     | Pin 1       | Compressor Relay Driver (S8050 NPN, Active HIGH)
 * LED      | PD5  | Pin 9     | Pin 2       | Status Yellow LED (Active HIGH)
 * KEY      | PD6  | Pin 10    | Pin 3       | 3-Button Resistor Ladder (ADC Channel AIN6)
 * NC / RST | PD7  | Pin 11    | Pin 4       | Unconnected / Reset
 * CT       | PA1  | Pin 12    | Pin 18      | AC Current Transformer (ADC Channel AIN1)
 * VAC      | PA2  | Pin 13    | Pin 19      | AC Mains Voltage Sensor (ADC Channel AIN0)
 * VSS      | GND  | Pin 14    | Pin 7/20    | Ground (0V)
 * VDD      | +5V  | Pin 15    | Pin 8       | Regulated +5.0V Logic Supply
 * PROG     | PC0  | Pin 16    | Pin 4       | Programming Header H1 Pin 2
 * ----------------------------------------------------------------------------
 * 
 * CRITICAL HARDWARE RESOLUTIONS:
 * 1. RELAY vs KEY Conflict:
 *    - In archive_old code, PIN_RELAY was mistakenly assigned to PD6.
 *    - Per schematic CH32Version.png, RELAY is connected to PD4 (Pin 8).
 *    - KEY resistor ladder is connected to PD6 (Pin 10, AIN6).
 *    - Confirmed: PIN_RELAY = PD4, PIN_KEY = PD6.
 * 
 * 2. LED Pin:
 *    - Confirmed on schematic: PD5 (Pin 9).
 *    - Hardware UART1 TX also multiplexes on PD5. To prevent conflict with the
 *      status LED, hardware Serial must remain disabled during normal execution.
 * 
 * 3. Buzzer Pin:
 *    - Confirmed on schematic: PC1 (Pin 1). Drives S8050 base via 2.2k resistor.
 * 
 * 4. TM1637 Display Pins:
 *    - TM1637 DIO -> PC4 (Pin 4).
 *    - TM1637 CLK -> PC6 (Pin 5).
 * 
 * 5. NTC1 and NTC2 Analog Limitations:
 *    - PC2 (Pin 2) and PC3 (Pin 3) have NO internal ADC mux connection on CH32V003
 *      silicon. Only PA2 (AIN0), PA1 (AIN1), PC4 (AIN2), PD2 (AIN3), PD3 (AIN4),
 *      PD5 (AIN5), PD6 (AIN6), PD4 (AIN7) possess analog input capabilities.
 *    - As a result, analog thermistor measurements on PC2/PC3 are physically impossible
 *      without a circuit board revision. These are marked HARDWARE_UNAVAILABLE.
 */

// Analog Measurement Inputs
#define PIN_VOLTAGE         PA2     // ADC AIN0 (Pin 13 on SOP-16) - ZMPT101B AC Voltage
#define PIN_CURRENT         PA1     // ADC AIN1 (Pin 12 on SOP-16) - ZHT103 AC Current
#define PIN_KEY             PD6     // ADC AIN6 (Pin 10 on SOP-16) - 3-Button Resistor Ladder

// Digital Actuator Outputs
#define PIN_RELAY           PD4     // Output (Pin 8 on SOP-16)  - Compressor Relay (Active HIGH)
#define PIN_LED             PD5     // Output (Pin 9 on SOP-16)  - Status Yellow LED (Active HIGH)
#define PIN_BUZZER          PC1     // Output (Pin 1 on SOP-16)  - Alarm Buzzer (Active HIGH)

// TM1637 4-Digit 7-Segment Display Bus
#define PIN_TM_DIO          PC4     // I/O    (Pin 4 on SOP-16)  - Display Data
#define PIN_TM_CLK          PC6     // Output (Pin 5 on SOP-16)  - Display Clock

// Thermistor Interfaces (Marked as Digital-Only / No ADC on CH32V003 Silicon)
#define PIN_NTC1_RAW        PC3     // Pin 3 on SOP-16 (Compressor Shell - NO ADC MUX)
#define PIN_NTC2_RAW        PC2     // Pin 2 on SOP-16 (Liquid-Line - NO ADC MUX)

// Header / Calibration Pins
#define PIN_CAL_DEBUG       PC7     // Pin 6 on SOP-16 (Calibration header / Optional SW UART)
#define PIN_PROG_HEADER     PC0     // Pin 16 on SOP-16 (Programming header)
#define PIN_SWIO_DEBUG      PD1     // Pin 7 on SOP-16 (SWIO Single-wire debug / flashing)

#endif // PINS_H

