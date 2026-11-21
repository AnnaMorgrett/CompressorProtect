#include "Params.h"

// Mendefinisikan memori untuk variabel global yang ada di Params.h
SystemParams params;

// --- Pengaturan Alamat EEPROM ---
const int EEPROM_MAGIC_ADDR = 0;   // Alamat 0 untuk cek status EEPROM
const byte EEPROM_MAGIC_VAL = 0xA5; // Kode penanda unik (bebas nilainya)
const int EEPROM_DATA_ADDR = 1;    // Data struktur disimpan mulai dari alamat 1

void resetSettingsToDefault() {
  // 1. Basic & UI Mode (Kalibrasi C-1 & C-2 TIDAK disentuh)
  params.timeDelay = 300;
  params.displayMode = 5;
  
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

  // 5. Refrigerant Flow (NTC2 - Kalibrasi C-3 TIDAK disentuh)
  params.ntc2Enable = false;
  params.refFlowDelay = 300;
  params.refDeltaTemp = 5;
  params.flowFaultRetryLimit = 5;

  // 6. Over Running Protection
  params.orpTimeLimit = 420;
  params.orpRestDelay = 30;

  saveParams();
}

void resetCalibrationToDefault() {
  params.currentMultiplier = 0.505;
  params.voltageMultiplier = 0.856;
  params.tempOffset = 0;
  saveParams();
}

void resetAllToDefault() {
  resetCalibrationToDefault();
  resetSettingsToDefault();
}

void loadParams() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VAL) {
    EEPROM.get(EEPROM_DATA_ADDR, params);
    if (params.displayMode < 1 || params.displayMode > 5) {
      params.displayMode = 5;
      saveParams();
    }
  } else {
    // EEPROM baru/kosong -> isi default keseluruhan
    resetAllToDefault();
  }
}

void saveParams() {
  // Simpan struct ke EEPROM (Fungsi put otomatis hanya menulis jika data berubah)
  EEPROM.put(EEPROM_DATA_ADDR, params);
  // Tulis magic byte sebagai penanda sistem sudah dikalibrasi/disimpan
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
}
