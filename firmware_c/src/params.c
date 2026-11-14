#include "params.h"

system_params_t params;

#define EEPROM_MAGIC_ADDR 0
#define EEPROM_MAGIC_VAL  0xA8
#define EEPROM_DATA_ADDR  1
#define EEPROM_CRC_ADDR   (EEPROM_DATA_ADDR + sizeof(system_params_t))

static uint8_t compute_params_crc(const system_params_t *p) {
    const uint8_t *bytes = (const uint8_t *)p;
    uint8_t crc = 0x5A;
    for (size_t i = 0; i < sizeof(system_params_t); i++) {
        crc ^= bytes[i];
        crc = (uint8_t)((crc << 1) | (crc >> 7));
    }
    return crc;
}

static bool are_params_valid(const system_params_t *p) {
    if (p->time_delay_mins < 2 || p->time_delay_mins > 10) return false;
    if (p->ovp_limit_div2 < 115 || p->ovp_limit_div2 > 130) return false;
    if (p->uvp_limit < 150 || p->uvp_limit > 200) return false;
    if (p->lra_limit_ma < 800 || p->lra_limit_ma > 5000) return false;
    if (p->ocp_limit_ma < 300 || p->ocp_limit_ma > 4500) return false;
    if (p->voltage_multiplier_x1000 < 1 || p->voltage_multiplier_x1000 > 5000) return false;
    if (p->current_multiplier_x1000 < 1 || p->current_multiplier_x1000 > 5000) return false;
    return true;
}

void params_reset_defaults(void) {
    params.time_delay_mins           = 5;
    params.ref_flow_delay_mins       = 5;
    params.orp_time_limit_hours      = 7;
    params.voltage_multiplier_x1000  = 3420;
    params.current_multiplier_x1000  = 1880;
    params.ovp_limit_div2            = 125; // 250 V
    params.uvp_limit                 = 180;
    params.ovp_uvp_delay             = 5;
    params.voltage_hysteresis        = 10;
    params.lra_limit_ma              = 1500;
    params.lra_time                  = 2;
    params.lra_retry_limit           = 3;
    params.ocp_limit_ma              = 800;
    params.ocp_delay                 = 4;
    params.ucp_limit_x10             = 0;
    params.min_current_threshold_x10 = 25;
    params.otp_limit                 = 85;
    params.otp_hysteresis            = 10;
    params.flow_fault_retry_limit    = 5;
    params.orp_rest_delay_mins       = 30;
    params_save();
}

bool params_load(void) {
    eeprom_begin();
    if (eeprom_read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VAL) {
        system_params_t temp;
        eeprom_get(EEPROM_DATA_ADDR, &temp, sizeof(temp));
        uint8_t stored_crc = eeprom_read(EEPROM_CRC_ADDR);
        if (stored_crc == compute_params_crc(&temp) && are_params_valid(&temp)) {
            params = temp;
            return true;
        }
    }
    params_reset_defaults();
    return false;
}

void params_save(void) {
    uint8_t crc = compute_params_crc(&params);
    eeprom_put(EEPROM_DATA_ADDR, &params, sizeof(params));
    eeprom_write(EEPROM_CRC_ADDR, crc);
    eeprom_update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
}