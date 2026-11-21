#include "tm1657.h"
#include "board.h"
#include "ch32fun.h"

const uint8_t DIGIT_SEG[10] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F  // 9
};

static uint8_t tm_brightness = 0x88 | 4;

static void tm_clk_high(void) { funDigitalWrite(PIN_TM_CLK, FUN_HIGH); }
static void tm_clk_low(void)  { funDigitalWrite(PIN_TM_CLK, FUN_LOW); }
static void tm_dat_high(void) { funDigitalWrite(PIN_TM_DAT, FUN_HIGH); }
static void tm_dat_low(void)  { funDigitalWrite(PIN_TM_DAT, FUN_LOW); }

static void tm_start(void) {
    tm_dat_high();
    tm_clk_high();
    Delay_Us(4);
    tm_dat_low();
    Delay_Us(4);
    tm_clk_low();
}

static void tm_stop(void) {
    tm_clk_low();
    Delay_Us(4);
    tm_dat_low();
    Delay_Us(4);
    tm_clk_high();
    Delay_Us(4);
    tm_dat_high();
}

static void tm_write_byte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        tm_clk_low();
        if (b & 0x01) tm_dat_high();
        else          tm_dat_low();
        Delay_Us(4);
        tm_clk_high();
        Delay_Us(4);
        b >>= 1;
    }
    // ACK clock
    tm_clk_low();
    funPinMode(PIN_TM_DAT, GPIO_CNF_IN_PUPD);
    tm_dat_high();
    Delay_Us(4);
    tm_clk_high();
    Delay_Us(4);
    tm_clk_low();
    funPinMode(PIN_TM_DAT, GPIO_Speed_10MHz | GPIO_CNF_OUT_OD);
}

void tm1637_init(void) {
    funPinMode(PIN_TM_CLK, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_TM_DAT, GPIO_Speed_10MHz | GPIO_CNF_OUT_OD);
    tm_clk_high();
    tm_dat_high();
}

void tm1637_set_brightness(uint8_t b) {
    if (b > 7) b = 7;
    tm_brightness = 0x88 | b;
    tm_start();
    tm_write_byte(tm_brightness);
    tm_stop();
}

void tm1637_show_segments(const uint8_t segments[4]) {
    tm_start();
    tm_write_byte(0x40); // Auto-increment address
    tm_stop();

    tm_start();
    tm_write_byte(0xC0); // Start at display address 0
    for (uint8_t i = 0; i < 4; i++) {
        tm_write_byte(segments[i]);
    }
    tm_stop();

    tm_start();
    tm_write_byte(tm_brightness);
    tm_stop();
}

void tm1637_show_number(int num) {
    if (num < 0) num = 0;
    uint8_t segs[4];
    segs[3] = DIGIT_SEG[num % 10];
    num /= 10;
    segs[2] = (num > 0) ? DIGIT_SEG[num % 10] : BLANK;
    num /= 10;
    segs[1] = (num > 0) ? DIGIT_SEG[num % 10] : BLANK;
    num /= 10;
    segs[0] = (num > 0) ? DIGIT_SEG[num % 10] : BLANK;
    tm1637_show_segments(segs);
}
