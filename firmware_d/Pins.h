// ==========================================================
// PIN DEFINITIONS & HARDWARE MAPPING
// MCU: LGT8F328P (SSOP32 / LQFP32)
// ==========================================================

// --- Digital Inputs & Outputs ---
#define PIN_CALIBRATION    7   // (PD7 - Fisik 11) Tombol Kalibrasi. Active LOW (Gunakan INPUT_PULLUP di setup)
#define PIN_RELAY          9   // (PB1 - Fisik 13) Relay Kompresor. Active HIGH. (Ada R pull-down eksternal)
#define PIN_LED_STATUS     13  // (PB5 - Fisik 17) LED Status (Error / Normal)
#define PIN_BUZZER         A0  // (PC0 - Fisik 23) Buzzer Indikator (Gunakan sebagai Digital OUTPUT)

// --- TM1637 4-Digit 7-Segment Display ---
#define PIN_TM_DAT         A4  // (PC4 - Fisik 27) Pin Data (I2C SDA padanan)
#define PIN_TM_CLK         A5  // (PC5 - Fisik 28) Pin Clock (I2C SCL padanan)

// --- Analog Inputs (Sensor & Tombol) ---
// Catatan: LGT8F328P bisa menggunakan resolusi ADC 10-bit (0-1023) atau 12-bit (0-4095)
#define PIN_CURRENT_SENS   A6  // (PE1 - Fisik 19) ZMCT103 (Arus). Offset 2.5V, Butuh True RMS
#define PIN_VOLTAGE_SENS   A7  // (PE3 - Fisik 22) ZMPT101 (Tegangan). Offset 2.5V, Butuh True RMS

#define PIN_BUTTONS        A1  // (PC1 - Fisik 24) 3 Tombol via Resistor Divider (Pull-up 10k internal/eksternal)
                               // - Tombol 1 (Min): R 10k -> ~2.5V
                               // - Tombol 2 (Menu/OK): R 4.3k -> ~1.5V
                               // - Tombol 3 (Plus): R 1k -> ~0.45V

#define PIN_TEMP_DRYER     A2  // (PC2 - Fisik 25) Sensor Suhu Filter Dryer (NTC + Pull-up 10k)
#define PIN_TEMP_COMP      A3  // (PC3 - Fisik 26) Sensor Suhu Kompresor (NTC + Pull-up 10k)

// --- Serial UART (Sudah otomatis digunakan oleh Serial.begin) ---
// RX = 0 (PD0 - Fisik 30)
// TX = 1 (PD1 - Fisik 31)
