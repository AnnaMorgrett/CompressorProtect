#include "sensors.h"
#include "board.h"
#include "params.h"
#include "sys_time.h"
#include "ch32fun.h"

// 64-bit integer square root (isqrt64) for precise RMS without float math
static uint32_t isqrt64(uint64_t val) {
    if (val == 0) return 0;
    uint64_t op  = val;
    uint64_t res = 0;
    uint64_t one = (uint64_t)1 << 62;

    while (one > op) one >>= 2;
    while (one != 0) {
        if (op >= res + one) {
            op  -= res + one;
            res  = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return (uint32_t)res;
}

static uint8_t pin_to_channel(uint8_t pin) {
    if (pin == PA2) return 0; // AIN0 (Voltage)
    if (pin == PA1) return 1; // AIN1 (Current)
    if (pin == PD6) return 6; // AIN6 (Buttons)
    return 0;
}

uint16_t sensors_read_adc(uint8_t pin) {
    uint8_t ch = pin_to_channel(pin);
    ADC1->RSQR3 = ch;
    ADC1->CTLR2 |= ADC_SWSTART;
    while (!(ADC1->STATR & ADC_EOC));
    return (uint16_t)(ADC1->RDATAR & 0x3FF);
}

void sensors_init(void) {
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOD;

    // Analog pins config
    funPinMode(PIN_VOLTAGE_SENS, GPIO_CNF_IN_ANALOG);
    funPinMode(PIN_CURRENT_SENS, GPIO_CNF_IN_ANALOG);
    funPinMode(PIN_BUTTONS,      GPIO_CNF_IN_ANALOG);

    // ADC setup: Prescaler /8 (48MHz / 8 = 6MHz), independent continuous/single
    RCC->CFGR0 &= ~RCC_ADCPRE;
    RCC->CFGR0 |= RCC_ADCPRE_DIV8;

    ADC1->CTLR1 = 0;
    ADC1->CTLR2 = ADC_ADON | ADC_EXTSEL; // External trigger mode = SWSTART
    Delay_Ms(2);

    // Calibrate ADC
    ADC1->CTLR2 |= ADC_RSTCAL;
    while (ADC1->CTLR2 & ADC_RSTCAL);
    ADC1->CTLR2 |= ADC_CAL;
    while (ADC1->CTLR2 & ADC_CAL);

    // Sampling time: 71.5 cycles for clean stability
    ADC1->SAMPTR2 = (7 << 0) | (7 << 3) | (7 << 18);
}

// True RMS with Anti-Crosstalk and 40 ms (2 full 50Hz cycles) sampling window
static uint32_t get_true_rms_q8(uint8_t pin) {
    // 1. Dummy read for MUX settling
    sensors_read_adc(pin);
    Delay_Us(100);
    sensors_read_adc(pin);
    Delay_Us(50);

    uint32_t start = micros();
    int32_t offset = sensors_read_adc(pin);
    int32_t sum = 0;
    uint64_t sum_sq = 0;
    uint32_t count = 0;

    // Sample 40,000 us = 40 ms (exact 2 cycles of 50 Hz mains)
    while ((micros() - start) < 40000UL) {
        int32_t val = (int32_t)sensors_read_adc(pin) - offset;
        sum += val;
        sum_sq += (uint64_t)((int64_t)val * val);
        count++;
    }

    if (count == 0) return 0;

    int32_t mean = sum / (int32_t)count;
    uint64_t mean_sq = sum_sq / (uint64_t)count;
    int64_t variance = (int64_t)mean_sq - ((int64_t)mean * mean);
    if (variance < 0) variance = 0;

    // Returns RMS scaled by 256 (Q8)
    return isqrt64((uint64_t)variance << 16);
}

uint16_t sensors_read_voltage_dv(void) {
    uint32_t rms_q8 = get_true_rms_q8(PIN_VOLTAGE_SENS);

    // Hard noise gate (< 5.0 ADC counts) -> instant 0 V
    if (rms_q8 < (5U << 8)) return 0;

    // Calibrated voltage: decivolts (0.1 V)
    uint32_t raw_dv = (rms_q8 * (uint32_t)params.voltage_multiplier_x1000) >> 12;

    static uint16_t filtered_dv = 0;
    if (filtered_dv == 0) {
        filtered_dv = (uint16_t)raw_dv;
        return filtered_dv;
    }

    // Adaptive filter from firmware_d:
    // If diff > 30 (3.0 V) -> instant update
    // Else -> 85% old + 15% new smoothing
    int32_t diff = (int32_t)raw_dv - (int32_t)filtered_dv;
    if (diff < 0) diff = -diff;

    if (diff > 30) {
        filtered_dv = (uint16_t)raw_dv;
    } else {
        filtered_dv = (uint16_t)(((uint32_t)filtered_dv * 85 + raw_dv * 15) / 100);
    }

    if (filtered_dv < 300) filtered_dv = 0; // Cutoff below 30 V
    return filtered_dv;
}

uint16_t sensors_read_current_ma(void) {
    uint32_t rms_q8 = get_true_rms_q8(PIN_CURRENT_SENS);

    // Scale to milliamps
    uint32_t raw_ma = (rms_q8 * (uint32_t)params.current_multiplier_x1000) >> 11;

    // Hard noise gate (< 100 mA) -> instant 0 mA
    if (raw_ma < 100) return 0;

    static uint16_t filtered_ma = 0;
    if (filtered_ma == 0) {
        filtered_ma = (uint16_t)raw_ma;
        return filtered_ma;
    }

    // Adaptive filter from firmware_d:
    // If diff > 100 mA -> instant update
    // Else -> 85% old + 15% new smoothing
    int32_t diff = (int32_t)raw_ma - (int32_t)filtered_ma;
    if (diff < 0) diff = -diff;

    if (diff > 100) {
        filtered_ma = (uint16_t)raw_ma;
    } else {
        filtered_ma = (uint16_t)(((uint32_t)filtered_ma * 85 + raw_ma * 15) / 100);
    }

    return filtered_ma;
}

int16_t sensors_read_temp1(void) {
#if NTC1_ENABLED
    // Placeholder if hardware expanded with external ADC mux
    return 25 + params.temp_offset;
#else
    return 0;
#endif
}

int16_t sensors_read_temp2(void) {
#if NTC2_ENABLED
    return 25 + params.temp_offset;
#else
    return 0;
#endif
}
