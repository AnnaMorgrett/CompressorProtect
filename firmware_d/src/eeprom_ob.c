#include "eeprom_ob.h"
#include "ch32fun.h"
#include <string.h>

#define OB_AVAIL_DATA_START 8

#define CR_OPTPG_Set   ((uint32_t)0x00000010)
#define CR_OPTPG_Reset ((uint32_t)0xFFFFFFEF)
#define CR_OPTER_Set   ((uint32_t)0x00000020)
#define CR_OPTER_Reset ((uint32_t)0xFFFFFFDF)
#define CR_STRT_Set    ((uint32_t)0x00000040)
#define CR_LOCK_Set    ((uint32_t)0x00000080)

#define FLASH_KEY1     ((uint32_t)0x45670123)
#define FLASH_KEY2     ((uint32_t)0xCDEF89AB)

static uint8_t ob_cache[EEPROM_OB_SIZE];
static bool    ob_dirty = false;

void eeprom_begin(void) {
    uint16_t *ob16 = (uint16_t *)OB_BASE;
    ob_cache[0] = (uint8_t)ob16[2];
    ob_cache[1] = (uint8_t)ob16[3];
    for (int i = 2; i < EEPROM_OB_SIZE; i++) {
        ob_cache[i] = (uint8_t)ob16[OB_AVAIL_DATA_START + (i - 2)];
    }
    ob_dirty = false;
}

uint8_t eeprom_read(int idx) {
    if (idx >= 0 && idx < EEPROM_OB_SIZE) return ob_cache[idx];
    return 0;
}

void eeprom_write(int idx, uint8_t val) {
    if (idx >= 0 && idx < EEPROM_OB_SIZE) {
        ob_cache[idx] = val;
        ob_dirty = true;
    }
}

void eeprom_update(int idx, uint8_t val) {
    if (eeprom_read(idx) != val) {
        eeprom_write(idx, val);
        eeprom_commit();
    }
}

bool eeprom_commit(void) {
    if (!ob_dirty) return true;

    volatile uint16_t hold[8];
    uint32_t *hold32 = (uint32_t *)hold;
    uint32_t *ob32   = (uint32_t *)OB_BASE;
    hold32[0] = ob32[0];
    hold32[1] = ob_cache[0] | ((uint32_t)ob_cache[1] << 16);
    hold32[2] = ob32[2];
    hold32[3] = ob32[3];

    FLASH->KEYR   = FLASH_KEY1;
    FLASH->KEYR   = FLASH_KEY2;
    FLASH->OBKEYR = FLASH_KEY1;
    FLASH->OBKEYR = FLASH_KEY2;

    FLASH->CTLR |= CR_OPTER_Set | CR_STRT_Set;
    while (FLASH->STATR & FLASH_BUSY);
    FLASH->CTLR &= CR_OPTER_Reset;

    FLASH->CTLR |= CR_OPTPG_Set;
    uint16_t *ob16 = (uint16_t *)OB_BASE;
    for (size_t i = 0; i < sizeof(hold) / sizeof(hold[0]); i++) {
        ob16[i] = hold[i];
        while (FLASH->STATR & FLASH_BUSY);
    }

    for (int i = 2; i < EEPROM_OB_SIZE; i++) {
        if (ob_cache[i] != 0xFF) {
            ob16[OB_AVAIL_DATA_START + (i - 2)] = ob_cache[i];
            while (FLASH->STATR & FLASH_BUSY);
        }
    }

    FLASH->CTLR &= CR_OPTPG_Reset;
    FLASH->CTLR |= CR_LOCK_Set;
    ob_dirty = false;
    return true;
}

void eeprom_get(int addr, void *dest, size_t size) {
    if (addr < 0 || addr + (int)size > EEPROM_OB_SIZE) return;
    memcpy(dest, &ob_cache[addr], size);
}

void eeprom_put(int addr, const void *src, size_t size) {
    if (addr < 0 || addr + (int)size > EEPROM_OB_SIZE) return;
    if (memcmp(&ob_cache[addr], src, size) != 0) {
        memcpy(&ob_cache[addr], src, size);
        ob_dirty = true;
    }
}
