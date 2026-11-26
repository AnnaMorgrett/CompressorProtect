/* ============================ BEGINNER GUIDE: adc_dsp.c ============================
 * WHAT THIS FILE IS: the "measuring instrument" of the product.
 *   ADC = Analog-to-Digital Converter: turns a voltage on a pin into a number 0..1023.
 *   DSP = Digital Signal Processing: the math that turns many snapshots into volts and amps.
 *
 * HOW A MEASUREMENT HAPPENS (no CPU involved until the last step):
 *   Timer 1 ticks 6400 times/second --> ADC takes a snapshot of VOLTAGE then CURRENT
 *   --> DMA robot stores the numbers in an array --> after 128 snapshots (20 ms = one 50 Hz wave)
 *   an INTERRUPT calls process_block(), which does the math and calls the protection logic.
 *
 * RESULT: g_sens always holds fresh volts and amps, updated 50 times per second.
 * ==================================================================================== */

/* [#05] adc_dsp.c - Hardware-timed 6400 Hz ADC acquisition + True-RMS Digital Signal Processing.
 * Pipeline: TIM1 TRGO (6400 Hz) -> ADC1 Scan (VAC, CT) -> Circular DMA1 Ping-Pong Buffer.
 * Every 20 ms (128 samples = 1 full 50 Hz cycle), DMA fires an interrupt to compute single-cycle
 * True RMS, run instant 20 ms fault protection (fsm_on_block), and update smoothed UI values. */
#include "system.h"

/* The ADC drops its results into a memory array by itself (DMA, below). We make the array twice as big as one
 * 20 ms block ("ping-pong"): while the hardware fills one half, the math reads the other half. */
#define BUF_HALF_WORDS     (SAMPLES_PER_BLOCK * ADC_NCH) /* 128 * 2 = 256 half-words per 20 ms half-buffer */

/* Timer 1 counts 48,000,000 ticks per second. Counting up to 7500 then restarting = 6400 restarts per second.
 * Each restart tells the ADC "take a snapshot NOW", so the sample spacing is perfectly even (no software jitter). */
#define TIM1_PERIOD        (SYSCLK_HZ / SAMPLE_RATE_HZ)   /* 48,000,000 / 6400 = 7500 timer ticks */
/* For the buttons we sometimes sneak in an extra ADC reading. It must happen in the quiet gap between two
 * scheduled snapshots, so we only do it while the timer count is between these two numbers. */
#define INJ_SAFE_MIN       900U          /* Safe window start after TIM1 trigger (regular scan finished) */
#define INJ_SAFE_MAX       6500U         /* Safe window end before next TIM1 trigger (at 7500) */

/* Double-buffered (ping-pong) DMA array:
 * First half [0..255] fills while second half [256..511] is processed, and vice versa. */
/* "DMA" = a tiny helper robot inside the chip that copies each ADC result into this array without bothering the CPU. */
static volatile uint16_t dma_buf[2U * BUF_HALF_WORDS] __attribute__((aligned(4)));
/* After power-on the sensor's DC level needs ~100 ms to settle, so the first 5 blocks are thrown away. */
static uint8_t warmup = ADC_WARMUP_BLOCKS; /* Counter to discard initial DC-bias settling cycles */

/* Display smoothing filter state (EMA in Q4 fixed-point + Schmitt deadband) */
/* State for the display smoother ("EMA" = exponential moving average = each new value only nudges the old one). */
typedef struct { int32_t ema_q4; uint16_t shown; bool init; } Filter_t;
static Filter_t filt_v, filt_a;

/* Fast 32-bit integer square root (bit-by-bit restoring algorithm, ~16 iterations, no division) */
/* Square root WITHOUT using floating point or division (this chip has neither). Needed for RMS. */
static uint32_t isqrt32(uint32_t n)
{
    uint32_t root = 0, bit = 1UL << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
        else                 { root >>= 1; }
        bit >>= 2;
    }
    return root;
}

/* Per-block running sums. Kept for the last WIN_BLOCKS blocks so a 4-cycle (80 ms) window can be formed
 * without touching the samples again. A longer window shrinks the error caused by the window not being
 * exactly N AC cycles (HSI is +-2.2 % over temperature and mains frequency also wanders). */
/* RMS CRASH COURSE: mains is a wave that goes + and -, so its plain average is 0. RMS ("root mean square") is the
 * "effective" value: square every sample, average the squares, take the square root. 220 V RMS heats a lamp
 * the same as 220 V DC. To do that cheaply we only need two running totals per block:
 *    sum   = x1 + x2 + ... (for the average / DC level)
 *    sumsq = x1^2 + x2^2 + ... (for the squares) */
