#include "UI.h"
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"

// Inisialisasi Library TM1637
TM1637Display display(PIN_TM_CLK, PIN_TM_DAT);

// ==========================================
// DEFINISI HURUF CUSTOM 7-SEGMENT 
// ==========================================
const uint8_t LTR_U = SEG_B | SEG_C | SEG_D | SEG_E | SEG_F; // Huruf U (Volt)
const uint8_t LTR_E = SEG_A | SEG_D | SEG_E | SEG_F | SEG_G; // Huruf E
const uint8_t LTR_r = SEG_E | SEG_G;                         // Huruf r kecil
const uint8_t LTR_L = SEG_D | SEG_E | SEG_F;                 // Huruf L
const uint8_t LTR_O = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F; // Huruf O
const uint8_t LTR_C = SEG_A | SEG_D | SEG_E | SEG_F;         // Huruf C
const uint8_t LTR_S = SEG_A | SEG_C | SEG_D | SEG_F | SEG_G; // Huruf S
const uint8_t LTR_P = SEG_A | SEG_B | SEG_E | SEG_F | SEG_G; // Huruf P

const uint8_t BLANK = 0x00;

// ==========================================
// IMPLEMENTASI FUNGSI
// ==========================================
void initUI() {
  display.setBrightness(5); 
  
  // Improvisasi Booting: Tampil "SCP " 1.5 detik, lalu "PrO " 1 detik
  uint8_t msgSCP[] = { LTR_S, LTR_C, LTR_P, BLANK };
  display.setSegments(msgSCP);
  delay(1500); 
  
  uint8_t msgPro[] = { LTR_P, LTR_r, LTR_O, BLANK };
  display.setSegments(msgPro);
  delay(1000);
  
  display.clear();
}

void updateDisplay(float currentV, float currentA) {
  // ==========================================
  // FITUR SCREEN SAVER (AWET 7-SEGMENT)
  // ==========================================
  static unsigned long brightTimer = millis(); // Otomatis aktif saat pertama kali boot
  static FaultType lastDisplayFault = FAULT_NONE;
  static SystemState lastDisplayState = STATE_STARTUP_DELAY;

  // 1. Deteksi Jika Ada Error Baru atau Siklus Delay Baru
  if (currentFault != lastDisplayFault) {
    lastDisplayFault = currentFault;
    if (currentFault != FAULT_NONE) brightTimer = millis(); // Layar terang jika ada error!
  }
  
  if (currentState != lastDisplayState) {
    lastDisplayState = currentState;
    if (currentState == STATE_STARTUP_DELAY) brightTimer = millis(); // Layar terang saat mulai menghitung mundur delay
  }

  // 2. Eksekusi Kecerahan (Tahan 10 detik)
  if (millis() - brightTimer <= 10000UL) {
    display.setBrightness(4); // Terang (~70%)
  } else {
    display.setBrightness(1); // Redup awet (~20%)
  }
  
  bool isAltScreen = (millis() / 2000) % 2 == 1; 

  // --- MENGHITUNG SISA WAKTU (HANYA BERJALAN SAAT DELAY AMAN ATAU ORP) ---
  long remainingSec = 0;
  if (currentState == STATE_STARTUP_DELAY) {
    remainingSec = (params.timeDelay * 1000UL) - (millis() - stateTimer);
  } else if (currentState == STATE_ORP_REST) {
    remainingSec = (params.orpRestDelay * 60000UL) - (millis() - stateTimer);
  }
  
  if (remainingSec < 0) remainingSec = 0;
  remainingSec /= 1000; 

  // ==========================================
  // LOGIKA TAMPILAN BERDASARKAN STATUS
  // ==========================================
  
  // 1. KONDISI RUNNING NORMAL
  if (currentState == STATE_RUNNING) {
    if (isAltScreen) {
      int v = (int)currentV;
      uint8_t dataV[] = {
        v > 99 ? display.encodeDigit(v / 100) : BLANK,
        display.encodeDigit((v / 10) % 10),
        display.encodeDigit(v % 10),
        LTR_U
      };
      display.setSegments(dataV);
    } else {
      int mA = (int)(currentA * 1000);
      display.showNumberDec(mA, false, 4, 0); 
    }
  }
  
  // 2. KONDISI FAULT MURNI (TEGANGAN DROP/OVER -> TUNGGU NORMAL, NO COUNTDOWN!)
  else if (currentState == STATE_FAULT) {
    uint8_t dataErr[] = { LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault) };
    display.setSegments(dataErr);
  }
  
  // 3. KONDISI STARTUP DELAY & ORP REST (COUNTDOWN BERJALAN)
  else if (currentState == STATE_STARTUP_DELAY || currentState == STATE_ORP_REST) {
    if (remainingSec > 60) {
      // Masih > 1 Menit: Flip-flop antara sisa Menit dan histori Error sebelumnya
      if (isAltScreen && currentFault != FAULT_NONE) {
        uint8_t dataErr[] = { LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault) };
        display.setSegments(dataErr);
      } else {
        display.showNumberDec((remainingSec / 60) + 1, false, 4, 0);
      }
    } 
    else {
      // <= 60 Detik: Murni countdown detik tanpa flip-flop Error
      display.showNumberDec(remainingSec, false, 4, 0);
    }
  }
  
  // 4. KONDISI LOCKOUT (MACET TOTAL)
  else if (currentState == STATE_LOCKOUT) {
    if (isAltScreen) {
      uint8_t dataErr[] = { LTR_E, LTR_r, LTR_r, display.encodeDigit(currentFault) };
      display.setSegments(dataErr);
    } else {
      uint8_t locReason = (currentFault == FAULT_LRA) ? 1 : 2;
      uint8_t dataLoc[] = { LTR_L, LTR_O, LTR_C, display.encodeDigit(locReason) };
      display.setSegments(dataLoc);
    }
  }
}

