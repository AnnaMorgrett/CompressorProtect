#include "serial_cli.h"
#include "soft_serial.h"
#include "params.h"
#include "sensors.h"
#include "ui.h"

// ============================================================================
// IMPLEMENTASI SERIAL CLI & TELEMETRI MONITORING
// ============================================================================

bool serial_menu_active = false;
bool serial_menu_stream = false;

#if ENABLE_SERIAL_DEBUG

// Buffer penampung karakter string perintah CLI
static char    cmd_buf[24];
static uint8_t cmd_idx = 0;

// Daftar nama dan rentang nilai parameter S-01 sampai S-20 untuk tampilan bantuan CLI
static const char *const PARAM_NAMES[20] = {
    "Delay(m)[2..10]",        // S-01: Delay Kompresor Start
    "OVP(V)[230..260]",       // S-02: Batas Over-Voltage
    "UVP(V)[150..200]",       // S-03: Batas Under-Voltage
    "V-Dly(s)[3..10]",        // S-04: Delay Toleransi OVP/UVP
    "V-Hys(V)[10..40]",       // S-05: Histeresis Tegangan Pemulihan
    "LRA(mA)[800..5000]",     // S-06: Batas Arus Locked Rotor
    "LRA-T(s)[1..5]",         // S-07: Durasi Jendela Inrush LRA
    "LRA-R[2..5]",            // S-08: Batas Percobaan Start Ulang LRA
    "OCP(mA)[300..4500]",     // S-09: Batas Arus Lebih Running
    "OCP-D(s)[2..10]",        // S-10: Delay Toleransi OCP
    "UCP(mA)[0,200..1000]",   // S-11: Batas Arus Bocor Rendah (0=OFF)
    "Min-A(mA)[0,150..400]",  // S-12: Filter Arus Lampu (0=OFF)
    "NTC1[0=lock]",           // S-13: Status Sensor NTC1 Bodi Kompresor (Kunci 0 pada SOP-16)
    "OTP(C)[70..90]",         // S-14: Batas Over-Temperature
    "OTP-H(C)[5..20]",        // S-15: Histeresis Suhu Pemulihan OTP
    "NTC2[0=lock]",           // S-16: Status Sensor NTC2 Filter Dryer (Kunci 0 pada SOP-16)
    "Flw-D(m)[2..10]",        // S-17: Waktu Cek Aliran Freon
    "Flw-R[2..5]",            // S-18: Batas Percobaan Aliran Freon
    "ORP-T(h)[2..12]",        // S-19: Batas Jam Kerja Nonstop
    "ORP-R(m)[20..60]"        // S-20: Waktu Istirahat Kompresor ORP
};

/**
 * @brief Membandingkan dua string teks tanpa membedakan huruf besar/kecil (case-insensitive).
 */
static bool cmd_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb) return false;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/**
 * @brief Mengonversi string angka desimal menjadi integer bertanda 32-bit.
 */
static int32_t parse_uint(const char *s, bool *ok) {
    if (*s < '0' || *s > '9') {
        *ok = false;
        return 0;
    }
    int32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = (v * 10) + (*s - '0');
        s++;
    }
    *ok = (*s == '\0');
    return v;
}

/**
 * @brief Memasuki mode Menu Konfigurasi Serial.
 *        1. Mematikan relay kompresor demi keamanan.
 *        2. Mengubah tampilan TM1637 menjadi " PC " sebagai penanda mode terminal.
 *        3. Menghentikan stream telemetri agar layar terminal tidak terganggu saat membaca data.
 */
static void enter_serial_menu(void) {
    if (!serial_menu_active) {
        serial_menu_active = true;
        serial_menu_stream = false;
        funDigitalWrite(PIN_RELAY, FUN_LOW); // Matikan kompresor
        ui_show_serial_menu_display();       // Tampilkan " PC " di layar 7-segment
        soft_serial_println_str("\n=== MENU MODE (TX PAUSED) ===");
        soft_serial_println_str("Cmd: -h (help), $$ (list), $x=val, run");
    }
}

