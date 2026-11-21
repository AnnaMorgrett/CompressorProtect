#ifndef SYS_TIME_H
#define SYS_TIME_H

#include <stdint.h>

// ============================================================================
// MODUL PENCATAT WAKTU SISTEM (CH32V003 SYSTICK TIMER)
// ============================================================================
// Menggantikan fungsi millis() dan micros() bawaan Arduino menggunakan
// register SysTick internal arsitektur QingKe RISC-V @ 48 MHz.
// ============================================================================

// Inisialisasi counter SysTick 48 MHz
void     sys_time_init(void);

// Mengembalikan waktu berjalan sistem dalam satuan milidetik (ms)
uint32_t millis(void);

// Mengembalikan waktu berjalan sistem dalam satuan mikrodetik (us)
uint32_t micros(void);

#endif
