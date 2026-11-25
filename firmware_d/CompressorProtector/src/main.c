/* [#10] main.c - System boot orchestrator, startup mode selector, and main superloop. */
#include "system.h"

SystemConfig_t g_cfg;   /* Global configuration & calibration parameters (RAM mirror of Flash) */
SensorData_t   g_sens;  /* Global real-time AC voltage, current, and supply telemetry */

int main(void)
{
    /* 1. Initialize 48 MHz clock, fail-safe LOW outputs, SysTick (1 ms), and Watchdog (1.0 s) */
    SystemInit();
    hal_init();

    /* 2. Calibrate ADC, measure real VDD, load Flash settings, and start 6400 Hz DMA sampling */
    adc_init();
    adc_service();
    nvm_load();
    display_init();
    input_init();
    adc_start();

    /* 3. Wait 200 ms while showing "----" so AC-coupling capacitors settle and first RMS blocks arrive */
    uint32_t t0 = g_ms;
    while ((uint32_t)(g_ms - t0) < 200UL) {
        hal_wdg_kick();
        display_text("----");
        display_flush();
    }

    /* 4. Check boot mode priority:
     *    - Priority 1: PC6 shorted to GND -> Calibration Mode ("C-0")
     *    - Priority 2: 'OK' button held   -> Settings Menu ("Sett")
     *    - Default:                       -> Normal Protection Mode (Mode 1) */
    AppMode_t mode = MODE_PROTECT;
    if (input_cal_active())             mode = MODE_CAL;
    else if (input_menu_held_at_boot()) mode = MODE_SETTINGS;

    if (mode == MODE_CAL)      mode_cal();      /* Halts in "donE" when finished */
    if (mode == MODE_SETTINGS) mode_settings(); /* Halts in "donE" when finished */

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