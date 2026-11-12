#include <avr/wdt.h>
#include "Pins.h"
#include "Params.h"
#include "Sensors.h"  // Buka comment ini nanti kalau Sensors.h sudah dibuat
#include "UI.h"       // Buka comment ini nanti kalau UI.h sudah dibuat

// ==========================================
// VARIABEL GLOBAL SISTEM
// ==========================================
OperatingMode currentMode = MODE_NORMAL_PROTECTION;
SystemState currentState = STATE_STARTUP_DELAY;
FaultType currentFault = FAULT_NONE;

float sysVolt = 0.0;
float sysAmp = 0.0;
float sysTemp1 = 0.0;
float sysTemp2 = 0.0;
unsigned long stateTimer = 0;
unsigned long runningTimer = 0;
unsigned long orpTimer = 0;
unsigned long faultConditionTimer = 0;
unsigned long serialTelemetryTimer = 0;

uint8_t lraRetryCount = 0;
uint8_t flowRetryCount = 0;

float initialTempNTC2 = 0.0;
bool isFlowChecked = false;

// Fungsi deklarasi ulang agar bisa digunakan sebelum diimplementasi (karena ada pemanggilan restartSystem)
void restartSystem();

// ==========================================
// SETUP (HANYA DIEKSEKUSI 1 KALI SAAT BOOTING)
// ==========================================
void setup() {
  Serial.begin(9600);
  //Inisialisasi Sensor
  initSensors();
  
  // 1. Inisialisasi Output digital
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  
  digitalWrite(PIN_RELAY, LOW);     // Relay dipastikan OFF awal boot
  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // 2. Aktifkan Internal Pull-Up pada Pin Kalibrasi (PD7 / Pin 7)
  pinMode(PIN_CALIBRATION, INPUT_PULLUP);
  delay(50); // Jeda stabilisasi pembacaan pin hardware
  
  initUI();
  // 3. Muat parameter dari EEPROM (ditangani oleh Params.cpp)
  loadParams();
  Serial.println(F("[SYSTEM] Parameter Loaded from EEPROM"));

  // 4. LOGIKA CEK POWER-ON (CEK MODE AKSI UTAMA)
  if (digitalRead(PIN_CALIBRATION) == LOW) {
    // Jalur Kalibrasi di-short ke GND saat boot -> Masuk MODE KALIBRASI
    currentMode = MODE_CALIBRATION;
    digitalWrite(PIN_RELAY, HIGH); // Relay ON paksakan agar arus/tegangan bisa dikalibrasi live!
    Serial.println(F(">>> MODE: KALIBRASI AKTIF (RELAY ON) <<<"));
    Serial.println(F(">>> MATIKAN POWER & COPOT JUMPER UNTUK KELUAR <<<"));
  } 
  else {
    // Cek apakah tombol MENU/OK ditahan saat booting
    int buttonAdc = analogRead(PIN_BUTTONS);
    
    // Nilai ADC Tombol Menu (Resistor 4.3k divider) ~ Range ADC 250 - 400 (pada 10-bit)
    if (buttonAdc >= 800 && buttonAdc <= 1600) {
      currentMode = MODE_SETTING;
      digitalWrite(PIN_RELAY, LOW);
      Serial.println(F(">>> MODE: SETTING PARAMETER AKTIF <<<"));
      Serial.println(F(">>> TAHAN TOMBOL MENU UNTUK SAVE & RESTART <<<"));
    } 
    else {
      // Jika tidak ada trigger, masuk Sistem Proteksi Normal
      currentMode = MODE_NORMAL_PROTECTION;
      digitalWrite(PIN_RELAY, LOW);
      stateTimer = millis(); // Mulai timer delay dari awal
      Serial.println(F(">>> MODE: PROTEKSI SISTEM UTAMA AKTIF <<<"));
    }
  }
  
  wdt_enable(WDTO_2S);
}

