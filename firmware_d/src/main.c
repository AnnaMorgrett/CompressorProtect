/* [#10] main.c - orchestrator: init, boot priority CAL -> Sett -> Normal, superloop + IWDG. */
#include "system.h"

SystemConfig_t g_cfg;
SensorData_t   g_sens;

int main(void)
{
    SystemInit();
    hal_init();

    adc_init();
    adc_service();
    nvm_load();
    display_init();
    input_init();
    adc_start();

    uint32_t t0 = g_ms;
    while ((uint32_t)(g_ms - t0) < 200UL) {
        hal_wdg_kick();
        display_text("----");
        display_flush();
    }

    AppMode_t mode = MODE_PROTECT;
    if (input_cal_active())             mode = MODE_CAL;
    else if (input_menu_held_at_boot()) mode = MODE_SETTINGS;

    if (mode == MODE_CAL)      mode_cal();
    if (mode == MODE_SETTINGS) mode_settings();

    fsm_init();
    for (;;) {
        hal_wdg_kick();
        adc_service();
        input_poll();
        fsm_step();
        display_flush();
        hal_log();
    }
}