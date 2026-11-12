#include "Sensors.h"
#include <math.h>

void initSensors() {
  analogReadResolution(12);
  pinMode(PIN_VOLTAGE_SENS, INPUT);
  pinMode(PIN_CURRENT_SENS, INPUT);
  pinMode(PIN_TEMP_COMP, INPUT);
  pinMode(PIN_TEMP_DRYER, INPUT);
}

// Fungsi True RMS Presisi Tinggi dengan Anti-Cross-talk MUX
float getTrueRMS(uint8_t pin) {
  // 1. DUMMY READ & SETTLING TIME (Wajib untuk Mencegah Cross-talk A6 <-> A7)
  // Membuang sisa muatan kapasitor internal ADC dari pembacaan pin sebelumnya
  analogRead(pin);
  delayMicroseconds(100);
  analogRead(pin);
  delayMicroseconds(50);

  unsigned long startMicros = micros(); 
  
  int32_t offset = analogRead(pin); 
  int32_t sum = 0;
  uint64_t sumSq = 0; 
  uint32_t count = 0;

  // 2. SAMPLING TEPAT 40.000 Mikrodetik = 40 ms (Pas 2 Gelombang 50Hz)
  while (micros() - startMicros < 40000UL) {
    int32_t val = analogRead(pin) - offset;
    sum += val;
    sumSq += (uint64_t)((int64_t)val * val);
    count++;
  }

  if (count == 0) return 0;

  float mean = (float)sum / count;
  float meanSq = (float)sumSq / count;
  float variance = meanSq - (mean * mean);
  
  if (variance < 0) variance = 0; 

  return sqrt(variance);
}

float readVoltage() {
  float rmsADC = getTrueRMS(PIN_VOLTAGE_SENS);
  
  // 1. HARD NOISE GATE (Mati Lampu / Dicabut -> Respons 0 milidetik!)
  if (rmsADC < 5.0) {
    return 0.0;
  }

  float trueVoltage = rmsADC * params.voltageMultiplier; // Pengali 0.850
  static float filteredV = 0;
  
  // Tangkap nilai instan saat pertama kali dicolok
  if (filteredV == 0) {
    filteredV = trueVoltage;
    return filteredV;
  }
  
  // 2. ADAPTIVE FILTER LOGIC
  float diff = abs(trueVoltage - filteredV);
  
  if (diff > 3.0) {
    // JIKA NAIK/TURUN > 10 VOLT (PLN anjlok / lonjakan surge)
    // Langsung timpa tanpa jeda untuk proteksi seketika!
    filteredV = trueVoltage;
  } else {
    // JIKA CUMA FLUKTUASI KECIL (< 10 Volt)
    // Gunakan HEAVY SMOOTHING: 85% tahan nilai lama, 15% terima nilai baru.
    // Lonjakan liar ke 218.8 tadi akan langsung disetrika rata ke 217.x!
    filteredV = (0.85 * filteredV) + (0.15 * trueVoltage);
  }

  if (filteredV < 30) filteredV = 0;
  
  return filteredV;
}

float readCurrent() {
  // 1. BACA RMS MURNI
  float rmsADC = getTrueRMS(PIN_CURRENT_SENS);
  
  // 2. KALIKAN DENGAN MULTIPLIER (C-2)
  float trueCurrent = rmsADC * (params.currentMultiplier / 100); 
  
  static float filteredA = 0;

  // 3. HARD NOISE GATE BERSIH
  // Karena sinyal fisik sudah besar, noise sisa akan terbaca sangat kecil (di bawah 0.10A).
  // Jika terbaca di bawah 0.10A, langsung paksa 0 mutlak dan bersihkan memori filter.
  // (Ini sangat aman karena arus kompresor terkecil biasanya 0.3A ke atas, UCP tetap jalan)
  if (trueCurrent < 0.10) {
    filteredA = 0.0;
    return 0.0;
  }
  
  // Tangkap nilai instan saat kompresor pertama kali menyala (dari 0)
  if (filteredA == 0) {
    filteredA = trueCurrent;
    return filteredA;
  }
  
  // 4. ADAPTIVE FILTER (Super Anteng & Responsif)
  float diff = abs(trueCurrent - filteredA);
  
  if (diff > 0.10) {
    // JIKA LONJAKAN > 300mA (Misal saat kompresor start awal atau macet LRA)
    // Tembus filter seketika! Respons proteksi 0 detik!
    filteredA = trueCurrent;
  } else {
    // JIKA HANYA FLUKTUASI GETARAN KECIL
    // Gunakan filter 85:15 persis seperti voltase agar angka di layar membeku stabil
    filteredA = (0.85 * filteredA) + (0.15 * trueCurrent);
  }
  
  return filteredA;
}

// --- PEMBACAAN SUHU NTC (SUDAH 12-BIT) ---
float getTempNTC(uint8_t pin) {
  int adcVal = analogRead(pin);
  
  // 1. UBAH BATAS MAKSIMAL MENJADI 4095
  if (adcVal == 0 || adcVal >= 4095) return 0; 
  
  // 2. UBAH FAKTOR PEMBAGI MENJADI 4095.0
  float resistance = 10000.0 / (4095.0 / adcVal - 1.0); 
  
  float steinhart = resistance / 10000.0;     
  steinhart = log(steinhart);                 
  steinhart /= 3950.0;                        
  steinhart += 1.0 / (25.0 + 273.15);         
  steinhart = 1.0 / steinhart;                
  steinhart -= 273.15;                        
  
  return steinhart;
}

float readTemp1() {
  if (!params.ntc1Enable) return 0.0; 
  return getTempNTC(PIN_TEMP_COMP) + params.tempOffset;
}

float readTemp2() {
  if (!params.ntc2Enable) return 0.0;
  return getTempNTC(PIN_TEMP_DRYER) + params.tempOffset;
}
