/* [#04] nvm_flash.c - settings in the top 64-byte flash page (0x08003FC0), CRC16. */
#include "system.h"
#include <string.h>

#define NVM_ADDR        0x08003FC0UL
#define NVM_VERSION     1U
#define NVM_WORDS       (sizeof(SystemConfig_t) / 4U)

#define FL_STRT         (1UL << 6)
#define FL_LOCK         (1UL << 7)
#define FL_FLOCK        (1UL << 15)
#define FL_FTPG         (1UL << 16)
#define FL_FTER         (1UL << 17)
#define FL_BUFLOAD      (1UL << 18)
#define FL_BUFRST       (1UL << 19)
#define FL_STAT_BSY     (1UL << 0)

#define NVM_KEY1        0x45670123UL
#define NVM_KEY2        0xCDEF89ABUL

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

static void flash_wait(void)
{
    while (FLASH->STATR & FL_STAT_BSY) {
        hal_wdg_kick();
    }
}

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

static void nvm_all_defaults(SystemConfig_t *c)
{
    memset(c, 0, sizeof(*c));
    nvm_params_defaults(c);
    c->cal_v_q12 = 3506U;
    c->cal_a_q12 = 4096U;
    c->zero_v_q6 = 37U;
    c->zero_a_q6 = 19U;
    c->version   = NVM_VERSION;
}

bool nvm_save(void)
{
    g_cfg.version = NVM_VERSION;
    g_cfg.crc     = crc16((const uint8_t *)&g_cfg, sizeof(g_cfg) - 2U);

    NVIC_DisableIRQ(DMA1_Channel1_IRQn);

    FLASH->KEYR     = NVM_KEY1;
    FLASH->KEYR     = NVM_KEY2;
    FLASH->MODEKEYR = NVM_KEY1;
    FLASH->MODEKEYR = NVM_KEY2;

    flash_wait();

    /* Erase 64-byte page */
    FLASH->CTLR |= FL_FTER;
    FLASH->ADDR  = NVM_ADDR;
    FLASH->CTLR |= FL_STRT;
    flash_wait();
    FLASH->CTLR &= ~FL_FTER;

    /* Program 16 x 32-bit words */
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

    FLASH->ADDR  = NVM_ADDR;
    FLASH->CTLR |= FL_STRT;
    flash_wait();
    FLASH->CTLR &= ~FL_FTPG;
    FLASH->CTLR |= FL_LOCK | FL_FLOCK;

    NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    return memcmp((const void *)NVM_ADDR, &g_cfg, sizeof(g_cfg)) == 0;
}

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

void nvm_clear_fail(void)
{
    if (g_cfg.oc_fail != 0U) {
        g_cfg.oc_fail = 0U;
        (void)nvm_save();
    }
}