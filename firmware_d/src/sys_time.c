#include "sys_time.h"
#include "ch32fun.h"

// ============================================================================
// IMPLEMENTASI SYSTICK UNTUK MILLIS() & MICROS()
// ============================================================================
// Pada frekuensi CPU 48 MHz, counter SysTick (SysTick->CNT) berdetak 48.000.000 kali
// per detik. Artinya:
// - 48 cycle CPU = 1 mikrodetik (us)
// - 48.000 cycle CPU = 1 milidetik (ms)
// ============================================================================

static uint32_t overflow_ms = 0; // Akumulator milidetik
static uint32_t last_cnt = 0;    // Nilai counter terakhir yang dibaca

void sys_time_init(void) {
    // Aktifkan counter SysTick (bit 0 = CTLR_STE)
    SysTick->CTLR = 1;
    overflow_ms = 0;
    last_cnt = 0;
}

uint32_t millis(void) {
    uint32_t cnt = SysTick->CNT;
    uint32_t delta = cnt - last_cnt;

    // Setiap kali counter melewati kelipatan 48.000 tick (1 ms)
    if (delta >= 48000) {
        uint32_t inc = delta / 48000;
        overflow_ms += inc;
        last_cnt += inc * 48000;
    }
    return overflow_ms;
}

uint32_t micros(void) {
    // Menghitung mikrodetik presisi tinggi dari sisa tick counter di bawah 1 ms
    return millis() * 1000 + (SysTick->CNT - last_cnt) / 48;
}
