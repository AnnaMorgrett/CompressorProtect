/* [#03] hal_sys.c - fail-safe GPIO, SysTick 1 ms, IWDG, relay shadow. */
#include "system.h"
#include <stdio.h>

volatile uint32_t g_ms = 0;
static volatile bool relay_on = false;

#define ST_STE       (1UL << 0)
#define ST_STIE      (1UL << 1)
#define ST_STCLK     (1UL << 2)   /* 1 = HCLK */

#define IWDG_UNLOCK  0x5555U
#define IWDG_RELOAD  0xAAAAU
#define IWDG_START   0xCCCCU

static void pin_out_low(uint32_t pin)
{
    funDigitalWrite(pin, FUN_LOW);
    funPinMode(pin, GPIO_CFGLR_OUT_10Mhz_PP);
}

static void pin_in_pullup(uint32_t pin)
{
    funPinMode(pin, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(pin, FUN_HIGH);
}

void hal_init(void)
{
    /* Keep HPE disabled so s0/s1 are not clobbered by interrupts */
    __asm__ volatile("csrw 0x804, %0" : : "r"(0));

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC |
                      RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO;

    /* Ensure PA1 & PA2 are mapped as GPIO/ADC rather than crystal OSCI/OSCO */
    AFIO->PCFR1 &= ~(1UL << 15);

    /* 1. Fail-safe outputs first */
    pin_out_low(PIN_RELAY);
    pin_out_low(PIN_BUZZER);
    pin_out_low(PIN_LED);
    relay_on = false;

    /* 2. Display lines (TM1637 on PC4 CLK / PC3 DIO) */
    funDigitalWrite(PIN_TM_CLK, FUN_HIGH);
    funDigitalWrite(PIN_TM_DIO, FUN_HIGH);
    funPinMode(PIN_TM_CLK, GPIO_CFGLR_OUT_10Mhz_PP);
    funPinMode(PIN_TM_DIO, GPIO_CFGLR_OUT_10Mhz_PP);

    /* 3. Inputs & unused SOP16 pull-ups */
    pin_in_pullup(PIN_CAL);
    pin_in_pullup(PC1);          /* NTC2 header, unused */
    pin_in_pullup(PC2);          /* NTC1 header, unused */
    pin_in_pullup(PC7);          /* TX header, unused   */
    funPinMode(PIN_VAC, GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_CT,  GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_KEY, GPIO_CFGLR_IN_ANALOG);

    /* 4. SysTick: free-running CNT (STRE=0), compare IRQ every 1 ms */
    SysTick->CTLR = 0;
    SysTick->SR   = 0;
    SysTick->CMP  = SysTick->CNT + SYSTICK_TICKS_1MS;
    SysTick->CTLR = ST_STE | ST_STCLK | ST_STIE;
    NVIC_EnableIRQ(SysTick_IRQn);

    /* 5. Independent Watchdog, 1.0 s */
    IWDG->CTLR = IWDG_UNLOCK;
    IWDG->PSCR = IWDG_PRESCALER_CODE;
    IWDG->RLDR = IWDG_RELOAD_TICKS;
    IWDG->CTLR = IWDG_RELOAD;
    IWDG->CTLR = IWDG_START;

#if ENABLE_SWIO_LOG
    SetupDebugPrintf();
#endif
}

void hal_wdg_kick(void)
{
    IWDG->CTLR = IWDG_RELOAD;
}

void hal_relay_set(bool on)
{
    funDigitalWrite(PIN_RELAY, on ? FUN_HIGH : FUN_LOW);
    relay_on = on;
}

bool hal_relay_is_on(void)
{
    return relay_on;
}

void hal_log(void)
{
#if ENABLE_SWIO_LOG
    static uint32_t last = 0;
    if ((uint32_t)(g_ms - last) >= 500UL) {
        last = g_ms;
        printf("V=%u raw=%u I=%u vdd=%u rmsV=%u rmsA=%u\r\n",
               g_sens.vac_disp_dv, g_sens.vac_raw_dv, g_sens.i_disp_ca,
               g_sens.vdd_mv, g_sens.vac_rms_q6, g_sens.ct_rms_q6);
    }
#endif
}

void SysTick_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void)
{
    SysTick->SR = 0;
    uint32_t next = SysTick->CMP + SYSTICK_TICKS_1MS;
    if ((int32_t)(next - SysTick->CNT) < 1000) {
        next = SysTick->CNT + SYSTICK_TICKS_1MS;
    }
    SysTick->CMP = next;
    g_ms++;
    alerts_tick_1ms();
}