/**
 * @brief Keluar dari Menu Konfigurasi Serial dan melanjutkan operasi proteksi normal.
 *        Kompresor akan memasuki Startup Delay untuk memastikan tekanan freon seimbang.
 */
static void exit_serial_menu(void) {
    serial_menu_active = false;
    serial_menu_stream = false;
    current_state = STATE_STARTUP_DELAY;
    state_timer = millis();
    soft_serial_println_str("\n[RUN] Resuming...\n");
}

/**
 * @brief Menampilkan petunjuk perintah dan contoh penggunaan CLI pada terminal serial.
 */
static void print_help(void) {
    soft_serial_println_str("\n--- HELP & EXAMPLES ---");
    soft_serial_println_str("menu        Pause TX & enter menu");
    soft_serial_println_str("-h | help   Show this help");
    soft_serial_println_str("$$          View all settings ($1..$22)");
    soft_serial_println_str("$<id>=<val> Edit & save parameter");
    soft_serial_println_str("read        Read 1 sensor snapshot");
    soft_serial_println_str("stream      Toggle live TX in menu");
    soft_serial_println_str("defaults    Reset factory defaults");
    soft_serial_println_str("run | exit  Exit menu & resume");
    soft_serial_println_str("reboot      Restart controller");
    soft_serial_println_str("\nExamples:");
    soft_serial_println_str("  $1=5      -> Startup delay = 5 min");
    soft_serial_println_str("  $2=250    -> OVP limit = 250 V");
    soft_serial_println_str("  $6=1500   -> LRA limit = 1500 mA");
    soft_serial_println_str("  $11=0     -> UCP Off");
    soft_serial_println_str("  $C1=855   -> Voltage cal ($21)");
    soft_serial_println_str("  $C2=470   -> Current cal ($22)");
    soft_serial_println_str("-----------------------\n");
}

/**
 * @brief Menampilkan daftar seluruh parameter S-01..S-20 dan kalibrasi C-1..C-2.
 */
static void print_settings(void) {
    soft_serial_println_str("\n--- SETTINGS ($1..$22) ---");
    for (uint8_t i = 0; i < 20; i++) {
        soft_serial_print_char('$');
        soft_serial_print_int(i + 1);
        soft_serial_print_char('=');
        soft_serial_print_int(ui_get_param_val(i));
        soft_serial_print_str(" (");
        soft_serial_print_str(PARAM_NAMES[i]);
        soft_serial_println_str(")");
    }
    // Parameter kalibrasi C-1 & C-2
    soft_serial_print_str("$21=");
    soft_serial_print_int(params.voltage_multiplier_x1000);
    soft_serial_println_str(" (Cal-C1:V-Mult)");

    soft_serial_print_str("$22=");
    soft_serial_print_int(params.current_multiplier_x1000);
    soft_serial_println_str(" (Cal-C2:A-Mult)");
    soft_serial_println_str("--------------------------\n");
}

/**
 * @brief Memproses perintah penugasan parameter bergaya GRBL ($<id>=<nilai>).
 *        Contoh: "$1=5" (S-01 diubah ke 5 menit), "$C1=855" (Kalibrasi tegangan).
 *        Nilai langsung disimpan secara permanen ke memori Option Bytes Flash.
 */
