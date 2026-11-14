#ifndef SERIAL_CLI_H
#define SERIAL_CLI_H

#include "board.h"

extern bool serial_menu_active;
extern bool serial_menu_stream;

void serial_cli_init(void);
void serial_cli_enable_rx(void);
bool serial_cli_rx_incoming(void);
void serial_cli_poll(void);
void serial_cli_send_telemetry(void);
void serial_cli_log_str(const char *s);
void serial_cli_log_fault(int32_t fault_code);

#endif