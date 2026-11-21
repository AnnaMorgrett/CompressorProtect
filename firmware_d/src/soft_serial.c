#include "soft_serial.h"
#include "board.h"
#include "ch32fun.h"

// ============================================================================
// IMPLEMENTASI SOFTWARE UART (BIT-BANGING 9600 BAUD 8N1)
// ============================================================================

// Mask register bit port C untuk pin TX (PC7) dan RX (PC6)
#define DBG_TX_MASK (1U << 7) // PC7: Pin 6 SOP-16
#define DBG_RX_MASK (1U << 6) // PC6: Pin 5 SOP-16

static uint16_t bit_us  = 104;   // Durasi satu bit pada 9600 baud = 1.000.000 / 9600 ≈ 104 mikrodetik
static bool     has_rx  = false; // Status apakah fungsi penerimaan RX aktif

// Circular Ring Buffer untuk menampung karakter masuk dari serial CLI
static uint8_t  rx_buf[24];
static uint8_t  rx_head = 0;
static uint8_t  rx_tail = 0;

/**
 * @brief Menginisialisasi pin TX (PC7) sebagai Output Push-Pull dengan level Idle HIGH.
 */
void soft_serial_begin(uint32_t baud) {
    funPinMode(PIN_DEBUG_TX, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    GPIOC->BSHR = DBG_TX_MASK; // Port Bit Set: Jalur UART idle pada logika HIGH (1)
    bit_us = (baud == 9600) ? 104 : (uint16_t)(1000000UL / baud);
}

/**
 * @brief Mengaktifkan pin RX (PC6) sebagai Input Pull-Up untuk menerima perintah CLI.
 */
void soft_serial_enable_rx(void) {
    funPinMode(PIN_DEBUG_RX, GPIO_CNF_IN_PUPD);
    GPIOC->BSHR = DBG_RX_MASK; // Aktifkan resistor internal Pull-Up pada PC6
    has_rx = true;
}

/**
 * @brief Deteksi cepat sinyal start bit (LOW) pada pin RX secara non-blocking.
 */
bool soft_serial_rx_start_detected(void) {
    return has_rx && ((GPIOC->INDR & DBG_RX_MASK) == 0);
}

/**
 * @brief Menerima dan melakukan dekode byte UART secara presisi waktu bit-bang.
 *        1. Deteksi falling-edge (Start Bit).
 *        2. Tunggu setengah durasi bit (52 us) untuk memverifikasi titik tengah sinyal.
 *        3. Baca 8 data bit setiap kelipatan 104 mikrodetik (LSB First).
 *        4. Verifikasi stop bit (HIGH) dan simpan byte ke dalam circular ring buffer.
 */
void soft_serial_poll_rx(void) {
    if (!has_rx) return;

    // Selama pin RX berada dalam kondisi LOW (Start Bit terdeteksi)
    while ((GPIOC->INDR & DBG_RX_MASK) == 0) {
        // Sampel tepat di tengah start-bit (52 us) untuk memvalidasi noise/glitch
        Delay_Us(bit_us / 2);
        if ((GPIOC->INDR & DBG_RX_MASK) != 0) break; // Abaikan jika hanya glitch sesaat

        uint8_t val = 0;
        // Baca 8 data bit (dimulai dari Bit 0 / LSB)
        for (uint8_t i = 0; i < 8; i++) {
            Delay_Us(bit_us);
            if (GPIOC->INDR & DBG_RX_MASK) val |= (1 << i);
        }

        // Tunggu hingga jendela waktu Stop Bit (harus kembali HIGH)
        Delay_Us(bit_us);
        if (GPIOC->INDR & DBG_RX_MASK) {
            // Masukkan karakter ke dalam circular buffer jika masih ada slot kosong
            uint8_t next_head = (rx_head + 1) % sizeof(rx_buf);
            if (next_head != rx_tail) {
                rx_buf[rx_head] = val;
                rx_head = next_head;
            }
        }
        Delay_Us(bit_us);
    }
}

/**
 * @brief Memeriksa ketersediaan karakter di dalam ring buffer.
 */
int soft_serial_available(void) {
    soft_serial_poll_rx();
    return (rx_head >= rx_tail) ? (rx_head - rx_tail) : (sizeof(rx_buf) - rx_tail + rx_head);
}

/**
 * @brief Mengambil satu karakter terlama dari buffer penerima.
 */
int soft_serial_read(void) {
    if (rx_head == rx_tail) return -1; // Buffer kosong
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % sizeof(rx_buf);
    return c;
}

/**
 * @brief Mengirimkan satu byte data keluar secara bit-bang serial 8N1.
 *        Menggunakan register atomik:
 *        - GPIOC->BCR  (Bit Clear Register) untuk logika LOW (0)
 *        - GPIOC->BSHR (Bit Set/Reset Register) untuk logika HIGH (1)
 */
void soft_serial_write_byte(uint8_t b) {
    // 1. Kirim START BIT (Logika LOW)
    GPIOC->BCR = DBG_TX_MASK;
    Delay_Us(bit_us);

    // 2. Kirim 8 DATA BITS (LSB first)
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x01) GPIOC->BSHR = DBG_TX_MASK;
        else          GPIOC->BCR  = DBG_TX_MASK;
        b >>= 1;
        Delay_Us(bit_us);
    }

    // 3. Kirim STOP BIT (Logika HIGH)
    GPIOC->BSHR = DBG_TX_MASK;
    Delay_Us(bit_us);
}

/**
 * @brief Mengirimkan satu karakter ASCII.
 */
void soft_serial_print_char(char c) {
    soft_serial_write_byte((uint8_t)c);
}

/**
 * @brief Mengirimkan rangkaian teks (string null-terminated).
 */
void soft_serial_print_str(const char *s) {
    while (*s) soft_serial_write_byte((uint8_t)*s++);
}

/**
 * @brief Mengonversi dan mencetak bilangan bulat integer bertanda (desimal).
 */
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
    // Cetak urutan angka dari digit paling signifikan (kiri)
    while (i > 0) soft_serial_write_byte(buf[--i]);
}

/**
 * @brief Mengirimkan karakter baris baru CRLF (\r\n).
 */
void soft_serial_println(void) {
    soft_serial_write_byte('\r');
    soft_serial_write_byte('\n');
}

/**
 * @brief Mencetak string diikuti baris baru.
 */
void soft_serial_println_str(const char *s) {
    soft_serial_print_str(s);
    soft_serial_println();
}

/**
 * @brief Mencetak bilangan integer diikuti baris baru.
 */
void soft_serial_println_int(int32_t n) {
    soft_serial_print_int(n);
    soft_serial_println();
}
