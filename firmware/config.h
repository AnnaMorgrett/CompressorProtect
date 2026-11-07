#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

/**
 * ============================================================================
 * COMPRESSOR PROTECT - SYSTEM CONFIGURATION & FACTORY DEFAULTS
 * Target: Refrigerator Compressor Protection Controller (CH32V003)
 * ============================================================================
 */

// Firmware Version Metadata
#define FW_VERSION_MAJOR            1
#define FW_VERSION_MINOR            1
#define FW_VERSION_PATCH            0

// Optional Debug Serial Output (Software UART on PC7)
// Set to 1 only for laboratory telemetry logging. Defaults to 0 to prevent PC7 conflicts.
#define ENABLE_DEBUG_LOG            0

// ============================================================================
// NON-VOLATILE STORAGE CONFIGURATION (Option-Bytes Emulation)
// ============================================================================
#define STORAGE_MAGIC_BYTE          0xC3
#define STORAGE_VERSION             0x01

// EEPROM Option Byte Memory Map (Total 26 bytes available)
#define EEPROM_ADDR_MAGIC           0   // Magic byte: 0xC3
#define EEPROM_ADDR_VERSION         1   // Config format version: 0x01
#define EEPROM_ADDR_OVP             2   // Over-voltage threshold (uint8_t: 230 - 280 V)
#define EEPROM_ADDR_UVP             3   // Under-voltage threshold (uint8_t: 150 - 210 V)
#define EEPROM_ADDR_OCP_RUN         4   // Run current threshold (x10 A, e.g. 12 = 1.2 A)
#define EEPROM_ADDR_OCP_LRA         5   // Stalled current threshold (x10 A, e.g. 16 = 1.6 A)
#define EEPROM_ADDR_DELAY_TIMER     6   // Startup delay (x10 sec, e.g. 30 = 300 sec)
#define EEPROM_ADDR_LAST_FAULT      7   // Last recorded fault code
#define EEPROM_ADDR_CHECKSUM        8   // XOR Checksum of bytes [0..7]

// ============================================================================
// AC MAINS VOLTAGE PROTECTION PARAMETERS
// ============================================================================
// Provisional factory defaults (ZMPT101B transformer)
#define DEFAULT_VOLT_CAL            1.635f  // Scaling constant: V_RMS = V_PeakToPeak * 1.635

#define DEFAULT_OVP_TRIP            260     // Over-voltage trip threshold (Volts AC)
#define DEFAULT_OVP_REC_HYST        10      // Recovery hysteresis (OVP Recovery = OVP_TRIP - 10V = 250V)

#define DEFAULT_UVP_TRIP            175     // Under-voltage trip threshold (Volts AC)
#define DEFAULT_UVP_REC_HYST        10      // Recovery hysteresis (UVP Recovery = UVP_TRIP + 10V = 185V)

#define VOLTAGE_SAG_CRITICAL        160.0f  // Critical brownout limit: immediate shutdown (0 ms delay)
#define VOLTAGE_FAULT_TIME_MS       5000UL  // Sustained abnormal voltage tolerance before tripping
#define VOLTAGE_QUALIFY_TIME_MS     3000UL  // Continuous healthy voltage required before leaving delay/recovery

// Valid Range Constraints for User Settings
#define CFG_OVP_MIN                 230
#define CFG_OVP_MAX                 280
#define CFG_OVP_STEP                5

#define CFG_UVP_MIN                 150
#define CFG_UVP_MAX                 210
#define CFG_UVP_STEP                5

// ============================================================================
// COMPRESSOR CURRENT PROTECTION PARAMETERS
// ============================================================================
// Provisional factory defaults (ZHT103 current transformer)
#define DEFAULT_CURRENT_CAL         0.01969f // Scaling constant: I_RMS = sqrt(variance) * 0.01969

#define DEFAULT_CURRENT_RUN_X10     12      // 1.2 A running overload trip
#define DEFAULT_CURRENT_LRA_X10     16      // 1.6 A stalled / locked rotor trip

