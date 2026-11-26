/* ============================ BEGINNER GUIDE: nvm_flash.c ============================
 * WHAT THIS FILE IS: the chip's "notebook that survives power-off".
 * Your settings (limits, delays, calibration, trip counter) are kept in a 64-byte Flash page.
 *   nvm_load()  = read the notebook at boot (or start with defaults if it is empty/corrupt)
 *   nvm_save()  = write the notebook (only when something really changed)
 * ======================================================================================= */

/* [#04] nvm_flash.c - Persistent storage in the top 64-byte Flash page (0x08003FC0) with CRC16.
 * Uses CH32V003 "Fast Page" erase/program hardware (64 bytes per page). */
#include "system.h"
#include <string.h>

/* NVM = "non-volatile memory" = memory that remembers when power is off. We use the LAST 64 bytes of the chip's
 * program Flash as a tiny notepad. Warning: your program must stay smaller than 16320 bytes or it would
 * overwrite this notepad. */
#define NVM_ADDR        0x08003FC0UL   /* Last 64 bytes of 16 KB Flash (0x08000000..0x08003FFF) */
#define NVM_VERSION     1U             /* Increment if SystemConfig_t struct layout ever changes */
#define NVM_WORDS       (sizeof(SystemConfig_t) / 4U)  /* 64 bytes / 4 = 16 x 32-bit words */

/* Compute CRC-16/CCITT-FALSE checksum to detect blank or corrupted Flash pages */
/* CRC = a "fingerprint" of the data. We store the fingerprint next to the settings. On boot we recompute it;
 * if it does not match, the page was blank or damaged (e.g. power cut while saving) and we ignore it. */
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
/* Flash is slow: erasing or writing takes a few milliseconds. We wait here (still feeding the watchdog)
 * until the controller says "BUSY = 0". */
static void flash_wait(void)
{
    while (FLASH->STATR & REG_FLASH_STATR_BUSY) {
        hal_wdg_kick();
    }
}

/* Restore user-adjustable protection settings (P01..P09) without wiping sensor calibration */
/* "Factory reset" of ONLY the user limits P01..P09. Calibration numbers are left alone. */
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
/* Full reset used when Flash is empty (brand-new chip). Calibration starts at nominal until you run CAL mode. */
static void nvm_all_defaults(SystemConfig_t *c)
{
    memset(c, 0, sizeof(*c));
    nvm_params_defaults(c);
    c->cal_v_q12 = DEF_CAL_V_Q12; /* Nominal gain until CAL wizard is run */
    c->cal_a_q12 = DEF_CAL_A_Q12;
    c->zero_v_q6 = DEF_ZERO_Q6;
    c->zero_a_q6 = DEF_ZERO_Q6;
    c->version   = NVM_VERSION;
}

/* Erase and reprogram the 64-byte NVM page in Flash, then verify byte-for-byte */
/* Saving to Flash = 1) fingerprint, 2) erase the old page, 3) write the new page, 4) read back and compare.
 * Flash can only be erased in whole pages and wears out after ~10,000 erases, so we save ONLY when needed. */
bool nvm_save(void)
{
    g_cfg.version = NVM_VERSION;
    g_cfg.crc     = crc16((const uint8_t *)&g_cfg, sizeof(g_cfg) - 2U);

    /* While Flash is erasing, the CPU cannot read program code from it (it is busy), so we pause the measuring
     * interrupt. It catches up afterwards (~7 ms is far less than the 20 ms block time). */
    /* Pause DMA ISR while Flash bus is stalled during erase/program */
    NVIC_DisableIRQ(DMA1_Channel1_IRQn);

    /* Unlock both standard Flash and Fast-Page Programming registers */
    /* Flash is LOCKED by default so a bug cannot erase it. Writing two secret numbers in order unlocks it. */
    FLASH->KEYR     = REG_FLASH_KEY1;
    FLASH->KEYR     = REG_FLASH_KEY2;
    FLASH->MODEKEYR = REG_FLASH_KEY1;
    FLASH->MODEKEYR = REG_FLASH_KEY2;

    flash_wait();

    /* A Flash bit can only change from 1 to 0 by writing; to get 1 again you must ERASE the whole page first. */
    /* Step 1: Erase the 64-byte page at 0x08003FC0 */
    FLASH->CTLR |= REG_FLASH_CTLR_FTER;
    FLASH->ADDR  = NVM_ADDR;
    FLASH->CTLR |= REG_FLASH_CTLR_START;
    flash_wait();
    FLASH->CTLR &= ~REG_FLASH_CTLR_FTER;

    /* Step 2: Reset internal page latch buffer and load 16 x 32-bit words */
    FLASH->CTLR |= REG_FLASH_CTLR_FTPG;
    FLASH->CTLR |= REG_FLASH_CTLR_BUFRST;
    flash_wait();

    const uint32_t *src    = (const uint32_t *)&g_cfg;
    volatile uint32_t *dst = (volatile uint32_t *)NVM_ADDR;
    for (uint32_t i = 0; i < NVM_WORDS; i++) {
        dst[i] = src[i];
        FLASH->CTLR |= REG_FLASH_CTLR_BUFLOAD;
        flash_wait();
    }

    /* Step 3: Commit page buffer to Flash and re-lock controller */
    FLASH->ADDR  = NVM_ADDR;
    FLASH->CTLR |= REG_FLASH_CTLR_START;
    flash_wait();
    FLASH->CTLR &= ~REG_FLASH_CTLR_FTPG;
    FLASH->CTLR |= REG_FLASH_CTLR_LOCK | REG_FLASH_CTLR_FLOCK;

    NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    return memcmp((const void *)NVM_ADDR, &g_cfg, sizeof(g_cfg)) == 0;
}

/* Load settings from Flash on boot; initialize defaults if Flash is unprogrammed or corrupt */
/* Called once at boot. If the saved page is valid we use it; if not (first start / corrupted) we use defaults. */
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
/* The over-current trip counter is saved in Flash on purpose: unplugging the power cannot cheat the lockout. */
void nvm_clear_fail(void)
{
    if (g_cfg.oc_fail != 0U) {
        g_cfg.oc_fail = 0U;
        (void)nvm_save();
    }
}