// ==========================================
// LOOP (PROGRAM UTAMA BERJALAN CONTINUOUS)
// ==========================================
void loop() {
  wdt_reset();
  // A. BACA SENSOR (HANYA DIBACA 1X DI SINI UNTUK SELURUH SISTEM!)
  sysVolt = readVoltage();
  sysAmp = readCurrent();
  sysTemp1 = readTemp1();
  sysTemp2 = readTemp2();

  // B. KIRIM TELEMETRI SERIAL TIAP 1 DETIK
  if (millis() - serialTelemetryTimer >= 1000) {
    serialTelemetryTimer = millis();
    // Ubah pemanggilan variabelnya
    sendSerialTelemetry(sysVolt, sysAmp, sysTemp1, sysTemp2);
  }

  // C. EKSEKUSI SESUAI OPERATING MODE
  if (currentMode == MODE_CALIBRATION) {
    handleCalibrationMode();
    return;
  } 
  else if (currentMode == MODE_SETTING) {
    handleSettingMode();
    return;
  }

  // =========================================================
  // D. LOGIKA SISTEM PROTEKSI UTAMA (MODE_NORMAL_PROTECTION)
  // =========================================================
  
  // 1. Proteksi Tegangan Ekstrem Instan (Surge & Blackout)
  if (sysVolt >= params.powerSurgeLimit) triggerFault(FAULT_SURGE);
  if (sysVolt <= params.blackoutLimit) triggerFault(FAULT_BLACKOUT);

  // 2. State Machine System Execution
  switch (currentState) {
    
    case STATE_STARTUP_DELAY:
      digitalWrite(PIN_RELAY, LOW);
      
      // Tahan timer jika tegangan belum aman / NTC1 masih overheat
      if (!isVoltageSafe(sysVolt) || (params.ntc1Enable && sysTemp1 > (params.otpLimit - params.otpHysteresis))) {
        stateTimer = millis(); 
      } 
      // Jika waktu delay tuntas -> Start Kompresor!
      else if (millis() - stateTimer >= (params.timeDelay * 1000UL)) {
        currentFault = FAULT_NONE;
        initialTempNTC2 = sysTemp2; // Ambil baseline suhu NTC2
        isFlowChecked = false;
        runningTimer = millis();
        orpTimer = millis();
        
        currentState = STATE_RUNNING;
        digitalWrite(PIN_RELAY, HIGH); // NYALAKAN RELAY KOMPRESOR
        Serial.println(F("[SYSTEM] STARTING COMPRESSOR -> RELAY ON"));
      }
      break;

    case STATE_RUNNING:
      // --- A. Monitoring Tegangan (OVP & UVP Delay) ---
      if (sysVolt > params.ovpLimit) checkFaultDelay(FAULT_OVP, params.ovpUvpDelay);
      else if (sysVolt < params.uvpLimit) checkFaultDelay(FAULT_UVP, params.ovpUvpDelay);
      else faultConditionTimer = millis(); 

      // --- B. Monitoring Suhu Kompresor Instan (NTC1) ---
      if (params.ntc1Enable && sysTemp1 >= params.otpLimit) {
        triggerFault(FAULT_OTP);
      }

      // --- C. Monitoring Arus (LRA vs OCP & UCP) ---
      unsigned long runDuration = millis() - runningTimer;
      
      if (runDuration <= (params.lraTime * 1000UL)) {
        // Fase Inrush (LRA Time Default: 2 Detik)
        if (sysAmp >= params.lraLimit) {
          lraRetryCount++;
          Serial.print(F("[WARNING] LRA Trip Detected! Retry: "));
          Serial.println(lraRetryCount);
          
          if (lraRetryCount >= params.lraRetryLimit) {
            currentState = STATE_LOCKOUT;
            digitalWrite(PIN_RELAY, LOW);
            Serial.println(F("[FATAL] LRA Retry Exceeded -> SYSTEM LOCKEDOUT!"));
          } else {
            triggerFault(FAULT_LRA);
          }
        }
      } 
      else {
        // Fase Running Normal (LRA Pass)
        lraRetryCount = 0; // Reset LRA Counter
        
        if (sysAmp >= params.ocpLimit) checkFaultDelay(FAULT_OCP, params.ocpDelay);
        if (params.ucpLimit > 0 && sysAmp <= params.ucpLimit) checkFaultDelay(FAULT_UCP, 60);
      }

      // --- D. Monitoring Refrigerant Flow (NTC2) ---
      if (params.ntc2Enable && !isFlowChecked && runDuration >= (params.refFlowDelay * 1000UL)) {
        isFlowChecked = true;
        if (sysTemp2 < (initialTempNTC2 + params.refDeltaTemp)) {
          flowRetryCount++;
          Serial.print(F("[WARNING] Ref Flow Error! Retry: "));
          Serial.println(flowRetryCount);
          
          if (flowRetryCount >= params.flowFaultRetryLimit) {
            currentState = STATE_LOCKOUT;
            digitalWrite(PIN_RELAY, LOW);
            Serial.println(F("[FATAL] Ref Flow Retry Exceeded -> SYSTEM LOCKEDOUT!"));
          } else {
            triggerFault(FAULT_REF_FLOW);
          }
        } else {
          flowRetryCount = 0; 
        }
      }

      // --- E. Monitoring ORP (Over Running Protection) ---
      if (sysAmp < params.minCurrentThreshold) {
        // Arus di bawah batas (misal 0.25A) -> Termostat kulkas OFF
        orpTimer = millis(); 
      } 
      else if (millis() - orpTimer >= (params.orpTimeLimit * 60000UL)) {
        // Kompresor berjalan melampaui batas tanpa henti
        currentState = STATE_ORP_REST;
        stateTimer = millis(); 
        digitalWrite(PIN_RELAY, LOW);
        Serial.println(F("[SYSTEM] ORP Triggered -> Entering Rest State"));
      }
      break;

    case STATE_FAULT:
      digitalWrite(PIN_RELAY, LOW);
      
      // Cek Kondisi Recovery (Hysteresis)
      bool readyToRecover = false;
      if (currentFault == FAULT_OVP && sysVolt <= (params.ovpLimit - params.voltageHysteresis)) readyToRecover = true;
      else if (currentFault == FAULT_UVP && sysVolt >= (params.uvpLimit + params.voltageHysteresis)) readyToRecover = true;
      else if (currentFault == FAULT_OTP && sysTemp1 <= (params.otpLimit - params.otpHysteresis)) readyToRecover = true;
      else if (currentFault != FAULT_OVP && currentFault != FAULT_UVP && currentFault != FAULT_OTP) readyToRecover = true;

      if (readyToRecover) {
        Serial.println(F("[SYSTEM] Fault Recovered -> Restarting Startup Delay"));
        //currentFault = FAULT_NONE;
        currentState = STATE_STARTUP_DELAY;
        stateTimer = millis(); 
      }
      break;

    case STATE_ORP_REST:
      digitalWrite(PIN_RELAY, LOW);
      // Istirahat tuntas ATAU Suhu Kompresor (NTC1) sudah turun
      bool tempCool = (!params.ntc1Enable || sysTemp1 <= 60.0);
      
      if ((millis() - stateTimer >= (params.orpRestDelay * 60000UL)) && tempCool) {
        Serial.println(F("[SYSTEM] ORP Rest Completed -> Restarting Startup Delay"));
        currentState = STATE_STARTUP_DELAY;
        stateTimer = millis();
      }
      break;

    case STATE_LOCKOUT:
      digitalWrite(PIN_RELAY, LOW);
      // Terkunci permanen (Menunggu reset dari UI atau Cabut Listrik)
      break;
  }
  // =========================================================
  // E. UPDATE TAMPILAN & INDIKATOR (HANYA DI MODE NORMAL)
  // =========================================================
  // UPDATE DISPLAY 7 SEGMENT
  updateDisplay(sysVolt, sysAmp);
  handleIndicators();
}

