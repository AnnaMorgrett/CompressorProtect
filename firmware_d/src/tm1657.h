#ifndef TM1657_H
#define TM1657_H

#include <stdint.h>
#include <stdbool.h>

// Standard 7-Segment bits
#define SEG_A 0b00000001
#define SEG_B 0b00000010
#define SEG_C 0b00000100
#define SEG_D 0b00001000
#define SEG_E 0b00010000
#define SEG_F 0b00100000
#define SEG_G 0b01000000
#define SEG_DP 0b10000000

#define BLANK 0x00

// Custom letter glyphs from firmware_d UI
#define LTR_U (SEG_B | SEG_C | SEG_D | SEG_E | SEG_F)
#define LTR_E (SEG_A | SEG_D | SEG_E | SEG_F | SEG_G)
#define LTR_r (SEG_E | SEG_G)
#define LTR_L (SEG_D | SEG_E | SEG_F)
#define LTR_O (SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F)
#define LTR_C (SEG_A | SEG_D | SEG_E | SEG_F)
#define LTR_S (SEG_A | SEG_C | SEG_D | SEG_F | SEG_G)
#define LTR_P (SEG_A | SEG_B | SEG_E | SEG_F | SEG_G)
#define LTR_n (SEG_C | SEG_E | SEG_G)
#define LTR_t (SEG_D | SEG_E | SEG_F | SEG_G)
#define LTR_A (SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G)
#define LTR_K (SEG_B | SEG_C | SEG_E | SEG_F | SEG_G)

extern const uint8_t DIGIT_SEG[10];

void tm1637_init(void);
void tm1637_set_brightness(uint8_t brightness);
void tm1637_show_segments(const uint8_t segments[4]);
void tm1637_show_number(int num);

#endif