typedef struct { uint32_t sum, sumsq; } BlkSum_t;
static BlkSum_t ring[ADC_NCH][WIN_BLOCKS];
static uint8_t  ring_idx, ring_fill;

/* Add up the 128 snapshots of ONE channel inside one 20 ms block. "offset" picks the channel: 0 = voltage,
 * 1 = current (the DMA stores them interleaved: V, I, V, I, ...). */
static BlkSum_t block_sums(const volatile uint16_t *blk, uint8_t offset)
{
    BlkSum_t r = { 0, 0 };
    for (uint32_t i = 0; i < SAMPLES_PER_BLOCK; i++) {
        uint32_t x = blk[i * ADC_NCH + offset];
        r.sum   += x;
        r.sumsq += x * x;
    }
    return r;
}

/* Variance in Q12 (= RMS_counts^2 * 4096) from sums over n = 2^k samples:
 *   N*sum(x^2) - (sum x)^2 = N^2 * variance  ->  Q12 value = that >> (2k - 12).
 * DC bias is removed mathematically, no separate midpoint needed. */
/* Turn the two totals into the "variance" (mean square with the DC level removed). */
static uint32_t ms_q12(uint32_t sum, uint32_t sumsq, uint32_t n, uint8_t shift)
{
    uint64_t num = (uint64_t)sumsq * n - (uint64_t)sum * sum;
    return (uint32_t)(num >> shift);
}

/* Convert raw ADC RMS (Q6) into physical units (0.1 V or 0.01 A):
 * 1. Subtract zero-input noise floor in quadrature: sqrt(rms^2 - zero^2)
 * 2. Scale ADC counts to pin millivolts using real VREFINT-measured VDD
 * 3. Multiply by hardware front-end gain (Q8) and user calibration trim (Q12) */
/* Convert "ADC counts" into real units (0.1 V or 0.01 A). Steps in plain words:
 *  1) remove the electronic noise the sensor shows when nothing is connected (zero calibration)
 *  2) counts -> millivolts at the chip pin (using the real supply voltage VDD as the ruler)
 *  3) pin millivolts -> mains volts/amps using the circuit gain and your calibration trim */
static uint16_t to_units(uint32_t rms_q6, uint16_t zero_q6, uint16_t trim_q12, uint32_t gain_q8)
{
    uint32_t r2 = rms_q6 * rms_q6;
    uint32_t z2 = (uint32_t)zero_q6 * zero_q6;
    uint32_t r  = (r2 > z2) ? isqrt32(r2 - z2) : 0U;
    uint32_t mv_q6 = (r * g_sens.vdd_mv) >> 10;          /* Divide by 1024 (10-bit ADC full scale) */
    uint64_t u = (uint64_t)mv_q6 * gain_q8 * trim_q12;   /* Q6 * Q8 * Q12 = Q26 fractional bits */
    u = (u + (1ULL << 25)) >> 26;                        /* Round to nearest integer and shift down */
    return (u > 65535ULL) ? 65535U : (uint16_t)u;
}

/* Exponential Moving Average (EMA, alpha = 1/8) + Schmitt deadband for rock-steady UI numbers.
 * Snaps immediately if input jumps more than `snap` (e.g. 2.0 V) so big changes feel instant. */
/* Make the DISPLAY number steady. NOT used for safety trips (those use the raw fast value). */
static uint16_t filter_update(Filter_t *f, uint16_t raw, uint16_t snap, uint16_t dead, volatile uint16_t *avg_out)
{
    int32_t x = (int32_t)raw * 16;   /* Scale to Q4 for fractional EMA precision */
    int32_t diff = x - f->ema_q4;
    if (diff < 0) diff = -diff;

    if (!f->init || diff > (int32_t)snap * 16) {
        f->ema_q4 = x;               /* Snap filter directly to large step changes */
        f->shown  = raw;
        f->init   = true;
    } else {
        f->ema_q4 += (x - f->ema_q4) / 8; /* Smooth small noise with 1/8 weight */
    }

    uint16_t avg = (uint16_t)((f->ema_q4 + 8) / 16);
    *avg_out = avg;

    /* Only update displayed number if smoothed value moved outside +/- deadband */
    int32_t d = (int32_t)avg - (int32_t)f->shown;
    if (d < 0) d = -d;
    if (d > (int32_t)dead) f->shown = avg;
    return f->shown;
}