// ==========================================
// FUNGSI HELPER & MONITORING
// ==========================================
void triggerFault(FaultType fault) {
  // CEGAH SPAM: Jika sistem sudah dalam state FAULT dan jenis error-nya sama, abaikan!
  if (currentState == STATE_FAULT && currentFault == fault) {
    return; 
  }

  currentFault = fault;
  currentState = STATE_FAULT;
  digitalWrite(PIN_RELAY, LOW);
  faultConditionTimer = millis();
  
  Serial.print(F("\n[TRIP FAULT] Code: "));
  Serial.println(fault);
}

void checkFaultDelay(FaultType fault, uint8_t delaySeconds) {
  if (millis() - faultConditionTimer >= (delaySeconds * 1000UL)) {
    triggerFault(fault);
  }
}

bool isVoltageSafe(float v) {
  return (v > params.uvpLimit && v < params.ovpLimit);
}

void sendSerialTelemetry(float v, float a, float t1, float t2) {
  Serial.print(F("MODE:")); Serial.print(currentMode);
  Serial.print(F(" | ST:")); Serial.print(currentState);
  Serial.print(F(" | FLT:")); Serial.print(currentFault);
  Serial.print(F(" | V:")); Serial.print(v, 1);
  Serial.print(F(" | A:")); Serial.print(a, 2);
  Serial.print(F(" | T1:")); Serial.print(t1, 1);
  Serial.print(F(" | T2:")); Serial.println(t2, 1);
}