static void handle_grbl_assign(char *cmd) {
    char *eq = 0;
    for (char *p = cmd; *p; p++) {
        if (*p == '=') { eq = p; break; }
    }
    if (!eq) {
        soft_serial_println_str("[ERR] Use $<id>=<val>");
        return;
    }
    *eq = '\0';
    char *val_str = eq + 1;
    while (*val_str == ' ') val_str++;

    bool val_ok = false;
    int32_t val = parse_uint(val_str, &val_ok);
    if (!val_ok) {
        soft_serial_println_str("[ERR] Invalid value");
        return;
    }

    char *id_str = cmd + 1;
    while (*id_str == ' ') id_str++;

    // Penanganan khusus kalibrasi C-1 ($C1 atau $21)
    if (cmd_eq(id_str, "C1") || cmd_eq(id_str, "21")) {
        if (val < 1 || val > 5000) { soft_serial_println_str("[ERR] Range: 1..5000"); return; }
        params.voltage_multiplier_x1000 = (uint16_t)val;
        params_save();
        soft_serial_print_str("[OK] $21=");
        soft_serial_println_int(val);
        return;
    }
    // Penanganan khusus kalibrasi C-2 ($C2 atau $22)
    if (cmd_eq(id_str, "C2") || cmd_eq(id_str, "22")) {
        if (val < 1 || val > 5000) { soft_serial_println_str("[ERR] Range: 1..5000"); return; }
        params.current_multiplier_x1000 = (uint16_t)val;
        params_save();
        soft_serial_print_str("[OK] $22=");
        soft_serial_println_int(val);
        return;
    }

    // Penanganan parameter S-01 sampai S-20 ($1..$20)
    bool id_ok = false;
    int32_t id = parse_uint(id_str, &id_ok);
    if (!id_ok || id < 1 || id > 20) {
        soft_serial_println_str("[ERR] Param $1..$22");
        return;
    }

    uint8_t idx = id - 1;
    // Parameter S-13 dan S-16 dikunci karena keterbatasan ADC pin SOP-16
    if (idx == 12 || idx == 15) {
        soft_serial_println_str("[ERR] Hardware locked on SOP-16");
        return;
    }

    ui_set_param_val(idx, val);
    params_save(); // Simpan ke flash
    soft_serial_print_str("[OK] $");
    soft_serial_print_int(id);
    soft_serial_print_char('=');
    soft_serial_println_int(val);
}

/**
 * @brief Mencetak nilai tegangan dalam format desimal 1 angka di belakang koma (misal: "220.5").
 */
static void print_decivolts(uint16_t dv) {
    soft_serial_print_int(dv / 10);
    soft_serial_print_char('.');
    soft_serial_print_int(dv % 10);
}

/**
 * @brief Mencetak nilai suhu dalam format 1 desimal (misal: "25.0").
 */
static void print_one_decimal(int16_t value) {
    soft_serial_print_int(value);
    soft_serial_print_str(".0");
}

/**
 * @brief Mencetak nilai arus beban dalam satuan Ampere dengan 2 angka di belakang koma (misal: "1.25").
 */
static void print_amps(uint16_t ma) {
    uint16_t hundredths = (ma + 5) / 10;
    soft_serial_print_int(hundredths / 100);
    soft_serial_print_char('.');
    if (hundredths % 100 < 10) soft_serial_print_char('0');
    soft_serial_print_int(hundredths % 100);
}

/**
 * @brief Parser dan pengambil keputusan eksekusi perintah teks dari serial CLI.
 */
