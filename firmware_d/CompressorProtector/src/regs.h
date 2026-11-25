/* [#00] regs.h - Every raw register bit / magic number used by the firmware, with a name and a comment.
 * Rule: no unexplained "1UL << 15" or "0x804" anywhere else. Values are from the CH32V003 datasheet /
 * reference manual and the ch32fun notes. Names are prefixed REG_ so they can never clash with ch32fun. */
#ifndef REGS_H
#define REGS_H

#include "config.h"

/* ---- CPU: interrupt system control ---------------------------------------- */
/* INTSYSCR is CSR 0x804. Value 0 = HPE (hardware stacking) and hardware nesting OFF, so GCC's normal
 * interrupt prologue saves every register (ch32fun notes: HPE does not preserve s0/s1). */
static inline void cpu_hpe_off(void)
{
    __asm__ volatile("csrw 0x804, %0" : : "r"(0));
}

/* ---- AFIO ------------------------------------------------------------------ */
#define REG_AFIO_PA12_REMAP   (1UL << 15)  /* PCFR1: 1 = PA1/PA2 become crystal pins; 0 = normal GPIO/ADC */

/* ---- SysTick (CTLR bits) --------------------------------------------------- */
#define REG_SYSTICK_ENABLE     (1UL << 0)  /* Counter enable                          */
#define REG_SYSTICK_IRQ_ENABLE (1UL << 1)  /* Interrupt on compare match              */
#define REG_SYSTICK_CLK_HCLK   (1UL << 2)  /* 1 = count at HCLK (48 MHz), 0 = HCLK/8  */

/* ---- Independent watchdog -------------------------------------------------- */
#define REG_IWDG_KEY_UNLOCK    0x5555U     /* Allow writing PSCR / RLDR               */
#define REG_IWDG_KEY_RELOAD    0xAAAAU     /* "Kick": reload the counter              */
#define REG_IWDG_KEY_START     0xCCCCU     /* Start counting                          */
#define REG_IWDG_BUSY_MASK     0x3U        /* STATR bits PVU | RVU: register update still running */

/* ---- Flash controller (FLASH->CTLR / STATR) -------------------------------- */
#define REG_FLASH_KEY1         0x45670123UL /* Unlock key 1 (KEYR and MODEKEYR)       */
#define REG_FLASH_KEY2         0xCDEF89ABUL /* Unlock key 2                           */
#define REG_FLASH_CTLR_START   (1UL << 6)   /* Start erase / program                  */
#define REG_FLASH_CTLR_LOCK    (1UL << 7)   /* Normal flash lock                      */
#define REG_FLASH_CTLR_FLOCK   (1UL << 15)  /* Fast-mode lock                         */
#define REG_FLASH_CTLR_FTPG    (1UL << 16)  /* Fast page program (64 bytes)           */
#define REG_FLASH_CTLR_FTER    (1UL << 17)  /* Fast page erase (64 bytes)             */
#define REG_FLASH_CTLR_BUFLOAD (1UL << 18)  /* Latch one word into the page buffer    */
#define REG_FLASH_CTLR_BUFRST  (1UL << 19)  /* Clear the page buffer                  */
#define REG_FLASH_STATR_BUSY   (1UL << 0)   /* Operation in progress                  */

/* ---- ADC ------------------------------------------------------------------- */
#define REG_ADC_CTLR1_SCAN        (1UL << 8)   /* Scan mode over the regular sequence      */
#define REG_ADC_CTLR2_JEXTSEL_SW  (7UL << 12)  /* Injected group trigger = software        */
#define REG_ADC_CTLR2_JEXTTRIG    (1UL << 15)  /* Enable injected trigger                  */
#define REG_ADC_CTLR2_JSWSTART    (1UL << 21)  /* Start injected conversion now            */
#define REG_ADC_STATR_JEOC        (1UL << 2)   /* Injected conversion finished             */
#define REG_ADC_SAMPTR_BITS       3U           /* Bits per channel in SAMPTR2              */
#define REG_ADC_RSQR1_LEN_SHIFT   20U          /* RSQR1: sequence length - 1 field         */
#define REG_ADC_RSQR3_SLOT_SHIFT  5U           /* RSQR3: bits per sequence slot            */
#define REG_ADC_ISQR_JSQ4_SHIFT   15U          /* ISQR: channel number of a 1-step injected group */

/* ---- TIM1 ------------------------------------------------------------------ */
#define REG_TIM1_MMS_UPDATE    (2U << 4)   /* CTLR2 MMS = 010: update event drives TRGO (ADC trigger) */

#endif /* REGS_H */
