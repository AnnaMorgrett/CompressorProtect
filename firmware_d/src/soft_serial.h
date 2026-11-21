#ifndef SOFT_SERIAL_H
#define SOFT_SERIAL_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// DRIVER KOMUNIKASI SERIAL SOFTWARE UART (BIT-BANG) NON-BLOCKING
// ============================================================================
// Mengimplementasikan UART asinkron 9600 baud (8 data bit, tanpa paritas, 1 stop bit)
// menggunakan manipulasi register GPIO secara cepat (atomic BSHR/BCR).
// - Jalur TX: Pin PC7 (Header H1 Pin 2)
// - Jalur RX: Pin PC6 (Header H1 Pin 3)
// Dilengkapi circular ring-buffer untuk penerimaan data tanpa kehilangan karakter.
// ============================================================================

/**
 * @brief Menginisialisasi jalur pemancar serial (TX) pada baud rate yang ditentukan.
 * @param baud Kecepatan transmisi (default: 9600 baud -> durasi per bit = 104 mikrodetik).
 */
void soft_serial_begin(uint32_t baud);

/**
 * @brief Mengaktifkan pin penerima serial (RX) dengan konfigurasi Input Pull-Up.
 */
void soft_serial_enable_rx(void);

/**
 * @brief Memeriksa secara instan (non-blocking) apakah sinyal start-bit (logika LOW)
 *        terdeteksi pada pin RX. Digunakan untuk menyela eksekusi jika ada perintah serial masuk.
 * @return true jika sinyal LOW terdeteksi pada pin RX.
 */
bool soft_serial_rx_start_detected(void);

/**
 * @brief Melakukan polling dan decoding bit-bang terhadap karakter serial yang masuk,
 *        lalu menyimpannya ke dalam circular ring buffer.
 */
void soft_serial_poll_rx(void);

/**
 * @brief Mengetahui jumlah byte yang tersedia untuk dibaca dalam ring buffer.
 * @return Jumlah karakter dalam buffer.
 */
int soft_serial_available(void);

/**
 * @brief Membaca satu karakter dari ring buffer.
 * @return Nilai karakter (0..255) atau -1 jika buffer kosong.
 */
int soft_serial_read(void);

/**
 * @brief Mengirimkan 1 byte data secara serial via bit-banging.
 * @param b Karakter byte yang akan dikirimkan.
 */
void soft_serial_write_byte(uint8_t b);

/**
 * @brief Mencetak 1 karakter teks ke port serial.
 * @param c Karakter ASCII.
 */
void soft_serial_print_char(char c);

/**
 * @brief Mencetak string teks (null-terminated) ke port serial.
 * @param s Pointer ke string teks.
 */
void soft_serial_print_str(const char *s);

/**
 * @brief Mencetak bilangan bulat (integer bertanda 32-bit) ke port serial.
 * @param n Nilai integer.
 */
void soft_serial_print_int(int32_t n);

/**
 * @brief Mencetak karakter baris baru (CR LF: \r\n) ke port serial.
 */
void soft_serial_println(void);

/**
 * @brief Mencetak string teks diikuti karakter baris baru (\r\n).
 * @param s Pointer ke string teks.
 */
void soft_serial_println_str(const char *s);

/**
 * @brief Mencetak bilangan bulat diikuti karakter baris baru (\r\n).
 * @param n Nilai integer.
 */
void soft_serial_println_int(int32_t n);

#endif // SOFT_SERIAL_H
