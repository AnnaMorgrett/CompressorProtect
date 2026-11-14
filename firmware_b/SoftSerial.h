#ifndef SOFTSERIAL_H
#define SOFTSERIAL_H

#include <Arduino.h>

// CH32V003 PC7 = bit 7, PC6 = bit 6 on GPIOC
#define DBG_TX_MASK (1U << 7)
#define DBG_RX_MASK (1U << 6)

class SoftSerial {
private:
  uint8_t  _txPin;
  uint8_t  _rxPin;
  bool     _hasRx;
  uint16_t _bitUs;
  uint8_t  _rxBuf[24];
  uint8_t  _rxHead;
  uint8_t  _rxTail;

public:
  SoftSerial(uint8_t txPin, uint8_t rxPin = 255)
    : _txPin(txPin), _rxPin(rxPin), _hasRx(false), _bitUs(102), _rxHead(0), _rxTail(0) {}

  void begin(uint32_t baud) {
    pinMode(_txPin, OUTPUT);
    GPIOC->BSHR = DBG_TX_MASK; // Idle HIGH
    _bitUs = (baud == 9600) ? 102 : (uint16_t)(1000000UL / baud - 2);
  }

  void enableRx(uint8_t rxPin) {
    _rxPin = rxPin;
    _hasRx = true;
    pinMode(_rxPin, INPUT_PULLUP);
  }

  bool rxStartDetected() const {
    return _hasRx && ((GPIOC->INDR & DBG_RX_MASK) == 0);
  }

  void pollRx() {
    if (!_hasRx) return;
    while ((GPIOC->INDR & DBG_RX_MASK) == 0) {
      delayMicroseconds(_bitUs / 2);
      if ((GPIOC->INDR & DBG_RX_MASK) != 0) break;

      uint8_t val = 0;
      for (uint8_t i = 0; i < 8; i++) {
        delayMicroseconds(_bitUs);
        if (GPIOC->INDR & DBG_RX_MASK) val |= (1 << i);
      }

      delayMicroseconds(_bitUs);
      if (GPIOC->INDR & DBG_RX_MASK) {
        uint8_t nextHead = (_rxHead + 1) % sizeof(_rxBuf);
        if (nextHead != _rxTail) {
          _rxBuf[_rxHead] = val;
          _rxHead = nextHead;
        }
      }

      unsigned long waitEnd = micros() + (_bitUs + (_bitUs / 2));
      while ((long)(micros() - waitEnd) < 0) {
        if ((GPIOC->INDR & DBG_RX_MASK) == 0) break;
      }
    }
  }

  int available() {
    pollRx();
    return (_rxHead >= _rxTail) ? (_rxHead - _rxTail) : (sizeof(_rxBuf) - _rxTail + _rxHead);
  }

  int read() {
    if (_rxHead == _rxTail) return -1;
    uint8_t c = _rxBuf[_rxTail];
    _rxTail = (_rxTail + 1) % sizeof(_rxBuf);
    return c;
  }

  void writeByte(uint8_t b) {
    GPIOC->BCR = DBG_TX_MASK; // Start bit LOW
    delayMicroseconds(_bitUs);
    for (uint8_t i = 0; i < 8; i++) {
      if (b & 0x01) GPIOC->BSHR = DBG_TX_MASK;
      else          GPIOC->BCR  = DBG_TX_MASK;
      b >>= 1;
      delayMicroseconds(_bitUs);
    }
    GPIOC->BSHR = DBG_TX_MASK; // Stop bit HIGH
    delayMicroseconds(_bitUs);
  }

  void print(char c) { writeByte((uint8_t)c); }

  void print(const char *s) {
    while (*s) writeByte((uint8_t)*s++);
  }

  void print(int32_t n) {
    if (n < 0) { writeByte('-'); n = -n; }
    char buf[12];
    uint8_t i = 0;
    do {
      buf[i++] = '0' + (n % 10);
      n /= 10;
    } while (n > 0);
    while (i > 0) writeByte(buf[--i]);
  }

  void print(int n)      { print((int32_t)n); }
  void print(uint16_t n) { print((int32_t)n); }
  void print(uint8_t n)  { print((int32_t)n); }

  void println()              { print("\r\n"); }
  void println(const char *s) { print(s); println(); }
  void println(int32_t n)     { print(n); println(); }
  void println(int n)         { print((int32_t)n); println(); }
  void println(uint16_t n)    { print((int32_t)n); println(); }
  void println(uint8_t n)     { print((int32_t)n); println(); }
};

#endif