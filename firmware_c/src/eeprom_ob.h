#ifndef EEPROM_OB_H
#define EEPROM_OB_H

#include "ch32fun.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define EEPROM_SIZE 26

void eeprom_begin(void);
uint8_t eeprom_read(uint8_t idx);
void eeprom_write(uint8_t idx, uint8_t val);
void eeprom_update(uint8_t idx, uint8_t val);
void eeprom_erase(void);
bool eeprom_commit(void);
void eeprom_get(uint8_t addr, void *dst, size_t len);
void eeprom_put(uint8_t addr, const void *src, size_t len);

#endif