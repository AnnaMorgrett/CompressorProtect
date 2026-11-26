/* ============================ BEGINNER GUIDE: hal_sys.c ============================
 * WHAT THIS FILE IS: the "basic hardware" file. It
 *   - sets every pin to a safe state at power-on (relay OFF!),
 *   - runs the 1-millisecond clock (g_ms) that all timing in the program depends on,
 *   - runs the watchdog that reboots the chip if the program freezes,
 *   - is the only place that switches the relay.
 * ===================================================================================== */

/* [#03] hal_sys.c - Fail-safe GPIO initialization, 1 ms SysTick timer, IWDG watchdog, and relay driver. */
#include "system.h"
#include <stdio.h>

/* g_ms is the program's CLOCK: it counts milliseconds since power-on (1000 = 1 second).
 * Used everywhere for "has 500 ms passed yet?" checks. The trick is to always subtract:
 *     if ((uint32_t)(g_ms - start) >= 500) ...
 * which keeps working even when the counter wraps around after 49 days. */
volatile uint32_t g_ms = 0;         /* System uptime in milliseconds (rolls over after ~49.7 days) */
static volatile bool relay_on = false; /* Shadow state of relay pin so ISR can check status fast */
static uint32_t wd_blocks_seen = 0, wd_progress_ms = 0; /* DSP liveness tracking for watchdog */

/* Drive a GPIO output LOW before enabling its push-pull driver to avoid power-on glitches */
/* Make a pin an OUTPUT, but first write LOW so it never glitches HIGH for an instant.
 * Important for the RELAY pin: a glitch could click the compressor on at the wrong time. */
static void pin_out_low(uint32_t pin)
{
    funDigitalWrite(pin, FUN_LOW);
    funPinMode(pin, GPIO_CFGLR_OUT_10Mhz_PP);
}

/* Configure an unused or jumper input with internal ~40 kOhm pull-up enabled */
/* Make a pin an INPUT with a weak internal resistor to 3.3/5 V ("pull-up"). Unconnected pins would otherwise
 * float and pick up noise; the pull-up keeps them calmly HIGH. A jumper to GND then reads as LOW. */
static void pin_in_pullup(uint32_t pin)
{
    funPinMode(pin, GPIO_CFGLR_IN_PUPD);
    funDigitalWrite(pin, FUN_HIGH);
}

/* hal_init = "hardware abstraction layer: initialise". Sets every pin to a SAFE state and starts the timers. */
void hal_init(void)
{
    /* INTSYSCR (0x804) = 0 turns HPE (hardware stacking + nesting) OFF. ch32fun documents that HPE does not
     * preserve s0/s1, so with HPE off GCC's own interrupt prologue saves everything safely.
     * Side effect: no interrupt nesting, so DMA ISR cannot preempt SysTick (both ISRs are short). */
    cpu_hpe_off();

    /* Peripherals (GPIO ports, ADC...) are asleep to save power. Writing the "clock enable" bit wakes each one up. */
    /* Enable peripheral clocks for GPIO Ports A, C, D and Alternate Function I/O */
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC |
                      RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO;

    /* Clear PA12_RM (bit 15) so PA1 and PA2 act as GPIO/ADC pins instead of external crystal pins */
    AFIO->PCFR1 &= ~REG_AFIO_PA12_REMAP;

    /* SAFETY FIRST: relay, buzzer and LED all start OFF. If the chip resets in the middle of the night,
     * the compressor stays OFF until the protection logic decides it is safe to start. */
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

    /* SysTick is a built-in stopwatch. We set it to interrupt every 1 ms. Each interrupt adds 1 to g_ms
     * (see SysTick_Handler at the bottom of this file). 48,000 ticks of a 48 MHz clock = exactly 1 ms. */
    /* 4. Configure SysTick timer to fire an interrupt every 1.000 ms (48,000 ticks @ 48 MHz) */
    SysTick->CTLR = 0;
    SysTick->SR   = 0;
    SysTick->CMP  = SysTick->CNT + SYSTICK_TICKS_1MS;
    SysTick->CTLR = REG_SYSTICK_ENABLE | REG_SYSTICK_CLK_HCLK | REG_SYSTICK_IRQ_ENABLE;
    NVIC_EnableIRQ(SysTick_IRQn);

    /* WATCHDOG = a countdown that resets the whole chip when it reaches zero. The program must "kick" (restart)
     * it regularly. If the program freezes, nobody kicks it, and the chip reboots with the relay OFF. */
    /* 5. Start Independent Watchdog (IWDG) with 1.0 s timeout.
     * If firmware ever hangs or enters an infinite loop without calling hal_wdg_kick(),
     * the MCU automatically resets and forces PIN_RELAY LOW. */
    wd_progress_ms = g_ms;   /* Grace period for the first RMS block to arrive */
    IWDG->CTLR = REG_IWDG_KEY_UNLOCK;
    IWDG->PSCR = IWDG_PRESCALER_CODE;
    IWDG->RLDR = IWDG_RELOAD_TICKS;
    { uint32_t g = 100000UL; while ((IWDG->STATR & REG_IWDG_BUSY_MASK) && --g) {} }  /* Wait PVU/RVU: register update done */
    IWDG->CTLR = REG_IWDG_KEY_RELOAD;
    IWDG->CTLR = REG_IWDG_KEY_START;

#if ENABLE_SWIO_LOG
    SetupDebugPrintf();
#endif
}

/* Track whether the ADC -> DMA -> RMS chain keeps producing blocks */
/* Remember WHEN the last fresh measurement block arrived. Used to notice if measuring has silently died. */
static void dsp_track(void)
{
    uint32_t b = g_sens.blocks;
    if (b != wd_blocks_seen) { wd_blocks_seen = b; wd_progress_ms = g_ms; }
}

/* Returns true while measurements keep arriving (a new one every 20 ms). If nothing new for 100 ms, returns false
 * and the state machine switches the relay off ("E-SE" = sensor error). */
bool hal_dsp_alive(void)
{
    dsp_track();
    return (uint32_t)(g_ms - wd_progress_ms) < DSP_ALIVE_MS;
}

/* IMPORTANT IDEA: we only kick the watchdog if the measuring chain is still alive.
 * Otherwise a frozen ADC would be hidden by a healthy-looking main loop, and the relay could stay ON unprotected. */
/* Feed the hardware watchdog ONLY while sensing is alive. Protection trips run inside the DMA ISR,
 * so a dead ADC/TIM1/DMA must never be hidden by a happy main loop: if no RMS block arrives for
 * WDG_DSP_STALL_MS the dog is starved and the MCU resets (relay pin returns to its safe state). */
void hal_wdg_kick(void)
{
    dsp_track();
    if ((uint32_t)(g_ms - wd_progress_ms) < WDG_DSP_STALL_MS) IWDG->CTLR = REG_IWDG_KEY_RELOAD;
}

/* Turn compressor relay ON (true) or OFF (false) and update fast shadow variable */
/* The ONLY function that touches the relay pin, so there is exactly one place to look when debugging. */
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

/* An INTERRUPT is an automatic pause: the chip stops the main loop, runs this short function, then continues
 * exactly where it left off. This one runs every 1 ms. Keep it SHORT and never wait inside it. */
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