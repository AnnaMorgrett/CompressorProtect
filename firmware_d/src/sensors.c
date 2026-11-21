#include "sensors.h"
#include "board.h"
#include "params.h"
#include "sys_time.h"
#include "ch32fun.h"

// ============================================================================
// IMPLEMENTASI PENGUKURAN LISTRIK TRUE-RMS & AKUISISI SENSOR ANALOG
// ============================================================================

/**
 * @brief Algoritma Integer Square Root 64-bit (isqrt64) berbasis Binary Restoration.
 *        Fungsi ini menghitung akar kuadrat dari bilangan bulat 64-bit secara eksak
 *        tanpa menggunakan pustaka math.h atau unit floating-point.
 *        Penghematan memori Flash mencapai ~2.5 KB dibandingkan komputasi float.
 * @param val Nilai kuadrat 64-bit.
 * @return Nilai akar kuadrat 32-bit.
 */
static uint32_t isqrt64(uint64_t val) {
    if (val == 0) return 0;
    uint64_t op  = val;
    uint64_t res = 0;
    uint64_t one = (uint64_t)1 << 62; // Bit penguji tertinggi (posisi genap kedua tertinggi)

    // Cari posisi awal bit 'one' yang sesuai dengan rentang nilai 'op'
    while (one > op) one >>= 2;

    // Iterasi ekstraksi akar biner bit demi bit
    while (one != 0) {
        if (op >= res + one) {
            op  -= res + one;
            res  = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return (uint32_t)res;
}

/**
 * @brief Pemetaan dari pin fisik mikrokontroler ke nomor kanal analog ADC1 internal CH32V003.
 * @param pin Identifier pin (PA2, PA1, PD6).
 * @return Nomor kanal ADC (AIN0, AIN1, AIN6).
 */
static uint8_t pin_to_channel(uint8_t pin) {
    if (pin == PA2) return 0; // AIN0 = Sensor Tegangan ZMPT101B
    if (pin == PA1) return 1; // AIN1 = Sensor Arus ZMCT103C
    if (pin == PD6) return 6; // AIN6 = Tangga Resistor Tombol Panel
    return 0;
}

/**
 * @brief Membaca satu sampel konversi ADC 10-bit pada kanal pin yang ditentukan.
 *        Menggunakan bit SWSTART (Software Trigger) dan menunggu hingga flag EOC
 *        (End of Conversion) bernilai 1.
 * @param pin Pin mikrokontroler target.
 * @return Data digital ADC 10-bit (rentang 0 - 1023).
 */
uint16_t sensors_read_adc(uint8_t pin) {
    uint8_t ch = pin_to_channel(pin);
    ADC1->RSQR3 = ch;                     // Pilih kanal ADC pada register urutan konversi
    ADC1->CTLR2 |= ADC_SWSTART;           // Picu konversi ADC melalui software
    while (!(ADC1->STATR & ADC_EOC));     // Tunggu konversi selesai (flag EOC aktif)
    return (uint16_t)(ADC1->RDATAR & 0x3FF); // Ambil 10-bit hasil konversi
}

/**
 * @brief Inisialisasi periferal ADC1, clocking, mode analog GPIO, dan kalibrasi silikon.
 */
void sensors_init(void) {
    // 1. Aktifkan jalur clock bus APB2 untuk ADC1, Port A, dan Port D
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOD;

    // 2. Konfigurasi GPIO pin sensor ke mode Input Analog murni (menonaktifkan input buffer digital)
    funPinMode(PIN_VOLTAGE_SENS, GPIO_CNF_IN_ANALOG);
    funPinMode(PIN_CURRENT_SENS, GPIO_CNF_IN_ANALOG);
    funPinMode(PIN_BUTTONS,      GPIO_CNF_IN_ANALOG);

    // 3. Konfigurasi ADC Clock Prescaler: 48 MHz / 8 = 6 MHz (sesuai spesifikasi WCH ADC <= 14 MHz)
    RCC->CFGR0 &= ~RCC_ADCPRE;
    RCC->CFGR0 |= RCC_ADCPRE_DIV8;

    // 4. Nyalakan modul ADC (bit ADON) dan pilih trigger konversi software
    ADC1->CTLR1 = 0;
    ADC1->CTLR2 = ADC_ADON | ADC_EXTSEL;
    Delay_Ms(2); // Beri waktu stabilisasi analog bandgap internal

    // 5. Kalibrasi Otomatis ADC internal CH32V003:
    // a. Reset kalibrasi sebelumnya
    ADC1->CTLR2 |= ADC_RSTCAL;
    while (ADC1->CTLR2 & ADC_RSTCAL);

    // b. Jalankan kalibrasi komparator internal
    ADC1->CTLR2 |= ADC_CAL;
    while (ADC1->CTLR2 & ADC_CAL);

    // 6. Atur Waktu Sampling: 71.5 cycle ADC untuk kanal AIN0, AIN1, dan AIN6.
    // Waktu sampling panjang memberikan impedansi input tinggi dan akurasi maksimal.
    ADC1->SAMPTR2 = (7 << 0) | (7 << 3) | (7 << 18);
}

/**
 * @brief Menghitung True-RMS sinyal AC dengan Teknik Anti-Crosstalk dan Jendela Waktu 40 ms.
 *
 * Mengapa 40 ms?
 * Frekuensi listrik PLN di Indonesia adalah 50 Hz, di mana 1 siklus penuh = 20 ms.
 * Waktu sampling 40 ms mencakup tepat 2 siklus gelombang sinus AC murni.
 * Hal ini mengeliminasi kesalahan pemotongan fasa (phase truncation error).
 *
 * Teknik Anti-Crosstalk:
 * Sebelum melakukan sampling aktual, dilakukan 2 kali pembacaan awal (dummy read)
 * agar kapasitor multiplexer ADC sempat mengisi muatan sesuai kanal baru.
 *
 * Komputasi Varians (True-RMS):
 * Sinyal AC berosilasi di sekitar offset DC (~VDD/2).
 * Varians sinyal: Var = E[X^2] - (E[X])^2
 * RMS = sqrt(Var)
 * Hasil RMS diskalakan ke format fixed-point Q8 (dikalikan 256) untuk menjaga presisi.
 *
 * @param pin Pin analog target (Tegangan atau Arus).
 * @return Nilai RMS mentah dalam skala Q8 (x256).
 */
static uint32_t get_true_rms_q8(uint8_t pin) {
    // 1. Pembacaan dummy untuk stabilisasi MUX ADC (mencegah crosstalk antar kanal)
    sensors_read_adc(pin);
    Delay_Us(100);
    sensors_read_adc(pin);
    Delay_Us(50);

    uint32_t start  = micros();
    int32_t  offset = sensors_read_adc(pin); // Estimasi titik tengah DC awal
    int32_t  sum    = 0;
    uint64_t sum_sq = 0;
    uint32_t count  = 0;

    // 2. Loop sampling kontinu selama tepat 40.000 mikrodetik (40 ms = 2 siklus 50 Hz)
    while ((micros() - start) < 40000UL) {
        int32_t val = (int32_t)sensors_read_adc(pin) - offset;
        sum    += val;
        sum_sq += (uint64_t)((int64_t)val * val);
        count++;
    }

    if (count == 0) return 0;

    // 3. Hitung rata-rata sinyal dan rata-rata kuadrat sinyal
    int32_t  mean     = sum / (int32_t)count;
    uint64_t mean_sq  = sum_sq / (uint64_t)count;
    int64_t  variance = (int64_t)mean_sq - ((int64_t)mean * mean);
    if (variance < 0) variance = 0;

    // 4. Hitung akar kuadrat dalam format Q8 (geser kiri 16 bit sebelum diakar = hasil x256)
    return isqrt64((uint64_t)variance << 16);
}

/**
 * @brief Membaca dan menghitung nilai tegangan AC PLN (Volt) dengan filter adaptif.
 *
 * Formula Konversi:
 * CH32V003 memiliki ADC 10-bit (0..1023), sedangkan firmware referensi Batch 1 (LGT8F328P)
 * menggunakan ADC 12-bit (0..4095).
 * Agar faktor kalibrasi C-1 pada buku manual (default: 855) bernilai 100% sama:
 * rms_12bit = rms_10bit * 4
 * V (decivolt) = rms_10bit * 4 * (C-1 / 1000) * 10
 * Dalam representasi Q8: (rms_q8 * C-1) / 6400
 *
 * Filter Adaptif Cerdas:
 * - Jika perubahan tegangan mendadak > 3.0 V (lonjakan/drop tiba-tiba) -> langsung diperbarui
 *   tanpa penundaan filter agar proteksi OVP/UVP merespons instan!
 * - Jika fluktuasi halus (< 3.0 V) -> dihaluskan dengan perbandingan 85% lama + 15% baru
 *   sehingga angka di layar tidak berkedip liar akibat ripple listrik.
 *
 * @return Tegangan dalam satuan Decivolts (0.1 V), misal 2205 = 220.5 Volt.
 */
uint16_t sensors_read_voltage_dv(void) {
    uint32_t rms_q8 = get_true_rms_q8(PIN_VOLTAGE_SENS);

    // Hard Noise Gate: Jika sinyal di bawah 5.0 count ADC -> anggap listrik padam (0 Volt)
    if (rms_q8 < (5U << 8)) return 0;

    // Hitung tegangan dalam satuan decivolt (0.1 V)
    uint32_t raw_dv = ((uint64_t)rms_q8 * params.voltage_multiplier_x1000) / 6400;

    static uint16_t filtered_dv = 0;
    if (filtered_dv == 0) {
        filtered_dv = (uint16_t)raw_dv;
        return filtered_dv;
    }

    // Filter Adaptif (Threshold 3.0 Volt = 30 Decivolt)
    int32_t diff = (int32_t)raw_dv - (int32_t)filtered_dv;
    if (diff < 0) diff = -diff;

    if (diff > 30) {
        filtered_dv = (uint16_t)raw_dv; // Respons instan terhadap perubahan besar
    } else {
        filtered_dv = (uint16_t)(((uint32_t)filtered_dv * 85 + raw_dv * 15) / 100); // Penghalusan fluktuasi kecil
    }

    // Cutoff: Tegangan di bawah 30.0 V dianggap kabel tidak bertegangan
    if (filtered_dv < 300) filtered_dv = 0;
    return filtered_dv;
}

/**
 * @brief Membaca dan menghitung nilai arus AC kompresor (mA) dengan filter adaptif.
 *
 * Formula Konversi:
 * Menyesuaikan skala ADC 10-bit ke 12-bit dengan faktor kalibrasi C-2 (default: 470).
 * Arus (mA) dalam representasi Q8: (rms_q8 * C-2 * 5) / 32
 *
 * Filter Adaptif Cerdas:
 * - Noise Gate: Arus di bawah 100 mA (0.1 A) dianggap noise induksi trafo CT -> dibulatkan ke 0 mA.
 * - Jika lonjakan arus > 100 mA (kompresor start / LRA / trip) -> instan tanpa jeda filter.
 * - Fluktuasi normal -> filter eksponensial 85:15.
 *
 * @return Arus beban dalam satuan Milliamperes (1 mA), misal 850 = 850 mA (0.85 A).
 */
uint16_t sensors_read_current_ma(void) {
    uint32_t rms_q8 = get_true_rms_q8(PIN_CURRENT_SENS);

    // Konversi nilai RMS ke satuan Milliampere
    uint32_t raw_ma = ((uint64_t)rms_q8 * params.current_multiplier_x1000 * 5) / 32;

    // Hard Noise Gate: Abaikan kebocoran sinyal di bawah 100 mA
    if (raw_ma < 100) return 0;

    static uint16_t filtered_ma = 0;
    if (filtered_ma == 0) {
        filtered_ma = (uint16_t)raw_ma;
        return filtered_ma;
    }

    // Filter Adaptif (Threshold 100 mA)
    int32_t diff = (int32_t)raw_ma - (int32_t)filtered_ma;
    if (diff < 0) diff = -diff;

    if (diff > 100) {
        filtered_ma = (uint16_t)raw_ma; // Respons cepat terhadap beban kompresor / inrush
    } else {
        filtered_ma = (uint16_t)(((uint32_t)filtered_ma * 85 + raw_ma * 15) / 100);
    }

    return filtered_ma;
}

/**
 * @brief Membaca sensor suhu NTC1 (Bodi Kompresor).
 *        Pada kemasan SOP-16 CH32V003, pin PC2 tidak memiliki kanal ADC internal,
 *        sehingga disimulasikan 25 °C + offset kalibrasi C-3 sesuai konfigurasi.
 */
int16_t sensors_read_temp1(void) {
#if NTC1_ENABLED
    return 25 + params.temp_offset;
#else
    return 0;
#endif
}

/**
 * @brief Membaca sensor suhu NTC2 (Pipa Filter Dryer).
 */
int16_t sensors_read_temp2(void) {
#if NTC2_ENABLED
    return 25 + params.temp_offset;
#else
    return 0;
#endif
}
