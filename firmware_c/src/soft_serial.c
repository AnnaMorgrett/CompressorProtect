#include "soft_serial.h"

#define DBG_TX_MASK (1U << 7) // PC7
#define DBG_RX_MASK (1U << 6) // PC6

static bool     has_rx  = false;
static uint16_t bit_us  = 102;
static uint8_t  rx_buf[24];
static uint8_t  rx_head = 0;
static uint8_t  rx_tail = 0;

void soft_serial_begin(uint32_t baud) {
    funPinMode(PIN_DEBUG_TX, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    GPIOC->BSHR = DBG_TX_MASK; // Idle HIGH
    bit_us = (baud == 9600) ? 102 : (uint16_t)(1000000UL / baud - 2);
}

void soft_serial_enable_rx(void) {
    has_rx = true;
    funPinMode(PIN_DEBUG_RX, GPIO_CNF_IN_PUPD);
    GPIOC->BSHR = DBG_RX_MASK; // Pull-up
}

bool soft_serial_rx_start_detected(void) {
    return has_rx && ((GPIOC->INDR & DBG_RX_MASK) == 0);
}

static void soft_serial_poll_rx(void) {
    if (!has_rx) return;
    while ((GPIOC->INDR & DBG_RX_MASK) == 0) {
        Delay_Us(bit_us / 2);
        if ((GPIOC->INDR & DBG_RX_MASK) != 0) break;

        uint8_t val = 0;
        for (uint8_t i = 0; i < 8; i++) {
            Delay_Us(bit_us);
            if (GPIOC->INDR & DBG_RX_MASK) val |= (1U << i);
        }

        Delay_Us(bit_us);
        if (GPIOC->INDR & DBG_RX_MASK) {
            uint8_t next_head = (rx_head + 1) % sizeof(rx_buf);
            if (next_head != rx_tail) {
                rx_buf[rx_head] = val;
                rx_head = next_head;
            }
        }

        uint32_t wait_end = micros() + (bit_us + (bit_us / 2));
        while ((int32_t)(micros() - wait_end) < 0) {
            if ((GPIOC->INDR & DBG_RX_MASK) == 0) break;
        }
    }
}

int soft_serial_available(void) {
    soft_serial_poll_rx();
    return (rx_head >= rx_tail) ? (rx_head - rx_tail) : (sizeof(rx_buf) - rx_tail + rx_head);
}

int soft_serial_read(void) {
    if (rx_head == rx_tail) return -1;
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % sizeof(rx_buf);
    return c;
}

void soft_serial_write_byte(uint8_t b) {
    GPIOC->BCR = DBG_TX_MASK; // Start bit LOW
    Delay_Us(bit_us);
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x01) GPIOC->BSHR = DBG_TX_MASK;
        else          GPIOC->BCR  = DBG_TX_MASK;
        b >>= 1;
        Delay_Us(bit_us);
    }
    GPIOC->BSHR = DBG_TX_MASK; // Stop bit HIGH
    Delay_Us(bit_us);
}

void soft_serial_print_char(char c) {
    soft_serial_write_byte((uint8_t)c);
}

void soft_serial_print_str(const char *s) {
    while (*s) soft_serial_write_byte((uint8_t)*s++);
}

void soft_serial_print_int(int32_t n) {
    if (n < 0) {
        soft_serial_write_byte('-');
        n = -n;
    }
    char buf[12];
    uint8_t i = 0;
    do {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    while (i > 0) soft_serial_write_byte(buf[--i]);
}

void soft_serial_println(void) {
    soft_serial_print_str("\r\n");
}

void soft_serial_println_str(const char *s) {
    soft_serial_print_str(s);
    soft_serial_println();
}

void soft_serial_println_int(int32_t n) {
    soft_serial_print_int(n);
    soft_serial_println();
}