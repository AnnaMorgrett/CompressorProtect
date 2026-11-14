#include "eeprom_ob.h"

#define OB_AVAIL_DATA_START 8

static uint8_t eeprom_buf[EEPROM_SIZE] = {0};
static bool    eeprom_dirty = false;

void eeprom_begin(void) {
    uint16_t *ob16p = (uint16_t *)OB_BASE;
    eeprom_buf[0] = (uint8_t)ob16p[2];
    eeprom_buf[1] = (uint8_t)ob16p[3];
    for (size_t i = 2; i < EEPROM_SIZE; i++) {
        eeprom_buf[i] = (uint8_t)ob16p[OB_AVAIL_DATA_START + (i - 2)];
    }
    eeprom_dirty = false;
}

uint8_t eeprom_read(uint8_t idx) {
    if (idx < EEPROM_SIZE) return eeprom_buf[idx];
    return 0;
}

void eeprom_write(uint8_t idx, uint8_t val) {
    if (idx < EEPROM_SIZE && eeprom_buf[idx] != val) {
        eeprom_dirty = true;
        eeprom_buf[idx] = val;
    }
}

void eeprom_update(uint8_t idx, uint8_t val) {
    eeprom_write(idx, val);
    eeprom_commit();
}

void eeprom_erase(void) {
    eeprom_dirty = true;
    memset(eeprom_buf, 0xFF, EEPROM_SIZE);
}

void eeprom_get(uint8_t addr, void *dst, size_t len) {
    if ((size_t)addr + len > EEPROM_SIZE) return;
    memcpy(dst, eeprom_buf + addr, len);
}

void eeprom_put(uint8_t addr, const void *src, size_t len) {
    if ((size_t)addr + len > EEPROM_SIZE) return;
    if (memcmp(eeprom_buf + addr, src, len) != 0) {
        eeprom_dirty = true;
        memcpy(eeprom_buf + addr, src, len);
    }
}

bool eeprom_commit(void) {
    if (!eeprom_dirty) return true;

    volatile uint16_t hold[8];
    uint32_t *hold32p = (uint32_t *)hold;
    uint32_t *ob32p   = (uint32_t *)OB_BASE;

    hold32p[0] = ob32p[0];
    hold32p[1] = (uint32_t)eeprom_buf[0] | ((uint32_t)eeprom_buf[1] << 16);
    hold32p[2] = ob32p[2];
    hold32p[3] = ob32p[3];

    FLASH->KEYR   = FLASH_KEY1;
    FLASH->KEYR   = FLASH_KEY2;
    FLASH->OBKEYR = FLASH_KEY1;
    FLASH->OBKEYR = FLASH_KEY2;

    FLASH->CTLR |= FLASH_CTLR_OPTER;
    FLASH->CTLR |= FLASH_CTLR_STRT;
    while (FLASH->STATR & FLASH_STATR_BSY);
    FLASH->CTLR &= ~FLASH_CTLR_OPTER;
    FLASH->CTLR |= FLASH_CTLR_OPTPG;

    uint16_t *ob16p = (uint16_t *)OB_BASE;
    for (size_t i = 0; i < 8; i++) {
        ob16p[i] = hold[i];
        while (FLASH->STATR & FLASH_STATR_BSY);
    }

    for (size_t i = 2; i < EEPROM_SIZE; i++) {
        if (eeprom_buf[i] != 0xFF) {
            ob16p[OB_AVAIL_DATA_START + (i - 2)] = eeprom_buf[i];
            while (FLASH->STATR & FLASH_STATR_BSY);
        }
    }

    FLASH->CTLR &= ~FLASH_CTLR_OPTPG;
    FLASH->CTLR |= FLASH_CTLR_LOCK;
    eeprom_dirty = false;
    return true;
}