// ==========================================
// LOGIKA MENU SETTING & PEMBACAAN TOMBOL
// ==========================================

// Huruf tambahan untuk menu setting
const uint8_t LTR_t = SEG_D | SEG_E | SEG_F | SEG_G;                 // Huruf t kecil
const uint8_t LTR_A = SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G; // Huruf A
const uint8_t LTR_K = SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;         // Huruf K / H

void handleSettingMode() {
  display.setBrightness(4);
  static uint8_t menuState = 0;  // 0:Wait Release, 1:Standby "SEtt", 2:Select Menu, 3:Edit Value
  static int8_t currentMenu = 1; // 1-20: Parameter, 21: SAUE, 22: RSt
  static float editValue = 0;    

  // --- PEMBACAAN TOMBOL NON-BLOCKING & DEBOUNCE ---
  static uint8_t lastBtn = 0; 
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;
  
  uint8_t rawBtn = 0; 
  int adc = analogRead(PIN_BUTTONS);
  if (adc < 600) rawBtn = 1;                             // Tombol Plus
  else if (adc >= 800 && adc <= 1600) rawBtn = 2;        // Tombol Menu/OK
  else if (adc >= 1800 && adc <= 2800) rawBtn = 3;       // Tombol Minus

  uint8_t clickEvent = 0;     
  bool longPressEvent = false;

  if (rawBtn != lastBtn) {
    if (rawBtn != 0) {
      btnPressTime = millis();
      isLongPressHandled = false;
    } else if (!isLongPressHandled && (millis() - btnPressTime > 50)) {
      clickEvent = lastBtn; 
    }
    lastBtn = rawBtn;
  } else if (rawBtn == 2 && !isLongPressHandled && (millis() - btnPressTime > 1000)) {
    longPressEvent = true;
    isLongPressHandled = true; 
  }

  // --- DEKLARASI PESAN TEKS ---
  uint8_t msgSett[] = { LTR_S, LTR_E, LTR_t, LTR_t };
  uint8_t msgSave[] = { LTR_S, LTR_A, LTR_U, LTR_E };
  uint8_t msgOk[]   = { LTR_O, LTR_K, BLANK, BLANK };
  uint8_t msgRst[]  = { LTR_r, LTR_S, LTR_t, BLANK };

  // --- LOGIKA STATE MACHINE MENU ---
  switch (menuState) {
    case 0: // TUNGGU TOMBOL MENU DILEPAS SAAT BOOT
      display.setSegments(msgSett);
      if (rawBtn == 0) menuState = 1; 
      break;

    case 1: // STANDBY TAMPIL "SEtt"
      display.setSegments(msgSett);
      if (clickEvent == 2) menuState = 2; 
      break;

    case 2: // NAVIGASI MENU (S-1 sampai RSt)
      if (currentMenu <= 20) {
        // Tampilan S-xx (menggunakan SEG_G dari library bawaan untuk tanda minus)
        uint8_t dataS[] = { 
          LTR_S, 
          SEG_G, 
          (currentMenu >= 10) ? display.encodeDigit(currentMenu / 10) : BLANK, 
          display.encodeDigit(currentMenu % 10) 
        };
        display.setSegments(dataS);
      } 
      else if (currentMenu == 21) display.setSegments(msgSave);
      else if (currentMenu == 22) display.setSegments(msgRst);

      if (clickEvent == 1 && currentMenu < 22) currentMenu++; 
      if (clickEvent == 3 && currentMenu > 1) currentMenu--; 
      
      if (clickEvent == 2) {
        if (currentMenu == 21) {
          // PROSES SAVE
          display.setSegments(msgOk);
          saveParams();
          delay(1000);
          restartSystem();
        } 
        else if (currentMenu == 22) {
          // PROSES FACTORY RESET
          display.setSegments(msgOk);
          resetToDefaultParams(); // Reset data di EEPROM
          delay(1000);
          restartSystem();
        } 
        else {
          // PROSES LOAD KE VARIABEL SEMENTARA
          if (currentMenu == 1) editValue = params.timeDelay / 60.0;
          else if (currentMenu == 2) editValue = params.ovpLimit;
          else if (currentMenu == 3) editValue = params.uvpLimit;
          else if (currentMenu == 4) editValue = params.ovpUvpDelay;
          else if (currentMenu == 5) editValue = params.voltageHysteresis;
          else if (currentMenu == 6) editValue = params.lraLimit * 1000.0; // A ke mA
          else if (currentMenu == 7) editValue = params.lraTime;
          else if (currentMenu == 8) editValue = params.lraRetryLimit;
          else if (currentMenu == 9) editValue = params.ocpLimit * 1000.0; // A ke mA
          else if (currentMenu == 10) editValue = params.ocpDelay;
          else if (currentMenu == 11) editValue = params.ucpLimit * 1000.0; // mA
          else if (currentMenu == 12) editValue = params.minCurrentThreshold * 1000.0; // mA
          else if (currentMenu == 13) editValue = params.ntc1Enable;
          else if (currentMenu == 14) editValue = params.otpLimit;
          else if (currentMenu == 15) editValue = params.otpHysteresis;
          else if (currentMenu == 16) editValue = params.ntc2Enable;
          else if (currentMenu == 17) editValue = params.refFlowDelay / 60.0;
          else if (currentMenu == 18) editValue = params.flowFaultRetryLimit;
          else if (currentMenu == 19) editValue = params.orpTimeLimit / 60.0; // Jam
          else if (currentMenu == 20) editValue = params.orpRestDelay;
          
          menuState = 3; // Pindah layar Edit
        }
      }
      break;

    case 3: // EDIT VALUE
      // --- TAMPILAN NILAI ---
      display.showNumberDec((int)editValue, false, 4, 0);

      // --- PENGATURAN STEP/INCREMENT ---
      float stepValue = 1.0;
      if (currentMenu == 2 || currentMenu == 3) stepValue = 10; // S-6: LRA naik 100mA
      else if (currentMenu == 6) stepValue = 100.0; // S-6: LRA naik 100mA
      else if (currentMenu == 9) stepValue = 50.0; // S-9: OCP naik 50mA
      else if (currentMenu == 11 || currentMenu == 12) stepValue = 50.0; // Naik 50mA

      if (clickEvent == 1) editValue += stepValue;
      if (clickEvent == 3) editValue -= stepValue;

      // --- BATASAN LIMIT & CONSTRAINT ---
      if (currentMenu == 1) { if(editValue>10) editValue=10; if(editValue<2) editValue=2; }
      else if (currentMenu == 2) { if(editValue>260) editValue=260; if(editValue<230) editValue=230; }
      else if (currentMenu == 3) { if(editValue>200) editValue=200; if(editValue<150) editValue=150; }
      else if (currentMenu == 4) { if(editValue>10) editValue=10; if(editValue<3) editValue=3; }
      else if (currentMenu == 5) { if(editValue>40) editValue=40; if(editValue<10) editValue=10; }
      else if (currentMenu == 6) { if(editValue>5000) editValue=5000; if(editValue<800) editValue=800; }
      else if (currentMenu == 7) { if(editValue>5) editValue=5; if(editValue<1) editValue=1; }
      else if (currentMenu == 8) { if(editValue>5) editValue=5; if(editValue<2) editValue=2; }
      else if (currentMenu == 9) { if(editValue>4500) editValue=4500; if(editValue<300) editValue=300; }
      else if (currentMenu == 10) { if(editValue>10) editValue=10; if(editValue<2) editValue=2; }
      else if (currentMenu == 11) { if(editValue>1000) editValue=1000; if(editValue<200) editValue=0; }
      else if (currentMenu == 12) { if(editValue>300) editValue=400; if(editValue<150) editValue=0; }
      else if (currentMenu == 13) { if(editValue>1) editValue=1; if(editValue<0) editValue=0; }
      else if (currentMenu == 14) { if(editValue>90) editValue=90; if(editValue<70) editValue=70; }
      else if (currentMenu == 15) { if(editValue>20) editValue=20; if(editValue<5) editValue=5; }
      else if (currentMenu == 16) { if(editValue>1) editValue=1; if(editValue<0) editValue=0; }
      else if (currentMenu == 17) { if(editValue>10) editValue=10; if(editValue<2) editValue=2; }
      else if (currentMenu == 18) { if(editValue>5) editValue=5; if(editValue<2) editValue=2; }
      else if (currentMenu == 19) { if(editValue>12) editValue=12; if(editValue<2) editValue=2; }
      else if (currentMenu == 20) { if(editValue>60) editValue=60; if(editValue<20) editValue=20; }

      // --- SIMPAN SEMENTARA KE RAM JIKA DITEKAN TAHAN ---
      if (clickEvent == 2 || longPressEvent) {
        if (currentMenu == 1) params.timeDelay = (int)editValue * 60; 
        else if (currentMenu == 2) params.ovpLimit = (int)editValue;
        else if (currentMenu == 3) params.uvpLimit = (int)editValue;
        else if (currentMenu == 4) params.ovpUvpDelay = (int)editValue;
        else if (currentMenu == 5) params.voltageHysteresis = (int)editValue;
        else if (currentMenu == 6) params.lraLimit = editValue / 1000.0; // mA ke A
        else if (currentMenu == 7) params.lraTime = (int)editValue;
        else if (currentMenu == 8) params.lraRetryLimit = (int)editValue;
        else if (currentMenu == 9) params.ocpLimit = editValue / 1000.0; // mA ke A
        else if (currentMenu == 10) params.ocpDelay = (int)editValue;
        else if (currentMenu == 11) params.ucpLimit = editValue / 1000.0; // mA ke A
        else if (currentMenu == 12) params.minCurrentThreshold = editValue / 1000.0; // mA ke A
        else if (currentMenu == 13) params.ntc1Enable = (bool)editValue;
        else if (currentMenu == 14) params.otpLimit = (int)editValue;
        else if (currentMenu == 15) params.otpHysteresis = (int)editValue;
        else if (currentMenu == 16) params.ntc2Enable = (bool)editValue;
        else if (currentMenu == 17) params.refFlowDelay = (int)editValue * 60; // Menit ke Detik
        else if (currentMenu == 18) params.flowFaultRetryLimit = (int)editValue;
        else if (currentMenu == 19) params.orpTimeLimit = (int)editValue * 60; // Jam ke Menit
        else if (currentMenu == 20) params.orpRestDelay = (int)editValue;
        
        menuState = 2; // Kembali ke tampilan "S-xx"
      }
      break;
  }
}

