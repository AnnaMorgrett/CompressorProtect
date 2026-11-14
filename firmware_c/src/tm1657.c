#include "tm1657.h"

#define TM_DAT_MASK (1U << 3) // PC3
#define TM_CLK_MASK (1U << 4) // PC4

const uint8_t DIGIT_SEG[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static uint8_t tm_brightness = 0x8C;

static inline void tm_delay(void) { Delay_Us(5); }

static void tm_start(void) {
    GPIOC->BSHR = TM_CLK_MASK | TM_DAT_MASK;
    tm_delay();
    GPIOC->BCR = TM_DAT_MASK;
    tm_delay();
    GPIOC->BCR = TM_CLK_MASK;
}

static void tm_stop(void) {
    GPIOC->BCR = TM_CLK_MASK | TM_DAT_MASK;
    tm_delay();
    GPIOC->BSHR = TM_CLK_MASK;
    tm_delay();
    GPIOC->BSHR = TM_DAT_MASK;
    tm_delay();
}

static void tm_write_byte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        GPIOC->BCR = TM_CLK_MASK;
        if (b & 0x01) GPIOC->BSHR = TM_DAT_MASK;
        else          GPIOC->BCR  = TM_DAT_MASK;
        tm_delay();
        GPIOC->BSHR = TM_CLK_MASK;
        tm_delay();
        b >>= 1;
    }
    GPIOC->BCR = TM_CLK_MASK;
    funPinMode(PIN_TM_DAT, GPIO_CNF_IN_PUPD);
    GPIOC->BSHR = TM_DAT_MASK;
    tm_delay();
    GPIOC->BSHR = TM_CLK_MASK;
    tm_delay();
    GPIOC->BCR = TM_CLK_MASK;
    funPinMode(PIN_TM_DAT, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
}

void tm1637_init(void) {
    funPinMode(PIN_TM_CLK, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_TM_DAT, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
}

void tm1637_set_brightness(uint8_t b) {
    tm_brightness = 0x88 | (b & 0x07);
}

void tm1637_show_segments(const uint8_t segs[4]) {
    tm_start(); tm_write_byte(0x40); tm_stop();
    tm_start(); tm_write_byte(0xC0);
    for (uint8_t i = 0; i < 4; i++) tm_write_byte(segs[i]);
    tm_stop();
    tm_start(); tm_write_byte(tm_brightness); tm_stop();
}

void tm1637_show_number(int32_t num) {
    if (num < 0) num = 0;
    uint8_t segs[4] = {BLANK, BLANK, BLANK, DIGIT_SEG[num % 10]};
    num /= 10;
    if (num > 0) { segs[2] = DIGIT_SEG[num % 10]; num /= 10; }
    if (num > 0) { segs[1] = DIGIT_SEG[num % 10]; num /= 10; }
    if (num > 0) { segs[0] = DIGIT_SEG[num % 10]; }
    tm1637_show_segments(segs);
}