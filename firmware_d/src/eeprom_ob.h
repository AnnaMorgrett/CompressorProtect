#ifndef EEPROM_OB_H
#define EEPROM_OB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// DRIVER EMULASI EEPROM DENGAN USER OPTION BYTES (CH32V003)
// ============================================================================
// Mikrokontroler CH32V003 tidak memiliki memori true EEPROM independen.
// Sebagai gantinya, chip ini menyediakan area "User Option Bytes" pada memori
// flash khusus (alamat 0x1FFFF800) sebesar 26 byte yang dapat diprogram ulang
// dan tidak terhapus saat mati lampu (non-volatile storage).
// ============================================================================

#define EEPROM_OB_SIZE 26 // Ukuran maksimal area data Option Bytes

// Membaca seluruh data dari flash Option Bytes ke memori cache RAM internal
void    eeprom_begin(void);

// Membaca 1 byte data dari cache RAM pada indeks tertentu
uint8_t eeprom_read(int idx);

// Menulis 1 byte data ke cache RAM dan menandai status dirty (perlu commit)
void    eeprom_write(int idx, uint8_t val);

// Menulis byte ke cache dan langsung membakarnya ke flash fisik jika nilai berbeda
void    eeprom_update(int idx, uint8_t val);

// Membakar (flashing) seluruh isi cache RAM yang berubah ke Option Bytes fisik MCU
bool    eeprom_commit(void);

// Membaca blok struct dari cache RAM ke variabel memori
void    eeprom_get(int addr, void *dest, size_t size);

// Menyalin blok struct dari variabel memori ke cache RAM
void    eeprom_put(int addr, const void *src, size_t size);

#endif
