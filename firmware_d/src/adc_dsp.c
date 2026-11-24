/* [#05] adc_dsp.c - TIM1 (6400 Hz) -> ADC1 scan (VAC, CT) -> DMA1 ch1 circular. */
#include "system.h"

#define BUF_HALF_WORDS     (SAMPLES_PER_BLOCK * ADC_NCH)

#define ADCX_SCAN          (1UL << 8)
#define ADCX_JEXTSEL_SOFT  (7UL << 12)
#define ADCX_JEXTTRIG      (1UL << 15)
#define ADCX_JSWSTART      (1UL << 21)
#define ADCX_JEOC          (1UL << 2)

#define TIM1_PERIOD        (SYSCLK_HZ / SAMPLE_RATE_HZ)   /* 7500 */
#define INJ_SAFE_MIN       900U
#define INJ_SAFE_MAX       6500U

static volatile uint16_t dma_buf[2U * BUF_HALF_WORDS] __attribute__((aligned(4)));
static uint8_t warmup = ADC_WARMUP_BLOCKS;

typedef struct { int32_t ema_q4; uint16_t shown; bool init; } Filter_t;
static Filter_t filt_v, filt_a;
static uint32_t ring_v[RMS_AVG_BLOCKS], ring_a[RMS_AVG_BLOCKS];
static uint8_t  ring_idx, ring_fill;

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

static uint32_t block_ms_q12(const volatile uint16_t *blk, uint8_t offset)
{
    uint32_t sum = 0, sumsq = 0;
    for (uint32_t i = 0; i < SAMPLES_PER_BLOCK; i++) {
        uint32_t x = blk[i * ADC_NCH + offset];
        sum   += x;
        sumsq += x * x;
    }
    uint64_t num = (uint64_t)sumsq * SAMPLES_PER_BLOCK - (uint64_t)sum * sum;
    return (uint32_t)(num >> 2);
}

static uint32_t avg_rms_q6(uint32_t *ring, uint32_t newest, uint8_t idx, uint8_t fill)
{
    ring[idx] = newest;
    uint64_t sum = 0;
    for (uint8_t i = 0; i < fill; i++) sum += ring[i];
    return isqrt32((uint32_t)(sum / fill));
}

static uint16_t to_units(uint32_t rms_q6, uint16_t zero_q6, uint16_t trim_q12, uint32_t gain_q8)
{
    uint32_t r2 = rms_q6 * rms_q6;
    uint32_t z2 = (uint32_t)zero_q6 * zero_q6;
    uint32_t r  = (r2 > z2) ? isqrt32(r2 - z2) : 0U;
    uint32_t mv_q6 = (r * g_sens.vdd_mv) >> 10;
    uint64_t u = (uint64_t)mv_q6 * gain_q8 * trim_q12;
    u = (u + (1ULL << 25)) >> 26;
    return (u > 65535ULL) ? 65535U : (uint16_t)u;
}

static uint16_t filter_update(Filter_t *f, uint16_t raw, uint16_t snap, uint16_t dead, volatile uint16_t *avg_out)
{
    int32_t x = (int32_t)raw * 16;
    int32_t diff = x - f->ema_q4;
    if (diff < 0) diff = -diff;
    if (!f->init || diff > (int32_t)snap * 16) {
        f->ema_q4 = x;
        f->shown  = raw;
        f->init   = true;
    } else {
        f->ema_q4 += (x - f->ema_q4) / 8;
    }
    uint16_t avg = (uint16_t)((f->ema_q4 + 8) / 16);
    *avg_out = avg;
    int32_t d = (int32_t)avg - (int32_t)f->shown;
    if (d < 0) d = -d;
    if (d > (int32_t)dead) f->shown = avg;
    return f->shown;
}

static void process_block(const volatile uint16_t *blk)
{
    if (warmup) { warmup--; return; }

    if (ring_fill < RMS_AVG_BLOCKS) ring_fill++;
    uint32_t v_q6 = avg_rms_q6(ring_v, block_ms_q12(blk, 0), ring_idx, ring_fill);
    g_sens.vac_rms_q6 = (uint16_t)v_q6;
    g_sens.vac_raw_dv = to_units(v_q6, g_cfg.zero_v_q6, g_cfg.cal_v_q12, DEF_GAIN_V_Q8);

#if ENABLE_CT
    uint32_t a_q6 = avg_rms_q6(ring_a, block_ms_q12(blk, 1), ring_idx, ring_fill);
    g_sens.ct_rms_q6 = (uint16_t)a_q6;
    g_sens.i_raw_ca  = to_units(a_q6, g_cfg.zero_a_q6, g_cfg.cal_a_q12, DEF_GAIN_A_Q8);
#endif

    ring_idx = (uint8_t)((ring_idx + 1U) % RMS_AVG_BLOCKS);
    g_sens.blocks++;

    /* FAST PATH: 20 ms cutoff check */
    fsm_on_block(&g_sens);

    /* STABLE PATH: display values */
    g_sens.vac_disp_dv = filter_update(&filt_v, g_sens.vac_raw_dv, V_SNAP_DV, V_DEAD_DV, &g_sens.vac_avg_dv);
#if ENABLE_CT
    g_sens.i_disp_ca   = filter_update(&filt_a, g_sens.i_raw_ca, A_SNAP_CA, A_DEAD_CA, &g_sens.i_avg_ca);
#endif
}