/* Process one completed 20 ms half-buffer (called directly from DMA1_Channel1_IRQHandler) */
/* THE HEART OF THE MEASURING. Called every 20 ms with 128 fresh snapshots. Order of work:
 *   A) add up the samples -> RMS volts and amps for this single 20 ms wave   (fast, used for protection)
 *   B) also combine the last 4 waves (80 ms) -> a more accurate value         (used for display & calibration)
 *   C) call fsm_on_block() so the protection logic can cut the relay immediately
 *   D) update the smoothed display numbers */
static void process_block(const volatile uint16_t *blk)
{
    /* "warmup" counts down from 5; while it is not zero we just skip this block. */
    /* Discard first 5 blocks (100 ms) after boot while mid-rail coupling capacitors charge */
    if (warmup) { warmup--; return; }

    BlkSum_t s[ADC_NCH];
    for (uint8_t ch = 0; ch < ADC_NCH; ch++) {
        s[ch] = block_sums(blk, ch);
        ring[ch][ring_idx] = s[ch];
    }
    ring_idx = (uint8_t)((ring_idx + 1U) & (WIN_BLOCKS - 1U));
    if (ring_fill < WIN_BLOCKS) ring_fill++;

    /* isqrt32(...) gives the RMS in "counts x 64" (Q6) so we keep 6 extra bits of precision. */
    /* Single-cycle (20 ms) True-RMS: used ONLY for the fast protection path (REQ-05). */
    uint32_t v_q6 = isqrt32(ms_q12(s[0].sum, s[0].sumsq, SAMPLES_PER_BLOCK, 2U));
    g_sens.vac_rms_q6 = (uint16_t)v_q6;
    g_sens.vac_raw_dv = to_units(v_q6, g_cfg.zero_v_q6, g_cfg.cal_v_q12, DEF_GAIN_V_Q8);

#if ENABLE_CT
    uint32_t a_q6 = isqrt32(ms_q12(s[1].sum, s[1].sumsq, SAMPLES_PER_BLOCK, 2U));
    g_sens.ct_rms_q6 = (uint16_t)a_q6;
    g_sens.i_raw_ca  = to_units(a_q6, g_cfg.zero_a_q6, g_cfg.cal_a_q12, DEF_GAIN_A_Q8);
#endif

    /* WHY 4 waves? The chip's own clock can be ~2% off and mains is never exactly 50.000 Hz, so one 20 ms block rarely
     * holds exactly ONE wave. Averaging 4 waves cuts that error about 4x. */
    /* 4-cycle (512-sample) window for display and calibration: N = 512 -> shift = 2*9 - 12 = 6 */
    if (ring_fill == WIN_BLOCKS) {
        for (uint8_t ch = 0; ch < ADC_NCH; ch++) {
            uint32_t sum = 0, sumsq = 0;
            for (uint8_t k = 0; k < WIN_BLOCKS; k++) { sum += ring[ch][k].sum; sumsq += ring[ch][k].sumsq; }
            uint32_t q6 = isqrt32(ms_q12(sum, sumsq, SAMPLES_PER_BLOCK * WIN_BLOCKS, 6U));
            if (ch == 0U) g_sens.vac_win_dv = to_units(q6, g_cfg.zero_v_q6, g_cfg.cal_v_q12, DEF_GAIN_V_Q8);
#if ENABLE_CT
            else          g_sens.i_win_ca   = to_units(q6, g_cfg.zero_a_q6, g_cfg.cal_a_q12, DEF_GAIN_A_Q8);
#endif
        }
    } else {
        g_sens.vac_win_dv = g_sens.vac_raw_dv;
#if ENABLE_CT
        g_sens.i_win_ca   = g_sens.i_raw_ca;
#endif
    }

    g_sens.blocks++;

    /* Two speeds on purpose: protection uses the FAST value so it reacts within one wave; the display uses the
     * SMOOTH value so the number does not flicker. */
    /* 1. FAST PATH: OV, UV, inrush and OC checks on the unfiltered single-cycle values */
    fsm_on_block(&g_sens);

    /* 2. STABLE PATH: smoothed display numbers come from the more accurate 4-cycle window */
    g_sens.vac_disp_dv = filter_update(&filt_v, g_sens.vac_win_dv, V_SNAP_DV, V_DEAD_DV, &g_sens.vac_avg_dv);
#if ENABLE_CT
    g_sens.i_disp_ca   = filter_update(&filt_a, g_sens.i_win_ca, A_SNAP_CA, A_DEAD_CA, &g_sens.i_avg_ca);
#endif
}

/* This interrupt fires when the DMA robot has filled HALF the array (20 ms) or ALL of it (40 ms).
 * Each time we process the half that just became full. */
