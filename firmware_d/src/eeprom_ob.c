#include "eeprom_ob.h"
#include "ch32fun.h"
#include <string.h>

// ============================================================================
// IMPLEMENTASI EMULASI EEPROM FLASH OPTION BYTES (CH32V003)
// ============================================================================
// Alamat dasar Option Bytes: OB_BASE = 0x1FFFF800.
// Terdiri dari:
// - Index 0: Byte Data0 (OB16 index 2)
// - Index 1: Byte Data1 (OB16 index 3)
// - Index 2..25: Byte data cadangan User Option Bytes (OB16 index 8..31)
// Total kapasitas yang dapat digunakan = 26 Byte.
// ============================================================================

#define OB_AVAIL_DATA_START 8

// Mask register kontrol FLASH CH32V003
#define CR_OPTPG_Set   ((uint32_t)0x00000010) // Aktifkan pemrograman Option Byte
#define CR_OPTPG_Reset ((uint32_t)0xFFFFFFEF)
#define CR_OPTER_Set   ((uint32_t)0x00000020) // Aktifkan penghapusan (erase) Option Byte
#define CR_OPTER_Reset ((uint32_t)0xFFFFFFDF)
#define CR_STRT_Set    ((uint32_t)0x00000040) // Picu operasi flash
#define CR_LOCK_Set    ((uint32_t)0x00000080) // Kunci kembali controller flash

// Kunci pembuka proteksi flash (Flash Unlock Keys)
#define FLASH_KEY1     ((uint32_t)0x45670123)
#define FLASH_KEY2     ((uint32_t)0xCDEF89AB)

static uint8_t ob_cache[EEPROM_OB_SIZE]; // Cache RAM penyangga agar tidak sering flash fisik
static bool    ob_dirty = false;         // Menandakan ada data di RAM yang belum ditulis ke flash

void eeprom_begin(void) {
    uint16_t *ob16 = (uint16_t *)OB_BASE;
    // Salin 26 byte dari alamat flash fisik OB ke cache RAM
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
    // Hanya tulis jika nilainya berbeda (menghemat siklus write flash)
    if (eeprom_read(idx) != val) {
        eeprom_write(idx, val);
        eeprom_commit();
    }
}

bool eeprom_commit(void) {
    // Jika tidak ada perubahan data di RAM, lewati penulisan fisik
    if (!ob_dirty) return true;

    // Buffer sementara untuk mempertahankan settingan sistem bawaan MCU di Option Bytes
    volatile uint16_t hold[8];
    uint32_t *hold32 = (uint32_t *)hold;
    uint32_t *ob32   = (uint32_t *)OB_BASE;
    hold32[0] = ob32[0];
    hold32[1] = ob_cache[0] | ((uint32_t)ob_cache[1] << 16);
    hold32[2] = ob32[2];
    hold32[3] = ob32[3];

    // 1. Buka kunci Flash Controller & Option Bytes Controller
    FLASH->KEYR   = FLASH_KEY1;
    FLASH->KEYR   = FLASH_KEY2;
    FLASH->OBKEYR = FLASH_KEY1;
    FLASH->OBKEYR = FLASH_KEY2;

    // 2. Lakukan Erase (penghapusan) halaman Option Byte
    FLASH->CTLR |= CR_OPTER_Set | CR_STRT_Set;
    while (FLASH->STATR & FLASH_BUSY); // Tunggu sampai selesai
    FLASH->CTLR &= CR_OPTER_Reset;

    // 3. Masuk mode pemrograman (Program Mode)
    FLASH->CTLR |= CR_OPTPG_Set;
    uint16_t *ob16 = (uint16_t *)OB_BASE;
    for (size_t i = 0; i < sizeof(hold) / sizeof(hold[0]); i++) {
        ob16[i] = hold[i];
        while (FLASH->STATR & FLASH_BUSY);
    }

    // 4. Tulis byte data user yang telah dimodifikasi
    for (int i = 2; i < EEPROM_OB_SIZE; i++) {
        if (ob_cache[i] != 0xFF) {
            ob16[OB_AVAIL_DATA_START + (i - 2)] = ob_cache[i];
            while (FLASH->STATR & FLASH_BUSY);
        }
    }

    // 5. Kunci kembali controller flash untuk proteksi keamanan data
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
    // Cek apakah data baru berbeda dengan isi cache sebelum menandai dirty
    if (memcmp(&ob_cache[addr], src, size) != 0) {
        memcpy(&ob_cache[addr], src, size);
        ob_dirty = true;
    }
}
