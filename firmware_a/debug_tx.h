// debug_tx.h - TX-only debug log on PC7 (software serial, 19200 baud, 8N1)
//
// PC7 is not a hardware UART pin on the CH32V003 (USART1 TX can only be PD5, PD6,
// PD0 or PC0), so Serial.print() can never come out on PC7. This file sends the
// bytes by switching the pin by hand. Transmit only: nothing is ever read.
//
// Wiring: PC7 -> USB-TTL adapter RX pin, board GND -> adapter GND.
// Do NOT connect the adapter's TX pin to the board.
//
// How it works: text is first put into a small queue in RAM (nothing waits).
// dbgService() then sends ONE character from the queue each time it is called,
// so the main loop is slowed by only about half a millisecond per pass while a
// message is being sent, and not at all when the queue is empty.

#ifndef DEBUG_TX_H
#define DEBUG_TX_H

#include <Arduino.h>
#include "pins.h"

#define DBG_PIN PIN_DEBUG_TX
#define DBG_BUF_SIZE 256

// Time of one bit in microseconds. 19200 baud is 52 us, and digitalWrite()
// itself takes a little time, so 50 is a good start. If the text looks like
// garbage in the monitor, try values between 46 and 52.
const unsigned int DBG_BIT_US = 50;

static char dbgBuf[DBG_BUF_SIZE];
static uint16_t dbgHead = 0;          // where the next character is stored
static uint16_t dbgTail = 0;          // next character to be sent
static bool dbgPlotStyle = false;     // true = "V:220,mA:1200", false = "V=220.3 A=1.200"
static bool dbgFirstValue = true;     // no separator before the first value of a line
static unsigned long dbgLastReport = 0;
static unsigned long dbgDropped = 0;  // characters lost because the queue was full

void dbgBegin() {
  pinMode(DBG_PIN, OUTPUT);
  digitalWrite(DBG_PIN, HIGH);        // line idles high
}

// Sends one byte right now (about 0.5 ms at 19200 baud).
static void dbgSendNow(uint8_t value) {
  digitalWrite(DBG_PIN, LOW);         // start bit
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

  digitalWrite(DBG_PIN, HIGH);        // stop bit
  delayMicroseconds(DBG_BIT_US);
}

// Call once per loop(). Sends at most one queued character.
void dbgService() {
  if (dbgTail == dbgHead) {
    return;
  }
  dbgSendNow((uint8_t)dbgBuf[dbgTail]);
  dbgTail = (dbgTail + 1) % DBG_BUF_SIZE;
}

// Puts one character in the queue. If the queue is full the character is
// dropped and counted, never waited for.
static void dbgPut(char c) {
  uint16_t next = (dbgHead + 1) % DBG_BUF_SIZE;
  if (next == dbgTail) {
    dbgDropped++;
    return;
  }
  dbgBuf[dbgHead] = c;
  dbgHead = next;
}

static uint16_t dbgFree() {
  return (dbgTail + DBG_BUF_SIZE - dbgHead - 1) % DBG_BUF_SIZE;
}

static void dbgText(const char* text) {
  while (*text != 0) {
    dbgPut(*text);
    text++;
  }
}

// Whole number, for example 220 or -15.
static void dbgNumber(long number) {
  char digits[11];
  uint8_t count = 0;

  if (number < 0) {
    dbgPut('-');
    number = -number;
  }
  if (number == 0) {
    dbgPut('0');
  }
  while (number > 0) {
    digits[count] = '0' + (number % 10);
    count++;
    number = number / 10;
  }
  while (count > 0) {
    count--;
    dbgPut(digits[count]);
  }
}

// A number that was multiplied by 10 or 1000 beforehand, printed with decimals.
// dbgFixed(2203, 1) prints 220.3 and dbgFixed(1005, 3) prints 1.005
static void dbgFixed(long scaled, uint8_t decimals) {
  long divisor = 1;
  for (uint8_t i = 0; i < decimals; i++) {
    divisor = divisor * 10;
  }
  if (scaled < 0) {
    dbgPut('-');
    scaled = -scaled;
  }
  dbgNumber(scaled / divisor);
  dbgPut('.');

  long fraction = scaled % divisor;
  long check = divisor / 10;
  while (check > 1 && fraction < check) {   // leading zeros of the decimals
    dbgPut('0');
    check = check / 10;
  }
  dbgNumber(fraction);
}

void dbgSetPlot(bool plotStyle) {
  dbgPlotStyle = plotStyle;
}

// "[123.456] " time stamp: seconds since power-up.
static void dbgTimeStamp() {
  dbgPut('[');
  dbgFixed((long)millis(), 3);
  dbgText("] ");
}

// Start a new report line. Returns true only when it is time for one AND the
// previous message has been sent completely. Then add values and call dbgEnd().
bool dbgStart(unsigned long everyMs) {
  if (millis() - dbgLastReport < everyMs) {
    return false;
  }
  if (dbgTail != dbgHead) {
    return false;
  }
  dbgLastReport = millis();
  dbgFirstValue = true;
  if (!dbgPlotStyle) {
    dbgTimeStamp();
  }
  return true;
}

static void dbgName(const char* name) {
  if (dbgFirstValue) {
    dbgFirstValue = false;
  } else if (dbgPlotStyle) {
    dbgPut(',');
  } else {
    dbgPut(' ');
  }
  dbgText(name);
  if (dbgPlotStyle) {
    dbgPut(':');
  } else {
    dbgPut('=');
  }
}

void dbgVal(const char* name, long value) {          // name=123
  dbgName(name);
  dbgNumber(value);
}

void dbgVal1(const char* name, long tenths) {        // name=220.3  (give 2203)
  dbgName(name);
  dbgFixed(tenths, 1);
}

void dbgVal3(const char* name, long thousandths) {   // name=1.205  (give 1205)
  dbgName(name);
  dbgFixed(thousandths, 3);
}

void dbgEnd() {
  dbgPut('\r');
  dbgPut('\n');
}

// One-off message such as  [12.345] *** STATE 1 -> 3
// Not printed in plotter style, because text would break the plot.
void dbgEvent(const char* text, long from, long to) {
  if (dbgPlotStyle || dbgFree() < 60) {
    return;
  }
  dbgTimeStamp();
  dbgText("*** ");
  dbgText(text);
  dbgPut(' ');
  dbgNumber(from);
  dbgText(" -> ");
  dbgNumber(to);
  dbgEnd();
}

#endif