#define CURRENT_LRA_TIME_MS         2000UL  // Max tolerated duration of locked rotor current (LRA)
#define CURRENT_OCP_RUN_DELAY_MS    4000UL  // Max tolerated duration of continuous running overload
#define STARTUP_INRUSH_WINDOW_MS    1500UL  // Initial motor start inrush blanking window
#define CURRENT_NOISE_FLOOR_AMPS    0.05f   // Below 50 mA treated as zero current

// Valid Range Constraints for User Settings
#define CFG_OCP_RUN_MIN_X10         5       // 0.5 A
#define CFG_OCP_RUN_MAX_X10         30      // 3.0 A
#define CFG_OCP_RUN_STEP_X10        1       // 0.1 A

#define CFG_OCP_LRA_MIN_X10         10      // 1.0 A
#define CFG_OCP_LRA_MAX_X10         50      // 5.0 A
#define CFG_OCP_LRA_STEP_X10        1       // 0.1 A

// Over-Current Retry and Lockout Accounting
#define OCP_MAX_RETRIES             3       // Maximum automatic restart attempts
#define OCP_RETRY_RESET_TIME_MS     600000UL // 10 minutes continuous run clears retry counter

// ============================================================================
// REFRIGERATION CYCLE & COMPRESSOR TIMERS
// ============================================================================
#define DEFAULT_DELAY_TIMER_X10     30      // 30 x 10 = 300 seconds (5 minutes) minimum OFF time
#define CFG_DELAY_MIN_X10           1       // 10 seconds (for testing / bench calibration)
#define CFG_DELAY_MAX_X10           60      // 600 seconds (10 minutes)
#define CFG_DELAY_STEP_X10          1       // 10 seconds

#define QUICK_BYPASS_HOLD_MS        3000UL  // Hold PLUS (+) button for 3s during countdown to bypass delay

// ============================================================================
// NTC THERMISTOR STATUS & CALIBRATION
// ============================================================================
// Hardware Reality Note:
// In schematic archive_old/CH32Version.png, NTC1 is routed to PC3 and NTC2 to PC2.
// Neither PC2 nor PC3 connects to the CH32V003 ADC multiplexer.
// Therefore, the analog thermistor subsystem is marked UNCOMMISSIONED / NO_ADC.
#define NTC_COMMISSIONED            0       // 0 = Hardware ADC unavailable, 1 = Active
#define NTC_TEMP_UNAVAILABLE        -999.0f // Sentinel for missing sensor reading

// Provisional constants (preserved for documentation / future hardware revisions)
#define NTC1_R25_OHMS               10000.0f
#define NTC1_BETA_K                 3950.0f
#define NTC1_PULLUP_OHMS            10000.0f

#define NTC2_R25_OHMS               10000.0f
#define NTC2_BETA_K                 3950.0f
#define NTC2_PULLUP_OHMS            10000.0f

// ============================================================================
// 3-BUTTON RESISTOR LADDER (PD6 / AIN6, 10-bit ADC, 5.0V Vref)
// ============================================================================
// Resistors: Pullup R24=10k, Min R19=10k, Menu R20=4.7k, Plus R21=2.2k
#define KEY_ADC_PLUS_MIN            100     // Nominal ~184
#define KEY_ADC_PLUS_MAX            240
#define KEY_ADC_MENU_MIN            240     // Nominal ~327
#define KEY_ADC_MENU_MAX            420
#define KEY_ADC_MIN_MIN             420     // Nominal ~512
#define KEY_ADC_MIN_MAX             650
#define KEY_ADC_IDLE_MIN            850     // Nominal ~1023 (Open circuit)

#define KEY_DEBOUNCE_MS             40UL    // Button debounce filter
#define KEY_LONG_PRESS_MS           1500UL  // Threshold for long press event
#define KEY_REPEAT_START_MS         500UL   // Delay before held repeat starts
#define KEY_REPEAT_INTERVAL_MS      150UL   // Continuous repeat speed

// ============================================================================
// ALARM & BUZZER INTERVALS
// ============================================================================
#define BUZZER_INT_ERROR_MS         1800000UL // 30 minutes periodic fault reminder
#define BUZZER_INT_LOCKOUT_MS       900000UL  // 15 minutes periodic lockout reminder

#endif // CONFIG_H

