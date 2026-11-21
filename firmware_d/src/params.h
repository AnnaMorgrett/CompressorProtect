#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>
#include <stdbool.h>
#include "eeprom_ob.h"

// Hardware NTC enable switches (CH32V003 SOP-16 has no ADC on PC1/PC2)
#define NTC1_ENABLED 0
#define NTC2_ENABLED 0

typedef struct {
    uint16_t lra_limit_ma;              // 800 - 5000 mA (S-06)
    uint16_t ocp_limit_ma;              // 300 - 4500 mA (S-09)
    uint16_t voltage_multiplier_x1000;  // 1 - 5000 (C-1)
    uint16_t current_multiplier_x1000;  // 1 - 5000 (C-2)

    uint8_t  ovp_limit_div2;            // 230 - 260 / 2 (S-02, 125 = 250V)
    uint8_t  uvp_limit;                 // 150 - 200 V (S-03)
    uint8_t  time_delay_mins       : 4; // 2 - 10 min (S-01)
    uint8_t  display_mode          : 4; // 1 - 5 (Running display mode, def 5)

    uint8_t  ref_flow_delay_mins;       // 2 - 10 min (S-17)
    uint8_t  orp_time_limit_hours;      // 2 - 12 hours (S-19)
    uint8_t  ocp_delay             : 4; // 2 - 10 sec (S-10)
    uint8_t  ovp_uvp_delay         : 4; // 3 - 10 sec (S-04)

    uint8_t  voltage_hysteresis;        // 10 - 40 V (S-05)
    uint8_t  lra_time              : 4; // 1 - 5 sec (S-07)
    uint8_t  lra_retry_limit       : 4; // 2 - 5 retries (S-08)

    uint8_t  ucp_limit_x10;             // 0 or 20 - 100 (x10 mA, S-11)
    uint8_t  min_current_threshold_x10; // 0 or 15 - 30 (x10 mA, S-12)
    uint8_t  otp_limit;                 // 70 - 90 C (S-14)
    uint8_t  otp_hysteresis;            // 5 - 20 C (S-15)
    uint8_t  flow_fault_retry_limit;    // 2 - 5 retries (S-18)
    uint8_t  orp_rest_delay_mins;       // 20 - 60 min (S-20)
    int8_t   temp_offset;               // -20 .. +20 C (C-3)
} system_params_t;

_Static_assert(sizeof(system_params_t) <= 24, "system_params_t exceeds 24 bytes (need 1 magic + 1 CRC)");

#define OVP_LIMIT               (params.ovp_limit_div2 * 2)
#define PARAM_POWER_SURGE_LIMIT 270
#define PARAM_BLACKOUT_LIMIT    140

extern system_params_t params;

bool params_load(void);
void params_save(void);
void params_reset_defaults(void);
void params_reset_calibration(void);

#endif
