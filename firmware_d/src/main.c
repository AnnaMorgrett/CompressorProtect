/* ============================ BEGINNER GUIDE: main.c ============================
 * WHAT THIS FILE IS: the "front door" of the whole program.
 *
 * THE BIG PICTURE (read this first!):
 *   The chip watches the 220 V mains that feeds a refrigerator/AC compressor.
 *   If the voltage is too LOW, too HIGH, or the compressor draws too much current,
 *   the chip switches a RELAY off so the compressor is not damaged.
 *   After a trip it waits several minutes before switching on again (compressors hate quick restarts).
 *
 * TWO "WORKERS" run at the same time:
 *   1) The MAIN LOOP (this file)  - slow, relaxed jobs: buttons, display, countdown.
 *   2) The INTERRUPTS             - urgent jobs that automatically pause the main loop:
 *        - every 20 ms: measure volts/amps and cut the relay if unsafe (adc_dsp.c + fsm_protect.c)
 *        - every  1 ms: keep the clock, blink the LED, beep the buzzer (hal_sys.c, input_alerts.c)
 *
 * FILE MAP:  config.h (settings numbers) -> system.h (shared types) -> hal_sys.c (hardware basics)
 *            adc_dsp.c (measuring) -> fsm_protect.c (decisions) -> input_alerts.c (buttons/LED/buzzer)
 *            display_tm1637.c (4-digit display) -> nvm_flash.c (saving) -> menu_cal.c (menus)
 * ================================================================================ */

/* [#10] main.c - System boot orchestrator, startup mode selector, and main superloop. */
#include "system.h"

/* "g_" at the start of a name means GLOBAL variable: every .c file can see it.
 * g_cfg = the user's settings (limits, delays, calibration). It lives in RAM while running,
 * and a copy is stored in Flash so it survives power-off. */
SystemConfig_t g_cfg;   /* Global configuration & calibration parameters (RAM mirror of Flash) */
/* g_sens = the latest measurements (voltage, current...). The ADC interrupt writes it, the main loop reads it. */
SensorData_t   g_sens;  /* Global real-time AC voltage, current, and supply telemetry */

/* main() is where the chip starts after power-on. It should never return: the "for (;;)" loop at the bottom
 * runs forever. If it ever stops being fed to the watchdog, the chip resets itself (safety). */
int main(void)
{
    /* STEP 1 in plain words: set up the chip's clock, make sure the RELAY IS OFF, start the 1 ms timer,
     * and start the "watchdog" (a dog that resets the chip if the program freezes). */
    /* 1. Initialize 48 MHz clock, fail-safe LOW outputs, SysTick (1 ms), and Watchdog (1.0 s) */
    SystemInit();
    hal_init();

    /* STEP 2 in plain words: get the measuring hardware ready, read the saved settings from Flash,
     * and start sampling the mains 6400 times per second. */
    /* 2. Calibrate ADC, measure real VDD, load Flash settings, and start 6400 Hz DMA sampling */
    adc_init();
    adc_service();
    nvm_load();
    display_init();
    input_init();
    adc_start();

    /* STEP 3 in plain words: show "----" for a moment. The sensor circuit needs ~0.1 s to settle,
     * and we do not want to judge the mains voltage before we have real measurements. */
    /* 3. Wait 200 ms while showing "----" so AC-coupling capacitors settle and first RMS blocks arrive */
    uint32_t t0 = g_ms;
    while ((uint32_t)(g_ms - t0) < 200UL) {
        hal_wdg_kick();
        display_text("----");
        display_flush();
    }

    /* STEP 4 in plain words: the chip has 3 "personalities". Which one runs depends on what you hold at power-on:
     *   - CAL jumper shorted      -> Calibration (factory, to teach the chip the real volts/amps)
     *   - OK button held          -> Settings menu (change limits)
     *   - nothing                 -> Normal protection (the everyday job) */
    /* 4. Check boot mode priority:
     *    - Priority 1: PC6 shorted to GND -> Calibration Mode ("C-0")
     *    - Priority 2: 'OK' button held   -> Settings Menu ("Sett")
     *    - Default:                       -> Normal Protection Mode (Mode 1) */
    AppMode_t mode = MODE_PROTECT;
    if (input_cal_active())             mode = MODE_CAL;
    else if (input_menu_held_at_boot()) mode = MODE_SETTINGS;

    /* The two special modes never come back: after saving they stop on "donE" and wait for a power cycle. */
    if (mode == MODE_CAL)      mode_cal();      /* Halts in "donE" when finished */
    if (mode == MODE_SETTINGS) mode_settings(); /* Halts in "donE" when finished */

    /* STEP 5 in plain words: start the protection brain (a "state machine", see fsm_protect.c) and then repeat
     * the same 6 small jobs forever. Each job is quick, so the loop runs thousands of times per second. */
    /* 5. Enter Normal Protection State Machine and run superloop forever */
    fsm_init();
    for (;;) {
        hal_wdg_kick();     /* Reset 1.0 s hardware watchdog */
        adc_service();      /* Refresh VDD from internal 1.20 V reference every 500 ms */
        input_poll();       /* Scan 3-button resistor ladder every 5 ms */
        fsm_step();         /* Advance protection state machine and update display buffer */
        display_flush();    /* Push changed digits to TM1637 LED display */
        hal_log();          /* Optional SWIO telemetry print */
    }
}