// Fungsi Software Reset (Berlaku untuk AVR / LGT8F328P)
void(* resetFunc) (void) = 0; 
void restartSystem() {
  Serial.println(F("[SYSTEM] Rebooting..."));
  delay(100);
  resetFunc(); // Lompat ke alamat 0 (Software Reset)
}

void handleIndicators() {
  static unsigned long lastLedTimer = 0;
  static uint8_t blinkCount = 0;
  static bool isBlinking = false;
  static bool ledState = false;
  
  static unsigned long lastBuzzerInterval = millis() - 1800000UL; // Dibuat minus agar lgsg bunyi saat error pertama
  static unsigned long lastBuzzerToggle = 0;
  static uint8_t beepCount = 0;
  static bool isBeeping = false;
  static bool buzzerState = false;

  unsigned long currentMillis = millis();

  // ==========================================
  // 1. LOGIKA LED STATUS
  // ==========================================
  if (currentState == STATE_RUNNING) {
    digitalWrite(PIN_LED_STATUS, HIGH); // Running = ON Terus
  } 
  else if (currentState == STATE_STARTUP_DELAY) {
    // Delay = Kedip lambat (500ms)
    if (currentMillis - lastLedTimer >= 500) {
      lastLedTimer = currentMillis;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState);
    }
  } 
  else if (currentState == STATE_FAULT || currentState == STATE_LOCKOUT || currentState == STATE_ORP_REST) {
    // Error = Kedip sesuai kode Error (misal OVP=3x), jeda 1.5 detik
    uint8_t maxBlinks = (currentState == STATE_ORP_REST) ? 10 : currentFault;
    if (maxBlinks == 0) maxBlinks = 1;

    if (!isBlinking) {
      if (currentMillis - lastLedTimer >= 1500) { 
        isBlinking = true;
        blinkCount = 0;
        lastLedTimer = currentMillis;
      }
      digitalWrite(PIN_LED_STATUS, LOW);
    } else {
      if (currentMillis - lastLedTimer >= (ledState ? 200 : 500)) { // 500ms ON, 200ms OFF
        lastLedTimer = currentMillis;
        ledState = !ledState;
        digitalWrite(PIN_LED_STATUS, ledState);
        
        if (!ledState) {
          blinkCount++;
          if (blinkCount >= maxBlinks) isBlinking = false; // Siklus kedip selesai
        }
      }
    }
  }

  // ==========================================
  // 2. LOGIKA BUZZER (ACTIVE BUZZER)
  // ==========================================
  if (currentState == STATE_FAULT || currentState == STATE_LOCKOUT) {
    // Interval: Lockout 15 Menit (900000 ms), Fault biasa 30 Menit (1800000 ms)
    unsigned long interval = (currentState == STATE_LOCKOUT) ? 900000UL : 1800000UL;
    
    // Cek apakah sudah waktunya bunyi (termasuk bunyi spontan pertama kali error)
    if (!isBeeping && (currentMillis - lastBuzzerInterval >= interval)) {
      isBeeping = true;
      beepCount = 0;
      lastBuzzerToggle = currentMillis;
      buzzerState = true;
      digitalWrite(PIN_BUZZER, HIGH);
    }

    // Eksekusi Beep 5x (300ms ON, 300ms OFF)
    if (isBeeping) {
      if (currentMillis - lastBuzzerToggle >= 300) { 
        lastBuzzerToggle = currentMillis;
        buzzerState = !buzzerState;
        digitalWrite(PIN_BUZZER, buzzerState);

        if (!buzzerState) {
          beepCount++;
          if (beepCount >= 5) { // Selesai 5 beep
            isBeeping = false;
            lastBuzzerInterval = currentMillis; // Reset timer 15/30 menit
          }
        }
      }
    }
  } else {
    // Jika tidak error, matikan buzzer dan paksa timer buzzer kedaluwarsa 
    // agar besok kalau ada error langsung bunyi seketika
    digitalWrite(PIN_BUZZER, LOW);
    isBeeping = false;
    lastBuzzerInterval = currentMillis - 1800000UL; 
  }
}
