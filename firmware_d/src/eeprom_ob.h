#ifndef EEPROM_OB_H
#define EEPROM_OB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define EEPROM_OB_SIZE 26

void    eeprom_begin(void);
uint8_t eeprom_read(int idx);
void    eeprom_write(int idx, uint8_t val);
void    eeprom_update(int idx, uint8_t val);
bool    eeprom_commit(void);
void    eeprom_get(int addr, void *dest, size_t size);
void    eeprom_put(int addr, const void *src, size_t size);

#endif
