#ifndef SERIAL_CLI_H
#define SERIAL_CLI_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// MODUL SERIAL CLI (COMMAND LINE INTERFACE) & TELEMETRI GRBL-STYLE
// ============================================================================
// Modul ini menyediakan antarmuka diagnostik, pemantauan telemetri real-time,
// dan konfigurasi parameter sistem melalui port serial (PC7 TX, PC6 RX).
// Karakteristik utama:
// 1. Format perintah mirip GRBL ($1=5, $2=250, $$, -h, dll).
// 2. Fitur "menu" otomatis memutus relay kompresor dan menghentikan streaming
//    telemetri agar teknisi dapat fokus membaca dan mengedit parameter.
// 3. Mirroring tampilan 7-segment ke serial: "TM1637:[xxxx]" setiap 1 detik.
// ============================================================================

// Status apakah sistem sedang berada di dalam Menu Konfigurasi Serial
extern bool serial_menu_active;

// Status apakah live stream sensor aktif saat berada di menu
extern bool serial_menu_stream;

/**
 * @brief Inisialisasi port komunikasi serial CLI (9600 baud, 8N1).
 */
void serial_cli_init(void);

/**
 * @brief Mengaktifkan pin penerima serial RX (PC6).
 */
void serial_cli_enable_rx(void);

/**
 * @brief Memeriksa apakah ada sinyal karakter masuk dari terminal serial PC.
 * @return true jika pin RX mendeteksi sinyal start-bit.
 */
bool serial_cli_rx_incoming(void);

/**
 * @brief Memproses buffer karakter serial yang masuk, mengekstrak baris perintah,
 *        dan mengeksekusi instruksi CLI yang sesuai.
 */
void serial_cli_poll(void);

/**
 * @brief Mengirimkan satu baris data telemetri real-time yang mencakup:
 *        - Teks yang sedang tampil di layar 7-segment (TM1637)
 *        - Mode Operasi (MODE)
 *        - State Mesin Status (ST)
 *        - Kode Gangguan Aktif (FLT)
 *        - Nilai Tegangan AC (V)
 *        - Nilai Arus AC Beban (A)
 *        - Suhu Sensor NTC1 (T1) & NTC2 (T2)
 */
void serial_cli_send_telemetry(void);

/**
 * @brief Mencetak string pesan log sistem ke terminal serial.
 * @param s String teks null-terminated.
 */
void serial_cli_log_str(const char *s);

/**
 * @brief Mencetak notifikasi trip gangguan (Fault Code) ke terminal serial.
 * @param fault_code Nomor kode kesalahan (1..9).
 */
void serial_cli_log_fault(int32_t fault_code);

#endif // SERIAL_CLI_H
