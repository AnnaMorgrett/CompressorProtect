#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MODE_NORMAL_PROTECTION = 0,
    MODE_CALIBRATION       = 1,
    MODE_SETTING           = 2
} operating_mode_t;

typedef enum {
    STATE_STARTUP_DELAY = 0,
    STATE_RUNNING       = 1,
    STATE_FAULT         = 2,
    STATE_LOCKOUT       = 3,
    STATE_ORP_REST      = 4
} system_state_t;

typedef enum {
    FAULT_NONE     = 0,
    FAULT_SURGE    = 1,
    FAULT_BLACKOUT = 2,
    FAULT_OVP      = 3,
    FAULT_UVP      = 4,
    FAULT_LRA      = 5,
    FAULT_OCP      = 6,
    FAULT_UCP      = 7,
    FAULT_OTP      = 8,
    FAULT_REF_FLOW = 9
} fault_type_t;

extern operating_mode_t current_mode;
extern system_state_t   current_state;
extern fault_type_t     current_fault;
extern uint32_t         state_timer;
extern uint8_t          lra_retry_count;
extern uint8_t          flow_retry_count;
extern uint16_t         sys_volt_dv;
extern uint16_t         sys_amp_ma;
extern int16_t          sys_temp1;
extern int16_t          sys_temp2;
extern char             current_display_str[5];

void    ui_init(void);
void    ui_update_display(uint16_t current_v, uint16_t current_ma);
void    ui_handle_setting_mode(void);
void    ui_handle_calibration_mode(void);
void    ui_show_serial_menu_display(void);
void    system_reboot(void);
void    system_button_chirp(void);

int32_t ui_get_param_val(uint8_t idx);
void    ui_set_param_val(uint8_t idx, int32_t val);

#endif