/* DMA1 Channel 1 Interrupt: fires at Half-Transfer (20 ms) and Transfer-Complete (40 ms) */
void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel1_IRQHandler(void)
{
    uint32_t flags = DMA1->INTFR;
    if (flags & DMA_HTIF1) {
        DMA1->INTFCR = DMA_CHTIF1;
        process_block(&dma_buf[0]);              /* Process first 20 ms half-buffer */
    }
    if (flags & DMA_TCIF1) {
        DMA1->INTFCR = DMA_CTCIF1;
        process_block(&dma_buf[BUF_HALF_WORDS]); /* Process second 20 ms half-buffer */
    }
}

/* adc_init: prepare the ADC (wake it up, choose speed, choose which pins in which order, self-calibrate). */
/* Initialize ADC1 clock, sample times, scan sequence, and run hardware self-calibration */
void adc_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1 | RCC_APB2Periph_TIM1;
    RCC->AHBPCENR  |= RCC_AHBPeriph_DMA1;

    /* Reset ADC1 peripheral to clean power-on state */
    RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
    RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;

    /* ADC speed: 12 MHz. Faster is possible but a slower ADC is more accurate with our high-impedance sensor. */
    /* Set ADC clock to 48 MHz / 4 = 12 MHz (max allowed by CH32V003 datasheet is 24 MHz) */
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_ADCPRE) | RCC_ADCPRE_DIV4;

    /* "Sample time" = how long the ADC lets its tiny capacitor charge from the pin before measuring. Longer = more accurate
     * when the signal source is weak. 43 cycles is plenty for our 100-ohm sensors. */
    /* Set 43-cycle sample-and-hold time on VAC, CT, KEY, and VREF channels */
    uint32_t t = ADC_SAMPLE_TIME_CODE;
    ADC1->SAMPTR2 = (t << (REG_ADC_SAMPTR_BITS * ADC_CH_VAC)) | (t << (REG_ADC_SAMPTR_BITS * ADC_CH_CT)) |
                    (t << (REG_ADC_SAMPTR_BITS * ADC_CH_KEY)) | (t << (REG_ADC_SAMPTR_BITS * ADC_CH_VREF));

    /* Configure Regular Scan sequence: 2 conversions -> Step 1: VAC (ch0), Step 2: CT (ch1) */
    ADC1->RSQR1 = (uint32_t)(ADC_NCH - 1U) << REG_ADC_RSQR1_LEN_SHIFT;
    ADC1->RSQR3 = ADC_CH_VAC | ((uint32_t)ADC_CH_CT << REG_ADC_RSQR3_SLOT_SHIFT);
    ADC1->CTLR1 = REG_ADC_CTLR1_SCAN;
    ADC1->CTLR2 = ADC_ADON | REG_ADC_CTLR2_JEXTTRIG | REG_ADC_CTLR2_JEXTSEL_SW;
    Delay_Us(20);

    /* The ADC can measure its own small errors and cancel them. This takes only microseconds. */
    /* Run CH32V003 built-in ADC capacitor offset calibration */
    ADC1->CTLR2 |= ADC_RSTCAL;
    while (ADC1->CTLR2 & ADC_RSTCAL) {}
    ADC1->CTLR2 |= ADC_CAL;
    while (ADC1->CTLR2 & ADC_CAL) {}

    g_sens.vdd_mv = 5000U; /* Initial fallback until adc_service() measures real VDD */
}

/* adc_start: switch on the whole automatic measuring chain: Timer -> ADC -> DMA -> interrupt. After this the
 * hardware samples forever by itself; the CPU is only bothered once every 20 ms. */