void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel1_IRQHandler(void)
{
    uint32_t flags = DMA1->INTFR;
    if (flags & DMA_HTIF1) {
        DMA1->INTFCR = DMA_CHTIF1;
        process_block(&dma_buf[0]);
    }
    if (flags & DMA_TCIF1) {
        DMA1->INTFCR = DMA_CTCIF1;
        process_block(&dma_buf[BUF_HALF_WORDS]);
    }
}

void adc_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1 | RCC_APB2Periph_TIM1;
    RCC->AHBPCENR  |= RCC_AHBPeriph_DMA1;

    RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
    RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;

    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_ADCPRE) | RCC_ADCPRE_DIV4; /* 12 MHz ADCCLK */

    uint32_t t = ADC_SAMPLE_TIME_CODE;
    ADC1->SAMPTR2 = (t << (3U * ADC_CH_VAC)) | (t << (3U * ADC_CH_CT)) |
                    (t << (3U * ADC_CH_KEY)) | (t << (3U * ADC_CH_VREF));

    ADC1->RSQR1 = (uint32_t)(ADC_NCH - 1U) << 20;
    ADC1->RSQR3 = ADC_CH_VAC | ((uint32_t)ADC_CH_CT << 5);
    ADC1->CTLR1 = ADCX_SCAN;
    ADC1->CTLR2 = ADC_ADON | ADCX_JEXTTRIG | ADCX_JEXTSEL_SOFT;
    Delay_Us(20);

    ADC1->CTLR2 |= ADC_RSTCAL;
    while (ADC1->CTLR2 & ADC_RSTCAL) {}
    ADC1->CTLR2 |= ADC_CAL;
    while (ADC1->CTLR2 & ADC_CAL) {}

    g_sens.vdd_mv = 5000U;
}

void adc_start(void)
{
    NVIC_SetPriority(DMA1_Channel1_IRQn, 0x00U);
    NVIC_SetPriority(SysTick_IRQn, 0x80U);

    DMA1_Channel1->CFGR  = 0;
    DMA1_Channel1->CNTR  = 2U * BUF_HALF_WORDS;
    DMA1_Channel1->PADDR = (uint32_t)&ADC1->RDATAR;
    DMA1_Channel1->MADDR = (uint32_t)dma_buf;
    DMA1_Channel1->CFGR  = DMA_CFGR1_MINC |
                           DMA_CFGR1_PSIZE_0 |
                           DMA_CFGR1_MSIZE_0 |
                           DMA_CFGR1_CIRC |
                           DMA_CFGR1_HTIE |
                           DMA_CFGR1_TCIE |
                           DMA_CFGR1_PL_1 |
                           DMA_CFGR1_EN;
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    /* EXTSEL = 000 -> TIM1_TRGO */
    ADC1->CTLR2 |= ADC_DMA | ADC_EXTTRIG;

    TIM1->CTLR1  = 0;
    TIM1->CTLR2  = (2U << 4);   /* MMS = 010: Update event -> TRGO */
    TIM1->PSC    = 0;
    TIM1->ATRLR  = TIM1_PERIOD - 1U;
    TIM1->RPTCR  = 0;
    TIM1->SWEVGR = TIM_UG;
    TIM1->CTLR1  = TIM_CEN;
}

uint16_t adc_read_injected(uint8_t channel)
{
    uint16_t result = 0;
    bool timer_running = (TIM1->CTLR1 & TIM_CEN) != 0U;

    __disable_irq();
    if (timer_running) {
        uint32_t c;
        do { c = TIM1->CNT; } while (c < INJ_SAFE_MIN || c > INJ_SAFE_MAX);
    }
    ADC1->ISQR   = (uint32_t)channel << 15;
    ADC1->STATR &= ~ADCX_JEOC;
    ADC1->CTLR2 |= ADCX_JSWSTART;
    uint32_t guard = 2000U;
    while (!(ADC1->STATR & ADCX_JEOC) && --guard) {}
    result = (uint16_t)ADC1->IDATAR1;
    __enable_irq();
    return result;
}

void adc_service(void)
{
    static uint32_t last = 0;
    if ((uint32_t)(g_ms - last) < 500UL && last != 0UL) return;
    last = g_ms ? g_ms : 1UL;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < 8U; i++) sum += adc_read_injected(ADC_CH_VREF);
    if (sum != 0UL) {
        uint32_t mv = (1200UL * 1024UL * 8UL) / sum;
        if (mv >= 2800UL && mv <= 5500UL) g_sens.vdd_mv = (uint16_t)mv;
    }
}

uint16_t adc_measure_noise_q6(uint8_t which)
{
    uint32_t acc = 0;
    for (uint8_t n = 0; n < 2U; n++) {
        uint32_t b = g_sens.blocks;
        while (g_sens.blocks == b) hal_wdg_kick();
        acc += (which == ADC_CH_CT) ? g_sens.ct_rms_q6 : g_sens.vac_rms_q6;
    }
    return (uint16_t)(acc / 2U);
}