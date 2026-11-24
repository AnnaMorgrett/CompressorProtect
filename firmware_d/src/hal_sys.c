/* [#03] hal_sys.c - Fail-safe GPIO initialization, 1 ms SysTick timer, IWDG watchdog, and relay driver. */
#include "system.h"
#include <stdio.h>

volatile uint32_t g_ms = 0;         /* System uptime in milliseconds (rolls over after ~49.7 days) */
static volatile bool relay_on = false; /* Shadow state of relay pin so ISR can check status fast */

/* CH32V003 SysTick control register bits */
#define ST_STE       (1UL << 0)     /* SysTick Counter Enable */
#define ST_STIE      (1UL << 1)     /* SysTick Interrupt Enable */
#define ST_STCLK     (1UL << 2)     /* Clock source: 1 = HCLK (48 MHz), 0 = HCLK/8 */

/* CH32V003 Independent Watchdog (IWDG) magic unlock/reload keys */
#define IWDG_UNLOCK  0x5555U        /* Enable write access to PSCR and RLDR registers */
#define IWDG_RELOAD  0xAAAAU        /* Reset watchdog countdown timer ("kick the dog") */
#define IWDG_START   0xCCCCU        /* Start watchdog hardware countdown */

/* Drive a GPIO output LOW before enabling its push-pull driver to avoid power-on glitches */
static void pin_out_low(uint32_t pin)
{
    funDigitalWrite(pin, FUN_LOW);
    funPinMode(pin, GPIO_CFGLR_OUT_10Mhz_PP);
}

/* Configure an unused or jumper input with internal ~40 kOhm pull-up enabled */
static void pin_in_pullup(uint32_t pin)
{
    funPinMode(pin, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(pin, FUN_HIGH);
}

void hal_init(void)
{
    /* Disable Hardware Prologue/Epilogue (HPE) interrupt register stacking so GCC's
     * standard RISC-V interrupt attribute preserves s0/s1 safely without corruption. */
    __asm__ volatile("csrw 0x804, %0" : : "r"(0));

    /* Enable peripheral clocks for GPIO Ports A, C, D and Alternate Function I/O */
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC |
                      RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO;

    /* Clear PA12_RM (bit 15) so PA1 and PA2 act as GPIO/ADC pins instead of external crystal pins */
    AFIO->PCFR1 &= ~(1UL << 15);

    /* 1. Initialize critical outputs LOW immediately at boot (relay OFF, buzzer OFF, LED OFF) */
    pin_out_low(PIN_RELAY);
    pin_out_low(PIN_BUZZER);
    pin_out_low(PIN_LED);
    relay_on = false;

    /* 2. Initialize TM1637 display lines idle HIGH */
    funDigitalWrite(PIN_TM_CLK, FUN_HIGH);
    funDigitalWrite(PIN_TM_DIO, FUN_HIGH);
    funPinMode(PIN_TM_CLK, GPIO_CFGLR_OUT_10Mhz_PP);
    funPinMode(PIN_TM_DIO, GPIO_CFGLR_OUT_10Mhz_PP);

    /* 3. Configure calibration jumper, unused SOP16 header pins, and analog inputs */
    pin_in_pullup(PIN_CAL);      /* PC6: pulled high; shorting H1 pin 3 to GND enters CAL mode */
    pin_in_pullup(PC1);          /* NTC2 header (unused): pull high so pin never floats */
    pin_in_pullup(PC2);          /* NTC1 header (unused): pull high so pin never floats */
    pin_in_pullup(PC7);          /* TX header (unused): pull high so pin never floats */
    funPinMode(PIN_VAC, GPIO_CFGLR_IN_ANALOG); /* PA2: AC mains voltage sense */
    funPinMode(PIN_CT,  GPIO_CFGLR_IN_ANALOG); /* PA1: AC load current sense */
    funPinMode(PIN_KEY, GPIO_CFGLR_IN_ANALOG); /* PD6: Resistor-ladder buttons */

    /* 4. Configure SysTick timer to fire an interrupt every 1.000 ms (48,000 ticks @ 48 MHz) */
    SysTick->CTLR = 0;
    SysTick->SR   = 0;
    SysTick->CMP  = SysTick->CNT + SYSTICK_TICKS_1MS;
    SysTick->CTLR = ST_STE | ST_STCLK | ST_STIE;
    NVIC_EnableIRQ(SysTick_IRQn);

    /* 5. Start Independent Watchdog (IWDG) with 1.0 s timeout.
     * If firmware ever hangs or enters an infinite loop without calling hal_wdg_kick(),
     * the MCU automatically resets and forces PIN_RELAY LOW. */
    IWDG->CTLR = IWDG_UNLOCK;
    IWDG->PSCR = IWDG_PRESCALER_CODE;
    IWDG->RLDR = IWDG_RELOAD_TICKS;
    IWDG->CTLR = IWDG_RELOAD;
    IWDG->CTLR = IWDG_START;

#if ENABLE_SWIO_LOG
    SetupDebugPrintf();
#endif
}

/* Refresh the 1.0 s hardware watchdog timer (must be called regularly in main loop & wait loops) */
void hal_wdg_kick(void)
{
    IWDG->CTLR = IWDG_RELOAD;
}

/* Turn compressor relay ON (true) or OFF (false) and update fast shadow variable */
void hal_relay_set(bool on)
{
    funDigitalWrite(PIN_RELAY, on ? FUN_HIGH : FUN_LOW);
    relay_on = on;
}

/* Fast inline check used by the 20 ms DMA interrupt to see if relay is currently energized */
bool hal_relay_is_on(void)
{
    return relay_on;
}

/* Optional debug telemetry output over SWIO pin PD1 (disabled by default) */
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

/* 1 ms system heartbeat interrupt: advances g_ms clock and steps buzzer/LED patterns */
void SysTick_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void)
{
    SysTick->SR = 0;   /* Clear SysTick compare match flag */
    uint32_t next = SysTick->CMP + SYSTICK_TICKS_1MS;
    /* Guard against missed deadlines (e.g. during Flash erase) so SysTick never stalls */
    if ((int32_t)(next - SysTick->CNT) < 1000) {
        next = SysTick->CNT + SYSTICK_TICKS_1MS;
    }
    SysTick->CMP = next;
    g_ms++;
    alerts_tick_1ms();
}