/* Start circular DMA1 transfers and launch TIM1 6400 Hz hardware trigger */
void adc_start(void)
{
    /* Priority only decides which ISR runs first when both are pending (HPE/nesting is off) */
    NVIC_SetPriority(DMA1_Channel1_IRQn, 0x00U);
    NVIC_SetPriority(SysTick_IRQn, 0x80U);

    DMA1_Channel1->CFGR  = 0;
    DMA1_Channel1->CNTR  = 2U * BUF_HALF_WORDS;
    DMA1_Channel1->PADDR = (uint32_t)&ADC1->RDATAR;
    DMA1_Channel1->MADDR = (uint32_t)dma_buf;
    DMA1_Channel1->CFGR  = DMA_CFGR1_MINC |     /* Increment memory pointer after each sample */
                           DMA_CFGR1_PSIZE_0 |  /* 16-bit peripheral read */
                           DMA_CFGR1_MSIZE_0 |  /* 16-bit memory write */
                           DMA_CFGR1_CIRC |     /* Circular ring buffer mode */
                           DMA_CFGR1_HTIE |     /* Interrupt at 50% buffer (every 20 ms) */
                           DMA_CFGR1_TCIE |     /* Interrupt at 100% buffer (every 20 ms) */
                           DMA_CFGR1_PL_1 |     /* High DMA bus priority */
                           DMA_CFGR1_EN;
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    /* Enable ADC DMA requests and external trigger (EXTSEL = 000 selects TIM1_TRGO) */
    ADC1->CTLR2 |= ADC_DMA | ADC_EXTTRIG;

    /* Start TIM1 up-counter at 48 MHz / 7500 = 6400 Hz trigger rate */
    TIM1->CTLR1  = 0;
    TIM1->CTLR2  = REG_TIM1_MMS_UPDATE; /* Timer update event generates the TRGO pulse */
    TIM1->PSC    = 0;
    TIM1->ATRLR  = TIM1_PERIOD - 1U;
    TIM1->RPTCR  = 0;
    TIM1->SWEVGR = TIM_UG;
    TIM1->CTLR1  = TIM_CEN;
}

/* The buttons and the supply-voltage check need extra ADC readings. Because the ADC is busy with the automatic
 * scan, we wait for the quiet gap between two snapshots and squeeze one reading in ("injected" conversion).
 * Interrupts are paused for a few microseconds so nothing disturbs this. */
/* Read an auxiliary channel (KEY or VREFINT) using ADC1 Injected mode without colliding
 * with the background TIM1 -> DMA regular scan sequence. */
uint16_t adc_read_injected(uint8_t channel)
{
    uint16_t result = 0;
    bool timer_running = (TIM1->CTLR1 & TIM_CEN) != 0U;

    __disable_irq();
    if (timer_running) {
        /* Wait until TIM1 counter is safely between 900 and 6500 (idle gap between triggers) */
        uint32_t c;
        do { c = TIM1->CNT; } while (c < INJ_SAFE_MIN || c > INJ_SAFE_MAX);
    }
    ADC1->ISQR   = (uint32_t)channel << REG_ADC_ISQR_JSQ4_SHIFT; /* Select single injected channel in JSQ4 slot */
    ADC1->STATR &= ~REG_ADC_STATR_JEOC;
    ADC1->CTLR2 |= REG_ADC_CTLR2_JSWSTART;           /* Trigger injected conversion */
    uint32_t guard = 2000U;
    while (!(ADC1->STATR & REG_ADC_STATR_JEOC) && --guard) {}
    result = (uint16_t)ADC1->IDATAR1;
    __enable_irq();
    return result;
}

/* The chip has a built-in 1.200 V reference. By measuring it we can work out the real supply voltage VDD
 * (e.g. 4.93 V instead of exactly 5.00 V) and use it as an accurate ruler for all other readings. */
/* Periodically measure internal 1.20 V reference (VREFINT) to compute exact supply voltage VDD.
 * Automatically compensates ADC readings if USB/regulator VDD is 4.85 V or 5.15 V. */
void adc_service(void)
{
    static uint32_t last = 0;
    if ((uint32_t)(g_ms - last) < 500UL && last != 0UL) return;
    last = g_ms ? g_ms : 1UL;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < 8U; i++) sum += adc_read_injected(ADC_CH_VREF);
    if (sum != 0UL) {
        /* Since VREFINT = 1200 mV = (ADC / 1024) * VDD -> VDD = (1200 * 1024 * 8) / sum_of_8 */
        uint32_t mv = (1200UL * 1024UL * 8UL) / sum;
        if (mv >= 2800UL && mv <= 5500UL) g_sens.vdd_mv = (uint16_t)mv;
    }
}

/* Used in calibration step C-0: with NOTHING connected, measure how much "noise" the sensor shows, so it can be
 * subtracted later. Averages 4 waves. */
/* Measure zero-input baseline noise across RMS_AVG_BLOCKS AC cycles (80 ms) during C-0 calibration */
uint16_t adc_measure_noise_q6(uint8_t which)
{
    uint32_t acc = 0;
    for (uint8_t n = 0; n < RMS_AVG_BLOCKS; n++) {
        uint32_t b = g_sens.blocks;
        while (g_sens.blocks == b) hal_wdg_kick(); /* Wait for next fresh 20 ms DMA block */
        acc += (which == ADC_CH_CT) ? g_sens.ct_rms_q6 : g_sens.vac_rms_q6;
    }
    return (uint16_t)(acc / RMS_AVG_BLOCKS);
}