#ifndef UI_H
#define UI_H

#include "sys_time.h"

typedef enum {
    MODE_NORMAL_PROTECTION,
    MODE_CALIBRATION,
    MODE_SETTING
} operating_mode_t;

typedef enum {
    STATE_STARTUP_DELAY,
    STATE_RUNNING,
    STATE_FAULT,
    STATE_LOCKOUT,
    STATE_ORP_REST
} system_state_t;

typedef enum {
    FAULT_NONE,
    FAULT_SURGE,
    FAULT_BLACKOUT,
    FAULT_OVP,
    FAULT_UVP,
    FAULT_LRA,
    FAULT_OCP,
    FAULT_UCP,
    FAULT_OTP,
    FAULT_REF_FLOW
} fault_type_t;

typedef struct {
    uint8_t offset;
    uint8_t size;
    int16_t min_ui;
    int16_t max_ui;
    int16_t step_ui;
    int8_t  scale;
    bool    zero_off;
} param_def_t;

extern const param_def_t menu_table[20];

extern operating_mode_t current_mode;
extern system_state_t   current_state;
extern fault_type_t     current_fault;
extern uint32_t         state_timer;
extern uint8_t          lra_retry_count;
extern uint8_t          flow_retry_count;

extern uint16_t sys_volt_dv;
extern uint16_t sys_amp_ma;
extern int16_t  sys_temp1;
extern int16_t  sys_temp2;
extern char     current_display_str[5];

void    ui_init(void);
void    ui_update_display(uint16_t current_v, uint16_t current_ma);
void    ui_show_serial_menu_display(void);
void    ui_handle_setting_mode(void);
void    ui_handle_calibration_mode(void);
int32_t ui_get_param_val(uint8_t idx);
void    ui_set_param_val(uint8_t idx, int32_t ui);

extern void system_button_chirp(void);
extern void system_reboot(void);

#endif