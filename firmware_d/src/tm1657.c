#include "tm1657.h"
#include "board.h"
#include "ch32fun.h"

// ============================================================================
// IMPLEMENTASI DRIVER KOMUNIKASI PROTOKOL SERIAL TM1637
// ============================================================================

// Tabel konversi angka 0 - 9 ke pola segmen 7-segment (Common Anode TM1637)
const uint8_t DIGIT_SEG[10] = {
    0x3F, // '0' = A + B + C + D + E + F
    0x06, // '1' = B + C
    0x5B, // '2' = A + B + D + E + G
    0x4F, // '3' = A + B + C + D + G
    0x66, // '4' = B + C + F + G
    0x6D, // '5' = A + C + D + F + G
    0x7D, // '6' = A + C + D + E + F + G
    0x07, // '7' = A + B + C
    0x7F, // '8' = A + B + C + D + E + F + G
    0x6F  // '9' = A + B + C + D + F + G
};

// Byte perintah kontrol kecerahan layar:
// Bit 3 = Display ON (0x08), Bit 0..2 = Brightness level (0..7)
static uint8_t tm_brightness = 0x88 | 4; // Default level 4, Display ON

// Kontrol pin kawat CLK dan DAT secara cepat via ch32fun macro
static inline void tm_clk_high(void) { funDigitalWrite(PIN_TM_CLK, FUN_HIGH); }
static inline void tm_clk_low(void)  { funDigitalWrite(PIN_TM_CLK, FUN_LOW); }
static inline void tm_dat_high(void) { funDigitalWrite(PIN_TM_DAT, FUN_HIGH); }
static inline void tm_dat_low(void)  { funDigitalWrite(PIN_TM_DAT, FUN_LOW); }

/**
 * @brief Kondisi START protokol TM1637.
 *        Saat CLK HIGH, transisi DIO dari HIGH ke LOW menandakan awal pengiriman frame.
 */
static void tm_start(void) {
    tm_dat_high();
    tm_clk_high();
    Delay_Us(4);
    tm_dat_low();
    Delay_Us(4);
    tm_clk_low();
}

/**
 * @brief Kondisi STOP protokol TM1637.
 *        Saat CLK HIGH, transisi DIO dari LOW ke HIGH menandakan akhir pengiriman frame.
 */
static void tm_stop(void) {
    tm_clk_low();
    Delay_Us(4);
    tm_dat_low();
    Delay_Us(4);
    tm_clk_high();
    Delay_Us(4);
    tm_dat_high();
}

/**
 * @brief Mengirimkan satu byte data ke chip TM1637 dengan urutan LSB First (Bit 0 dulu).
 *        Membaca sinyal konfirmasi ACK dari TM1637 pada pulsa clock ke-9.
 * @param b Byte data/perintah yang dikirim.
 */
static void tm_write_byte(uint8_t b) {
    // Kirim 8 bit data, dimulai dari LSB (Bit 0)
    for (uint8_t i = 0; i < 8; i++) {
        tm_clk_low();
        if (b & 0x01) tm_dat_high();
        else          tm_dat_low();
        Delay_Us(4);
        tm_clk_high();
        Delay_Us(4);
        b >>= 1;
    }

    // Pulsa clock ke-9 untuk membaca respon ACK dari chip TM1637
    tm_clk_low();
    // Ubah pin DAT ke mode Input Pull-up agar TM1637 dapat menarik sinyal ke GND (ACK)
    funPinMode(PIN_TM_DAT, GPIO_CNF_IN_PUPD);
    tm_dat_high();
    Delay_Us(4);
    tm_clk_high();
    Delay_Us(4);
    tm_clk_low();
    // Kembalikan pin DAT ke mode Output Open-Drain untuk pengiriman data berikutnya
    funPinMode(PIN_TM_DAT, GPIO_Speed_10MHz | GPIO_CNF_OUT_OD);
}

/**
 * @brief Inisialisasi pin antarmuka TM1637 (CLK sebagai Push-Pull, DAT sebagai Open-Drain).
 */
void tm1637_init(void) {
    funPinMode(PIN_TM_CLK, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_TM_DAT, GPIO_Speed_10MHz | GPIO_CNF_OUT_OD);
    tm_clk_high();
    tm_dat_high();
}

/**
 * @brief Mengatur kecerahan display (0 = redup, 7 = sangat terang).
 */
void tm1637_set_brightness(uint8_t b) {
    if (b > 7) b = 7;
    tm_brightness = 0x88 | b; // Perintah kontrol display (0x88 = Display ON + nilai kecerahan)
    tm_start();
    tm_write_byte(tm_brightness);
    tm_stop();
}

/**
 * @brief Menampilkan 4 karakter segmen secara simultan ke 4 digit display TM1637.
 *        Menggunakan mode Auto-Increment Address untuk efisiensi transfer bus.
 */
void tm1637_show_segments(const uint8_t segments[4]) {
    // 1. Kirim perintah pengaturan data: 0x40 (Write Data to RAM, Auto-Increment Address)
    tm_start();
    tm_write_byte(0x40);
    tm_stop();

    // 2. Kirim alamat awal RAM display: 0xC0 (Digit 0 paling kiri), diikuti 4 byte data
    tm_start();
    tm_write_byte(0xC0);
    for (uint8_t i = 0; i < 4; i++) {
        tm_write_byte(segments[i]);
    }
    tm_stop();

    // 3. Kirim kembali perintah kontrol tampilan & kecerahan untuk memastikan layar aktif
    tm_start();
    tm_write_byte(tm_brightness);
    tm_stop();
}

/**
 * @brief Menampilkan bilangan desimal pada 4-digit 7-segment (Right-Aligned).
 *        Mendukung nilai negatif (misalnya suhu minus pada kalibrasi C-3) dengan
 *        menampilkan tanda '-' (SEG_G) pada posisi di depan angka pertama.
 *        Digit kosong di sisi kiri tidak menampilkan angka (Blank/Padam) demi kerapian tampilan.
 */
void tm1637_show_number(int num) {
    uint8_t segs[4];
    bool neg = false;

    // Tangani bilangan negatif
    if (num < 0) {
        neg = true;
        num = -num;
    }

    // Digit 3 (Paling kanan / Satuan) selalu ditampilkan
    segs[3] = DIGIT_SEG[num % 10];
    num /= 10;

    // Digit 2 (Puluhan)
    if (num > 0) {
        segs[2] = DIGIT_SEG[num % 10];
        num /= 10;
        // Digit 1 (Ratusan)
        if (num > 0) {
            segs[1] = DIGIT_SEG[num % 10];
            num /= 10;
            // Digit 0 (Ribuan) atau tanda minus '-' jika bilangan negatif 3-digit
            segs[0] = neg ? SEG_G : ((num > 0) ? DIGIT_SEG[num % 10] : BLANK);
        } else {
            // Jika hanya 2 digit, tanda minus ditaruh di digit 1 (Ratusan)
            segs[1] = neg ? SEG_G : BLANK;
            segs[0] = BLANK;
        }
    } else {
        // Jika hanya 1 digit, tanda minus ditaruh di digit 2 (Puluhan)
        segs[2] = neg ? SEG_G : BLANK;
        segs[1] = BLANK;
        segs[0] = BLANK;
    }

    tm1637_show_segments(segs);
}
