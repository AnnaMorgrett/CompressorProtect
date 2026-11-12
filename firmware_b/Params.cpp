#include "Params.h"

// Mendefinisikan memori untuk variabel global yang ada di Params.h
SystemParams params;

// --- Pengaturan Alamat EEPROM ---
const int EEPROM_MAGIC_ADDR = 0;   // Alamat 0 untuk cek status EEPROM
const byte EEPROM_MAGIC_VAL = 0xA5; // Kode penanda unik (bebas nilainya)
const int EEPROM_DATA_ADDR = 1;    // Data struktur disimpan mulai dari alamat 1

void resetToDefaultParams() {
  // 1. Basic & Calibration
  params.timeDelay = 300;
  params.currentMultiplier = 0.470;
  params.voltageMultiplier = 0.855;
  
  // 2. Voltage Protection
  params.ovpLimit = 250;
  params.uvpLimit = 180;
  params.voltageHysteresis = 10;
  params.ovpUvpDelay = 5;
  params.powerSurgeLimit = 270;
  params.blackoutLimit = 140;

  // 3. Current Protection
  params.lraLimit = 1.5;
  params.lraTime = 2;
  params.lraRetryLimit = 3;
  params.ocpLimit = 0.8;
  params.ocpDelay = 4;
  params.ucpLimit = 0;
  params.minCurrentThreshold = 0.25;

  // 4. Temperature Protection (NTC1)
  params.ntc1Enable = false;
  params.otpLimit = 85;
  params.otpHysteresis = 10;

  // 5. Refrigerant Flow (NTC2)
  params.tempOffset = 0;
  params.ntc2Enable = false;
  params.refFlowDelay = 300;
  params.refDeltaTemp = 5;
  params.flowFaultRetryLimit = 5;

  // 6. Over Running Protection
  params.orpTimeLimit = 420;
  params.orpRestDelay = 30;

  // Simpan nilai default ini ke EEPROM
  saveParams();
}

void loadParams() {
  // Cek apakah EEPROM sudah pernah ditulisi oleh program kita
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VAL) {
    // Jika ada penanda, baca data ke struct params
    EEPROM.get(EEPROM_DATA_ADDR, params);
  } else {
    // Jika tidak ada (EEPROM baru/kosong), kembalikan ke default
    resetToDefaultParams();
  }
}

void saveParams() {
  // Simpan struct ke EEPROM (Fungsi put otomatis hanya menulis jika data berubah)
  EEPROM.put(EEPROM_DATA_ADDR, params);
  // Tulis magic byte sebagai penanda sistem sudah dikalibrasi/disimpan
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
}
