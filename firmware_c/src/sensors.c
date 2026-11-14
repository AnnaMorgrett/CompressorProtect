#include "sensors.h"
#include "serial_cli.h"

uint16_t sensors_read_adc(uint8_t pin) {
    uint8_t ch = 0;
    if      (pin == PA2) ch = 0; // AIN0
    else if (pin == PA1) ch = 1; // AIN1
    else if (pin == PD6) ch = 6; // AIN6
    else return 0;

    ADC1->RSQR3 = ch;
    ADC1->CTLR2 |= (1U << 0) | (1U << 22); // ADON + SWSTART
    while (!(ADC1->STATR & (1U << 1)));    // Wait for EOC
    return (uint16_t)(ADC1->RDATAR & 0x03FF);
}

void sensors_init(void) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOD | RCC_APB2Periph_ADC1;
    RCC->CFGR0 = (RCC->CFGR0 & ~(0x1FU << 11)) | (0x02U << 11); // PCLK2 / 6 = 8 MHz

    // PA1 (AIN1), PA2 (AIN0), PD6 (AIN6) -> Analog Input (CNF=00, MODE=00)
    GPIOA->CFGLR &= ~((0x0FU << (1 * 4)) | (0x0FU << (2 * 4)));
    GPIOD->CFGLR &= ~(0x0FU << (6 * 4));

    RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
    RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;

    ADC1->CTLR2 = (1U << 20) | (0x07U << 17) | (1U << 0); // EXTTRIG + SWSTART + ADON
    ADC1->SAMPTR2 = (0x03U << (0 * 3)) | (0x03U << (1 * 3)) | (0x03U << (6 * 3));

    ADC1->CTLR2 |= (1U << 3); // RSTCAL
    while (ADC1->CTLR2 & (1U << 3));
    ADC1->CTLR2 |= (1U << 2); // CAL
    while (ADC1->CTLR2 & (1U << 2));
}

// 64-bit integer square root for Q8 fixed-point sub-LSB TrueRMS
static uint32_t isqrt64(uint64_t n) {
    uint64_t c = 1ULL << 62;
    uint64_t g = 0;
    while (c > n) c >>= 2;
    while (c != 0) {
        if (n >= g + c) {
            n -= g + c;
            g = (g >> 1) + c;
        } else {
            g >>= 1;
        }
        c >>= 2;
    }
    return (uint32_t)g;
}

// 20,000 us = 1 full 50 Hz AC cycle (2x faster anomaly response than 40 ms)
// Outputs Q8 fixed-point RMS (actual_rms_adc * 256) for sub-LSB precision
static bool get_true_rms_q8(uint8_t pin, uint32_t *out_rms_q8) {
    sensors_read_adc(pin);
    Delay_Us(60);
    sensors_read_adc(pin);
    Delay_Us(40);

    uint32_t start_us = micros();
    const int32_t offset = 512;
    int32_t  sum    = 0;
    uint32_t sum_sq = 0;
    uint32_t count  = 0;

    while ((micros() - start_us) < 20000UL) {
        if (serial_cli_rx_incoming()) return false;
        int32_t val = (int32_t)sensors_read_adc(pin) - offset;
        sum += val;
        sum_sq += (uint32_t)(val * val);
        count++;
    }

    if (count == 0) {
        *out_rms_q8 = 0;
        return true;
    }

    int64_t  mean_q8      = ((int64_t)sum << 8) / (int32_t)count;
    uint64_t mean_sq_q16  = ((uint64_t)sum_sq << 16) / count;
    int64_t  variance_q16 = (int64_t)mean_sq_q16 - (mean_q8 * mean_q8);
    if (variance_q16 < 0) variance_q16 = 0;

    *out_rms_q8 = isqrt64((uint64_t)variance_q16);
    return true;
}

// Returns voltage in decivolts (0.1 V resolution, e.g., 2205 = 220.5 V)
uint16_t sensors_read_voltage_dv(void) {
    static uint16_t filtered_dv = 0;
    uint32_t rms_q8 = 0;

    if (!get_true_rms_q8(PIN_VOLTAGE_SENS, &rms_q8)) {
        return filtered_dv;
    }

    // (rms_adc * 256) * multiplier_x1000 / 25600 -> decivolts (V * 10)
    uint16_t true_dv = (uint16_t)(((uint64_t)rms_q8 * params.voltage_multiplier_x1000) / 25600ULL);
    if (true_dv < 50) { // < 5.0 V hard noise gate
        filtered_dv = 0;
        return 0;
    }

    if (filtered_dv == 0) {
        filtered_dv = true_dv;
        return filtered_dv;
    }

    int16_t diff = (int16_t)true_dv - (int16_t)filtered_dv;
    if (diff < 0) diff = -diff;

    if (diff > 30) filtered_dv = true_dv; // > 3.0 V step: bypass filter instantly
    else           filtered_dv = (uint16_t)((filtered_dv * 85UL + true_dv * 15UL) / 100UL);

    if (filtered_dv < 300) filtered_dv = 0; // < 30.0 V cutoff
    return filtered_dv;
}

// Returns current in milliamps (1 mA resolution with sub-LSB precision)
uint16_t sensors_read_current_ma(void) {
    static uint16_t filtered_ma = 0;
    uint32_t rms_q8 = 0;

    if (!get_true_rms_q8(PIN_CURRENT_SENS, &rms_q8)) {
        return filtered_ma;
    }

    // (rms_adc * 256) * multiplier_x1000 / 25600 -> milliamps (mA)
    uint16_t true_ma = (uint16_t)(((uint64_t)rms_q8 * params.current_multiplier_x1000) / 25600ULL);

    if (true_ma < 80) { // < 80 mA hard noise gate
        filtered_ma = 0;
        return 0;
    }
    if (filtered_ma == 0) {
        filtered_ma = true_ma;
        return filtered_ma;
    }

    int16_t diff = (int16_t)true_ma - (int16_t)filtered_ma;
    if (diff < 0) diff = -diff;

    if (diff > 100) filtered_ma = true_ma; // > 100 mA surge/LRA: bypass filter instantly
    else            filtered_ma = (uint16_t)((filtered_ma * 85UL + true_ma * 15UL) / 100UL);

    return filtered_ma;
}

int16_t sensors_read_temp1(void) { return 0; }
int16_t sensors_read_temp2(void) { return 0; }