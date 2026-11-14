#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "eeprom_ob.h"

#define NTC1_ENABLED 0
#define NTC2_ENABLED 0

typedef struct
{
    uint16_t lra_limit_ma;             // 800 - 5000
    uint16_t ocp_limit_ma;             // 300 - 4500
    uint16_t voltage_multiplier_x1000; // 3420 = 3.420 (calibration C-1)
    uint16_t current_multiplier_x1000; // 1880 = 1.880 (calibration C-2)
    uint8_t ovp_limit_div2;            // OVP volts / 2 (125 = 250 V)
    uint8_t uvp_limit;                 // 150 - 200
    uint8_t time_delay_mins;           // 2 - 10
    uint8_t ref_flow_delay_mins;       // 2 - 10
    uint8_t orp_time_limit_hours;      // 2 - 12
    uint8_t ovp_uvp_delay;             // 3 - 10
    uint8_t voltage_hysteresis;        // 10 - 40
    uint8_t lra_time;                  // 1 - 5
    uint8_t lra_retry_limit;           // 2 - 5
    uint8_t ocp_delay;                 // 2 - 10
    uint8_t ucp_limit_x10;             // 0 or 20 - 100 (x10 mA, 0 = Off)
    uint8_t min_current_threshold_x10; // 0 or 15 - 30  (x10 mA, 0 = Off)
    uint8_t otp_limit;                 // 70 - 90
    uint8_t otp_hysteresis;            // 5 - 20
    uint8_t flow_fault_retry_limit;    // 2 - 5
    uint8_t orp_rest_delay_mins;       // 20 - 60
} system_params_t;

_Static_assert(sizeof(system_params_t) <= 24, "system_params_t exceeds 24 bytes (need 1 magic + 1 CRC)");

#define OVP_LIMIT (params.ovp_limit_div2 * 2)
#define PARAM_POWER_SURGE_LIMIT 270
#define PARAM_BLACKOUT_LIMIT 140

extern system_params_t params;

bool params_load(void);
void params_save(void);
void params_reset_defaults(void);

#endif