void handleCalibrationMode() {
  display.setBrightness(4);
  static uint8_t menuState = 0;  
  static int8_t currentMenu = 1; // 1:U-rt, 2:A-rt, 3:t-rt, 4:C-1, 5:C-2, 6:C-3, 7:SAUE
  static float editValue = 0;    

  // --- PEMBACAAN TOMBOL ---
  static uint8_t lastBtn = 0; 
  static unsigned long btnPressTime = 0;
  static bool isLongPressHandled = false;
  
  uint8_t rawBtn = 0; 
  int adc = analogRead(PIN_BUTTONS);
  if (adc < 600) rawBtn = 1;                             // Tombol Plus
  else if (adc >= 800 && adc <= 1600) rawBtn = 2;        // Tombol Menu/OK
  else if (adc >= 1800 && adc <= 2800) rawBtn = 3;       // Tombol Minus

  uint8_t clickEvent = 0;     
  bool longPressEvent = false;

  if (rawBtn != lastBtn) {
    if (rawBtn != 0) {
      btnPressTime = millis();
      isLongPressHandled = false;
    } else if (!isLongPressHandled && (millis() - btnPressTime > 50)) {
      clickEvent = lastBtn; 
    }
    lastBtn = rawBtn;
  } else if (rawBtn == 2 && !isLongPressHandled && (millis() - btnPressTime > 1000)) {
    longPressEvent = true;
    isLongPressHandled = true; 
  }

  // --- DEKLARASI PESAN TEKS UI ---
  uint8_t msgCal[]  = { LTR_C, LTR_A, LTR_L, BLANK };
  uint8_t msgUrt[]  = { LTR_U, SEG_G, LTR_r, LTR_t }; // U-rt (Volt)
  uint8_t msgArt[]  = { LTR_A, SEG_G, LTR_r, LTR_t }; // A-rt (Arus mA)
  uint8_t msgTrt[]  = { LTR_t, SEG_G, LTR_r, LTR_t }; // t-rt (Suhu 1)
  uint8_t msgC1[]   = { LTR_C, SEG_G, BLANK, display.encodeDigit(1) }; // C- 1
  uint8_t msgC2[]   = { LTR_C, SEG_G, BLANK, display.encodeDigit(2) }; // C- 2
  uint8_t msgC3[]   = { LTR_C, SEG_G, BLANK, display.encodeDigit(3) }; // C- 3
  uint8_t msgSave[] = { LTR_S, LTR_A, LTR_U, LTR_E };
  uint8_t msgOk[]   = { LTR_O, LTR_K, BLANK, BLANK };

  // --- LOGIKA STATE MACHINE KALIBRASI ---
  switch (menuState) {
    case 0: // TUNGGU TOMBOL LEPAS SAAT BOOT
      display.setSegments(msgCal);
      if (rawBtn == 0) menuState = 1; 
      break;

    case 1: // STANDBY "CAL "
      display.setSegments(msgCal);
      if (clickEvent == 2) menuState = 2; // Tekan Menu -> Masuk List
      break;

    case 2: // NAVIGASI MENU KALIBRASI
      if (currentMenu == 1) display.setSegments(msgUrt);
      else if (currentMenu == 2) display.setSegments(msgArt);
      else if (currentMenu == 3) display.setSegments(msgTrt);
      else if (currentMenu == 4) display.setSegments(msgC1);
      else if (currentMenu == 5) display.setSegments(msgC2);
      else if (currentMenu == 6) display.setSegments(msgC3);
      else if (currentMenu == 7) display.setSegments(msgSave);

      if (clickEvent == 1 && currentMenu < 7) currentMenu++; 
      if (clickEvent == 3 && currentMenu > 1) currentMenu--; 
      
      if (clickEvent == 2) {
        if (currentMenu == 7) {
          // PROSES SAVE (User manual copot power untuk keluar)
          display.setSegments(msgOk);
          saveParams();
          delay(1000);
          menuState = 2; 
        } 
        else {
          // PROSES LOAD PARAMETER UNTUK DI EDIT
          if (currentMenu == 4) editValue = params.voltageMultiplier;
          else if (currentMenu == 5) editValue = params.currentMultiplier;
          else if (currentMenu == 6) editValue = params.tempOffset;
          menuState = 3; // Masuk Layar View / Edit
        }
      }
      break;

    case 3: // LAYAR LIVE VIEW / EDIT
      {
        static unsigned long lastLiveUpdate = 0;
        static int displayedLiveVal = 0;

        // Throttling: Sensor live view hanya dibaca tiap 250ms agar ADC tidak overload/cross-talk
        if (millis() - lastLiveUpdate >= 250) {
          lastLiveUpdate = millis();
          
          if (currentMenu == 1) {
            displayedLiveVal = (int)sysVolt;
          } 
          else if (currentMenu == 2) {
            displayedLiveVal = (int)(sysAmp * 1000.0); // Ubah ke mA
          } 
          else if (currentMenu == 3) {
            displayedLiveVal = (int)sysTemp1;
          } 
          else if (currentMenu == 4 || currentMenu == 5) {
            displayedLiveVal = (int)(editValue * 1000);
          }
          else if (currentMenu == 6) {
            displayedLiveVal = (int)editValue;
          }
        }

        // Tampilkan nilai hasil cache agar layar responsif dan ADC aman
        display.showNumberDec(displayedLiveVal, false, 4, 0);

        // --- PENGATURAN STEP/INCREMENT UNTUK EDIT MULTIPLIER/OFFSET ---
        if (currentMenu == 4 || currentMenu == 5) {
          if (clickEvent == 1) editValue += 0.001; 
          if (clickEvent == 3) editValue -= 0.001;
          
          if (editValue > 5.0) editValue = 5.0;
          if (editValue < 0.001) editValue = 0.001;
        }
        else if (currentMenu == 6) {
          if (clickEvent == 1) editValue += 1.0;
          if (clickEvent == 3) editValue -= 1.0;
          
          if (editValue > 20) editValue = 20;
          if (editValue < -20) editValue = -20;
        }

        // KELUAR DARI VIEW/EDIT (Tekan sebentar atau tahan)
        if (clickEvent == 2 || longPressEvent) {
          if (currentMenu == 4) params.voltageMultiplier = editValue;
          if (currentMenu == 5) params.currentMultiplier = editValue;
          if (currentMenu == 6) params.tempOffset = (int)editValue;
          menuState = 2; // Kembali ke menu utama kalibrasi
        }
      }
      break;
  }
}
