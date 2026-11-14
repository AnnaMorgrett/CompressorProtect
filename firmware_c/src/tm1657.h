#ifndef TM1657_H
#define TM1657_H

#include "board.h"

#define SEG_A 0x01
#define SEG_B 0x02
#define SEG_C 0x04
#define SEG_D 0x08
#define SEG_E 0x10
#define SEG_F 0x20
#define SEG_G 0x40

#define LTR_U (SEG_B | SEG_C | SEG_D | SEG_E | SEG_F)
#define LTR_E (SEG_A | SEG_D | SEG_E | SEG_F | SEG_G)
#define LTR_r (SEG_E | SEG_G)
#define LTR_L (SEG_D | SEG_E | SEG_F)
#define LTR_O (SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F)
#define LTR_C (SEG_A | SEG_D | SEG_E | SEG_F)
#define LTR_S (SEG_A | SEG_C | SEG_D | SEG_F | SEG_G)
#define LTR_P (SEG_A | SEG_B | SEG_E | SEG_F | SEG_G)
#define LTR_t (SEG_D | SEG_E | SEG_F | SEG_G)
#define LTR_A (SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G)
#define LTR_K (SEG_B | SEG_C | SEG_E | SEG_F | SEG_G)
#define BLANK 0x00

extern const uint8_t DIGIT_SEG[10];

void tm1637_init(void);
void tm1637_set_brightness(uint8_t b);
void tm1637_show_segments(const uint8_t segs[4]);
void tm1637_show_number(int32_t num);

#endif