#ifndef SOFT_SERIAL_H
#define SOFT_SERIAL_H

#include "sys_time.h"

void soft_serial_begin(uint32_t baud);
void soft_serial_enable_rx(void);
bool soft_serial_rx_start_detected(void);
int soft_serial_available(void);
int soft_serial_read(void);
void soft_serial_write_byte(uint8_t b);
void soft_serial_print_char(char c);
void soft_serial_print_str(const char *s);
void soft_serial_print_int(int32_t n);
void soft_serial_println(void);
void soft_serial_println_str(const char *s);
void soft_serial_println_int(int32_t n);

#endif