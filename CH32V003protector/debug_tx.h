// debug_tx.h - TX-only debug output on PC7 for the serial plotter (9600 baud, 8N1)
//
// PC7 is not a hardware UART pin on the CH32V003 (USART1 TX can only be PD5, PD6,
// PD0 or PC0), so Serial.print() can never come out on PC7. This sends the bytes
// by switching the pin by hand instead. Transmit only: nothing is ever read.
//
// Wiring: PC7 -> USB-TTL adapter RX pin, board GND -> adapter GND.
// Do NOT connect the adapter's TX pin to the board.
//
// Usage in the sketch:
//   #include "debug_tx.h"                              (top of the .ino)
//   dbgBegin();                                        (in setup())
//   dbgPlot((long)teganganRMS, (long)(arusRMS * 1000.0f));   (in loop())
//
// Output looks like  V:220,mA:1200  -> open the Arduino Serial Plotter at 9600 baud.
//
// Each character blocks the program for about 1 ms, so dbgPlot() only prints
// every 200 ms and skips the call the rest of the time.

#ifndef DEBUG_TX_H
#define DEBUG_TX_H

#include <Arduino.h>

#define DBG_PIN PC7

// Time of one bit in microseconds. 9600 baud is 104 us, and digitalWrite()
// itself takes a little time, so 100 is a good start. If the text looks like
// garbage, try values between 96 and 104.
const unsigned int DBG_BIT_US = 100;

void dbgBegin() {
  pinMode(DBG_PIN, OUTPUT);
  digitalWrite(DBG_PIN, HIGH);   // line idles high
}

void dbgByte(uint8_t value) {
  digitalWrite(DBG_PIN, LOW);    // start bit
  delayMicroseconds(DBG_BIT_US);

  for (uint8_t bitNumber = 0; bitNumber < 8; bitNumber++) {
    // least significant bit goes first
    if (value % 2 == 1) {
      digitalWrite(DBG_PIN, HIGH);
    } else {
      digitalWrite(DBG_PIN, LOW);
    }
    value = value / 2;
    delayMicroseconds(DBG_BIT_US);
  }

  digitalWrite(DBG_PIN, HIGH);   // stop bit
  delayMicroseconds(DBG_BIT_US);
}

// Prints a piece of text, for example dbgText("V:")
void dbgText(const char* text) {
  while (*text != 0) {
    dbgByte(*text);
    text++;
  }
}

// Prints a whole number, for example 220 or -15 (no decimals, to save flash).
void dbgPrint(long number) {
  char digits[11];
  uint8_t count = 0;

  if (number < 0) {
    dbgByte('-');
    number = -number;
  }
  if (number == 0) {
    dbgByte('0');
  }
  while (number > 0) {
    digits[count] = '0' + (number % 10);
    count++;
    number = number / 10;
  }
  while (count > 0) {
    count--;
    dbgByte(digits[count]);
  }
}

// One plotter line "V:220,mA:1200", at most once every 200 ms.
void dbgPlot(long volts, long milliAmps) {
  static unsigned long lastPlot = 0;

  if (millis() - lastPlot < 200) {
    return;
  }
  lastPlot = millis();

  dbgText("V:");
  dbgPrint(volts);
  dbgText(",mA:");
  dbgPrint(milliAmps);
  dbgByte('\n');
}

#endif
