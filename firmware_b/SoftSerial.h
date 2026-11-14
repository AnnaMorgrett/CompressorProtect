#ifndef SOFTSERIAL_H
#define SOFTSERIAL_H

#include <Arduino.h>
#include <Print.h>

class SoftSerialTx : public Print {
private:
  uint8_t _pin;
  uint16_t _bitUs;

public:
  SoftSerialTx(uint8_t pin) : _pin(pin), _bitUs(50) {}

  void begin(uint32_t baud) {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, HIGH);
    if (baud == 9600) {
      _bitUs = 102;
    } else if (baud == 19200) {
      _bitUs = 50;
    } else if (baud == 115200) {
      _bitUs = 7;
    } else {
      _bitUs = (1000000UL / baud > 2) ? (uint16_t)(1000000UL / baud - 2) : 1;
    }
  }

  size_t write(uint8_t b) override {
    // Start bit (LOW)
    digitalWrite(_pin, LOW);
    delayMicroseconds(_bitUs);

    // 8 data bits (LSB first)
    for (uint8_t i = 0; i < 8; i++) {
      digitalWrite(_pin, (b & 0x01) ? HIGH : LOW);
      b >>= 1;
      delayMicroseconds(_bitUs);
    }

    // Stop bit (HIGH)
    digitalWrite(_pin, HIGH);
    delayMicroseconds(_bitUs);
    return 1;
  }

  using Print::write;
};

#endif