static void execute_cmd(char *cmd) {
    // Hilangkan spasi di awal string
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    uint8_t len = 0;
    while (cmd[len]) len++;
    // Hilangkan spasi di akhir string
    while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) cmd[--len] = '\0';
    if (len == 0) return;

    if (cmd_eq(cmd, "menu") || cmd_eq(cmd, "m")) {
        enter_serial_menu();
    } else if (cmd_eq(cmd, "-h") || cmd_eq(cmd, "help") || cmd_eq(cmd, "?")) {
        enter_serial_menu();
        print_help();
    } else if (cmd_eq(cmd, "$$")) {
        enter_serial_menu();
        print_settings();
    } else if (cmd_eq(cmd, "read") || cmd_eq(cmd, "status")) {
        enter_serial_menu();
        soft_serial_print_str("\nV:");
        print_decivolts(sensors_read_voltage_dv());
        soft_serial_print_str("V | A:");
        soft_serial_print_int(sensors_read_current_ma());
        soft_serial_println_str("mA\n");
    } else if (cmd_eq(cmd, "stream")) {
        enter_serial_menu();
        serial_menu_stream = !serial_menu_stream;
        soft_serial_print_str("[MENU] Stream: ");
        soft_serial_println_str(serial_menu_stream ? "ON" : "PAUSED");
    } else if (cmd_eq(cmd, "defaults") || cmd_eq(cmd, "$RST=*")) {
        enter_serial_menu();
        params_reset_defaults();
        soft_serial_println_str("[OK] Defaults restored.");
        print_settings();
    } else if (cmd_eq(cmd, "run") || cmd_eq(cmd, "exit")) {
        if (serial_menu_active) exit_serial_menu();
    } else if (cmd_eq(cmd, "reboot")) {
        system_reboot();
    } else if (cmd[0] == '$') {
        enter_serial_menu();
        handle_grbl_assign(cmd);
    } else {
        enter_serial_menu();
        soft_serial_println_str("[ERR] Unknown cmd (type -h)");
    }
}

void serial_cli_init(void)            { soft_serial_begin(9600); }
void serial_cli_enable_rx(void)       { soft_serial_enable_rx(); }
bool serial_cli_rx_incoming(void)     { return soft_serial_rx_start_detected(); }
void serial_cli_log_str(const char *s) { soft_serial_println_str(s); }

/**
 * @brief Mengirimkan notifikasi trip kegagalan proteksi ke terminal serial.
 */
void serial_cli_log_fault(int32_t fault_code) {
    soft_serial_print_str("\n[FAULT] ");
    soft_serial_println_int(fault_code);
}

/**
 * @brief Mengirimkan telemetri lengkap secara berkala (setiap 1 detik).
 *        Menyertakan string visual TM1637:[xxxx] sehingga teknisi di terminal
 *        dapat melihat secara persis apa yang sedang dilihat pengguna di layar fisik!
 */
void serial_cli_send_telemetry(void) {
    soft_serial_print_str("TM1637:[");
    soft_serial_print_str(current_display_str);
    soft_serial_print_str("] | MODE:");
    soft_serial_print_int(current_mode);
    soft_serial_print_str(" | ST:");
    soft_serial_print_int(current_state);
    soft_serial_print_str(" | FLT:");
    soft_serial_print_int(current_fault);
    soft_serial_print_str(" | V:");
    print_decivolts(sys_volt_dv);
    soft_serial_print_str(" | A:");
    print_amps(sys_amp_ma);
    soft_serial_print_str(" | T1:");
    print_one_decimal(sys_temp1);
    soft_serial_print_str(" | T2:");
    print_one_decimal(sys_temp2);
    soft_serial_println();
}

/**
 * @brief Polling karakter masuk dari software serial buffer.
 *        Mendukung tombol enter (\r atau \n) untuk eksekusi perintah dan backspace (\b / 127).
 */
void serial_cli_poll(void) {
    while (soft_serial_available() > 0) {
        char c = (char)soft_serial_read();
        if (c == '\r' || c == '\n') {
            if (cmd_idx > 0) {
                cmd_buf[cmd_idx] = '\0';
                execute_cmd(cmd_buf);
                cmd_idx = 0;
            }
        } else if (c == '\b' || c == 127) { // Penanganan tombol Backspace
            if (cmd_idx > 0) cmd_idx--;
        } else if (cmd_idx < sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_idx++] = c;
        }
    }
}

#else

void serial_cli_init(void)             {}
void serial_cli_enable_rx(void)        {}
bool serial_cli_rx_incoming(void)      { return false; }
void serial_cli_poll(void)             {}
void serial_cli_send_telemetry(void)   {}
void serial_cli_log_str(const char *s) { (void)s; }
void serial_cli_log_fault(int32_t f)   { (void)f; }

#endif
