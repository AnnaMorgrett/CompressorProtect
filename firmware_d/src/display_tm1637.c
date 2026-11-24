/* [#06] display_tm1637.c - bit-banged TM1637 (CLK = PC4, DIO = PC3), 4 digits + colon. */
#include "system.h"

#define SEG_COLON           0x80U
#define TM_CMD_DATA_AUTOINC 0x40U
#define TM_CMD_ADDR0        0xC0U
#define TM_CMD_DISPLAY_ON   0x88U

static uint8_t buf[4];
static uint8_t shown[4] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU };
static uint32_t last_refresh = 0;

static void clk(uint8_t v) { funDigitalWrite(PIN_TM_CLK, v); Delay_Us(5); }
static void dio(uint8_t v) { funDigitalWrite(PIN_TM_DIO, v); Delay_Us(5); }

static void tm_start(void) { dio(1); clk(1); dio(0); clk(0); }
static void tm_stop(void)  { clk(0); dio(0); clk(1); dio(1); }

static void tm_write(uint8_t b)
{
    for (uint8_t i = 0; i < 8U; i++) {
        clk(0);
        dio(b & 0x01U);
        clk(1);
        b >>= 1;
    }
    /* 9th clock ACK slot */
    clk(0);
    dio(0);
    clk(1);
    clk(0);
}

static uint8_t seg_for(char c)
{
    static const uint8_t digits[10] = {
        0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U, 0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU
    };
    if (c >= '0' && c <= '9') return digits[c - '0'];
    switch (c) {
        case 'A': return 0x77U;  case 'C': return 0x39U;  case 'd': return 0x5EU;
        case 'E': return 0x79U;  case 'L': return 0x38U;  case 'n': return 0x54U;
        case 'O': return 0x3FU;  case 'P': return 0x73U;  case 'r': return 0x50U;
        case 'S': return 0x6DU;  case 't': return 0x78U;  case 'U': return 0x3EU;
        case 'V': return 0x3EU;  case '-': return 0x40U;  case 'e': return 0x79U;
        default:  return 0x00U;
    }
}

void display_init(void)
{
    for (uint8_t i = 0; i < 4U; i++) {
        buf[i]   = 0x00U;
        shown[i] = 0xFFU;
    }
}

void display_text(const char *s)
{
    for (uint8_t i = 0; i < 4U; i++) {
        buf[i] = (*s) ? seg_for(*s++) : 0x00U;
    }
}

void display_uint(uint16_t v)
{
    if (v > 9999U) v = 9999U;
    for (int8_t i = 3; i >= 0; i--) {
        if (v == 0U && i < 3) buf[i] = 0x00U;
        else                  buf[i] = seg_for((char)('0' + (v % 10U)));
        v /= 10U;
    }
}

void display_uint_pad4(uint16_t v)
{
    if (v > 9999U) v = 9999U;
    for (int8_t i = 3; i >= 0; i--) {
        buf[i] = seg_for((char)('0' + (v % 10U)));
        v /= 10U;
    }
}

void display_labeled(char label, uint16_t v)
{
    if (v > 999U) v = 999U;
    buf[0] = seg_for(label);
    for (int8_t i = 3; i >= 1; i--) {
        buf[i] = seg_for((char)('0' + (v % 10U)));
        v /= 10U;
    }
}

void display_mmss(uint16_t seconds)
{
    uint16_t m = seconds / 60U, s = seconds % 60U;
    if (m > 99U) m = 99U;
    buf[0] = seg_for((char)('0' + m / 10U));
    buf[1] = seg_for((char)('0' + m % 10U)) | SEG_COLON;
    buf[2] = seg_for((char)('0' + s / 10U));
    buf[3] = seg_for((char)('0' + s % 10U));
}

void display_flush(void)
{
    bool changed = false;
    for (uint8_t i = 0; i < 4U; i++) {
        if (buf[i] != shown[i]) changed = true;
    }
    if (!changed && (uint32_t)(g_ms - last_refresh) < 500UL) return;
    last_refresh = g_ms;

    tm_start(); tm_write(TM_CMD_DATA_AUTOINC); tm_stop();
    tm_start(); tm_write(TM_CMD_ADDR0);
    for (uint8_t i = 0; i < 4U; i++) {
        tm_write(buf[i]);
        shown[i] = buf[i];
    }
    tm_stop();
    tm_start(); tm_write((uint8_t)(TM_CMD_DISPLAY_ON | DISPLAY_BRIGHTNESS)); tm_stop();
}