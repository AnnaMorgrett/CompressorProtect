/* [#04] nvm_flash.c - Persistent storage in the top 64-byte Flash page (0x08003FC0) with CRC16.
 * Uses CH32V003 "Fast Page" erase/program hardware (64 bytes per page). */
#include "system.h"
#include <string.h>

#define NVM_ADDR        0x08003FC0UL   /* Last 64 bytes of 16 KB Flash (0x08000000..0x08003FFF) */
#define NVM_VERSION     1U             /* Increment if SystemConfig_t struct layout ever changes */
#define NVM_WORDS       (sizeof(SystemConfig_t) / 4U)  /* 64 bytes / 4 = 16 x 32-bit words */

/* CH32V003 Flash controller (FLASH->CTLR and FLASH->STATR) bit definitions */
#define FL_STRT         (1UL << 6)     /* Start erase or program operation */
#define FL_LOCK         (1UL << 7)     /* Standard Flash lock bit */
#define FL_FLOCK        (1UL << 15)    /* Fast-programming mode lock bit */
#define FL_FTPG         (1UL << 16)    /* Fast page program mode (64 bytes) */
#define FL_FTER         (1UL << 17)    /* Fast page erase mode (64 bytes) */
#define FL_BUFLOAD      (1UL << 18)    /* Latch one 32-bit word into internal page write buffer */
#define FL_BUFRST       (1UL << 19)    /* Clear internal 64-byte page write buffer */
#define FL_STAT_BSY     (1UL << 0)     /* Flash controller busy flag */

/* Hardware unlock keys required by CH32V003 Flash controller */
#define NVM_KEY1        0x45670123UL
#define NVM_KEY2        0xCDEF89ABUL

/* Compute CRC-16/CCITT-FALSE checksum to detect blank or corrupted Flash pages */
static uint16_t crc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0xFFFFU;
    while (n--) {
        crc ^= (uint16_t)(*p++) << 8;
        for (uint8_t b = 0; b < 8U; b++) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* Spin safely until Flash erase/write finishes while keeping the 1.0 s watchdog fed */
static void flash_wait(void)
{
    while (FLASH->STATR & FL_STAT_BSY) {
        hal_wdg_kick();
    }
}

/* Restore user-adjustable protection settings (P01..P09) without wiping sensor calibration */
void nvm_params_defaults(SystemConfig_t *c)
{
    c->ov_v      = DEF_OV_V;
    c->uv_v      = DEF_UV_V;
    c->hys_v     = DEF_HYS_V;
    c->oc_ca     = DEF_OC_CA;
    c->inr_x10   = DEF_INR_X10;
    c->tin_ds    = DEF_TIN_DS;
    c->toc_ds    = DEF_TOC_DS;
    c->delay_min = DEF_DELAY_MIN;
    c->ret_cnt   = DEF_RET_CNT;
}

/* Full factory initialization used on first boot when Flash page is blank (0xFF) */
static void nvm_all_defaults(SystemConfig_t *c)
{
    memset(c, 0, sizeof(*c));
    nvm_params_defaults(c);
    c->cal_v_q12 = 3506U;   /* Bench-calibrated VAC gain trim for this hardware */
    c->cal_a_q12 = 4096U;   /* Nominal 1.000x CT gain trim */
    c->zero_v_q6 = 37U;     /* Bench-calibrated VAC zero-volt noise floor */
    c->zero_a_q6 = 19U;     /* Bench-calibrated CT zero-amp noise floor */
    c->version   = NVM_VERSION;
}

/* Erase and reprogram the 64-byte NVM page in Flash, then verify byte-for-byte */
bool nvm_save(void)
{
    g_cfg.version = NVM_VERSION;
    g_cfg.crc     = crc16((const uint8_t *)&g_cfg, sizeof(g_cfg) - 2U);

    /* Pause DMA ISR while Flash bus is stalled during erase/program */
    NVIC_DisableIRQ(DMA1_Channel1_IRQn);

    /* Unlock both standard Flash and Fast-Page Programming registers */
    FLASH->KEYR     = NVM_KEY1;
    FLASH->KEYR     = NVM_KEY2;
    FLASH->MODEKEYR = NVM_KEY1;
    FLASH->MODEKEYR = NVM_KEY2;

    flash_wait();

    /* Step 1: Erase the 64-byte page at 0x08003FC0 */
    FLASH->CTLR |= FL_FTER;
    FLASH->ADDR  = NVM_ADDR;
    FLASH->CTLR |= FL_STRT;
    flash_wait();
    FLASH->CTLR &= ~FL_FTER;

    /* Step 2: Reset internal page latch buffer and load 16 x 32-bit words */
    FLASH->CTLR |= FL_FTPG;
    FLASH->CTLR |= FL_BUFRST;
    flash_wait();

    const uint32_t *src    = (const uint32_t *)&g_cfg;
    volatile uint32_t *dst = (volatile uint32_t *)NVM_ADDR;
    for (uint32_t i = 0; i < NVM_WORDS; i++) {
        dst[i] = src[i];
        FLASH->CTLR |= FL_BUFLOAD;
        flash_wait();
    }

    /* Step 3: Commit page buffer to Flash and re-lock controller */
    FLASH->ADDR  = NVM_ADDR;
    FLASH->CTLR |= FL_STRT;
    flash_wait();
    FLASH->CTLR &= ~FL_FTPG;
    FLASH->CTLR |= FL_LOCK | FL_FLOCK;

    NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    return memcmp((const void *)NVM_ADDR, &g_cfg, sizeof(g_cfg)) == 0;
}

/* Load settings from Flash on boot; initialize defaults if Flash is unprogrammed or corrupt */
void nvm_load(void)
{
    const SystemConfig_t *f = (const SystemConfig_t *)NVM_ADDR;
    bool valid = (f->version == NVM_VERSION) &&
                 (f->crc == crc16((const uint8_t *)f, sizeof(*f) - 2U));
    if (valid) {
        memcpy(&g_cfg, f, sizeof(g_cfg));
    } else {
        nvm_all_defaults(&g_cfg);
        (void)nvm_save();
    }
}

/* Reset persistent over-current trip counter (called after 3 min healthy run or LOC3 unlock) */
void nvm_clear_fail(void)
{
    if (g_cfg.oc_fail != 0U) {
        g_cfg.oc_fail = 0U;
        (void)nvm_save();
    }
}