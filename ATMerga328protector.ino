/*
 * PROTEKSI KOMPRESOR KULKAS - ATmega328 (8 MHz Internal)
 * Feature: Dual-Current Protection (LRA & Running OCP), Auto-Zeroing, 
 *          EMA Filter, Watchdog 2s, EEPROM Error Log, Hysteresis,
 *          Differentiated Buzzer Notification Interval (15m Lockout / 30m Error)
 * Fix: Transisi STATE_ERROR langsung ke STATE_NORMAL_RUN (Fix Double-Delay 10m ke 5m)
 */

#include <avr/wdt.h> // Library Watchdog Timer
#include <EEPROM.h>  // Library EEPROM Internal

// --- KONFIGURASI PIN ---
#define PIN_VOLTAGE A5
#define PIN_CURRENT A4
#define PIN_RELAY   13
#define PIN_LED     10
#define PIN_BUZZER  9

// --- EEPROM ALAMAT ---
#define EEPROM_ADDR_LAST_ERR 0 

// =========================================================================
// --- AREA ANGKA KALIBRASI & NOISE FILTER ---
// =========================================================================
#define KALIBRASI_VOLT  1.635    // Disesuaikan dengan AVO 218V
#define KALIBRASI_ARUS  0.01969  // Disesuaikan dengan AVO 0.268A

#define NOISE_CUTOFF_V  4      
#define NOISE_CUTOFF_I  5      
// =========================================================================

// --- KONFIGURASI PARAMETER SISTEM ---
#define VOLTAGE_OVER      260.0
#define VOLTAGE_OVER_REC  250.0  // Histeresis OVP
#define VOLTAGE_UNDER     175.0
#define VOLTAGE_UNDER_REC 185.0  // Histeresis UVP

// --- PARAMETER PROTEKSI ARUS ---
#define CURRENT_RUN_MAX   1.2    // Batas Max Arus Running Normal (Kulkas 1/8 PK)
#define CURRENT_LRA_MAX   1.6    // Batas Max Arus LRA / Kompresor Macet (>1.2A)
#define LRA_TIME          2000   // Toleransi LRA / Start Macet (1 detik)
#define OCP_RUN_DELAY     4000   // Toleransi Overcurrent Running (3 detik)

#define VOLTAGE_SAG_CRITICAL 160.0 // Cutoff instan saat listrik kedip

#define STARTUP_DELAY     300000UL // 5 Menit (300.000 ms) delay kompresor
#define V_DROP_TOLERANCE  5000     // Toleransi drop tegangan sesaat (5 detik)
#define OCP_MAX_RETRY     3     

// --- PARAMETER INTERVAL NOTIFIKASI BUZZER ---
#define BUZZER_INT_ERROR   1800000UL // 30 Menit untuk Error Biasa (UVP/OVP/Retry OCP)
#define BUZZER_INT_LOCKOUT  900000UL // 15 Menit untuk Lockout OCP (Macet Permanen)

// --- STATE MACHINE ENUM ---
enum SystemState {
  STATE_INIT_DELAY, 
  STATE_NORMAL_RUN, 
  STATE_ERROR,      
  STATE_LOCKOUT     
};

enum ErrorType {
  ERR_NONE = 0,
  ERR_OVP  = 1, 
  ERR_UVP  = 2, 
  ERR_OCP  = 3  
};

// --- VARIABEL GLOBAL ---
SystemState currentState = STATE_INIT_DELAY;
ErrorType currentError = ERR_NONE;

unsigned long stateTimer = 0;
unsigned long voltageBadTimer = 0; 
unsigned long ocpRunTimer = 0;
unsigned long lraTimer = 0;
byte ocpRetryCount = 0;

// Variabel Hasil Smoothing (EMA Filtered)
float teganganRMS = 0.0; 
float arusRMS = 0.0;

int rawV_PeakToPeak = 0;
int rawI_PeakToPeak = 0;

unsigned long ledTimer = 0;
unsigned long buzzerTimer = 0;

// Forward Declaration
void masukError(ErrorType err, unsigned long currentMillis);
void prosesOcpRetry(unsigned long currentMillis);
void mulaiBuzzer(int jmlBeep, int durasi);
void bacaSensor(unsigned long currentMillis);
void logikaSistem(unsigned long currentMillis);
void engineLED(unsigned long currentMillis);
void engineBuzzer(unsigned long currentMillis);
void cetakDataSerial(unsigned long currentMillis);

void setup() {
  wdt_enable(WDTO_2S);
  
  Serial.begin(8947); // Baudrate untuk 8MHz Internal Clock
  
  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  
  stateTimer = millis();
  
  byte lastErr = EEPROM.read(EEPROM_ADDR_LAST_ERR);
  Serial.println(F("===================================="));
  Serial.println(F("Sistem Proteksi Mulai. Dual OCP Active."));
  Serial.print(F("[EEPROM BLACKBOX] Last Error Code: "));
  Serial.println(lastErr);
  Serial.println(F("===================================="));
}

void loop() {
  wdt_reset(); 
  
  unsigned long currentMillis = millis();
  
  bacaSensor(currentMillis);
  logikaSistem(currentMillis);
  engineLED(currentMillis);
  engineBuzzer(currentMillis);
  cetakDataSerial(currentMillis);
}

// ==========================================
// FUNGSI 1: NON-BLOCKING SENSOR SAMPLING & EMA FILTERING
// ==========================================
const unsigned long RMS_PERIOD = 20; 

void bacaSensor(unsigned long currentMillis) {
  static unsigned long rmsStartTime = 0;
  static long sumSquaredI = 0;
  static long sumI = 0;
  static unsigned int sampleCount = 0;
  static int maxV = 0, minV = 1023;

  int valV = analogRead(PIN_VOLTAGE);
  int valI = analogRead(PIN_CURRENT);

  if (valV > maxV) maxV = valV;
  if (valV < minV) minV = valV;

  sumI += valI;
  sumSquaredI += (long)valI * valI;
  sampleCount++;

  if (currentMillis - rmsStartTime >= RMS_PERIOD) {
    
    // 1. Tegangan Peak-to-Peak
    rawV_PeakToPeak = maxV - minV;
    int filteredRawV = (rawV_PeakToPeak <= NOISE_CUTOFF_V) ? 0 : rawV_PeakToPeak;
    float teganganInstan = (float)filteredRawV * KALIBRASI_VOLT;

    if (teganganRMS == 0.0) {
      teganganRMS = teganganInstan; 
    } else {
      teganganRMS = (teganganRMS * 0.92) + (teganganInstan * 0.08);
    }

    // 2. Current True RMS
    if (sampleCount > 0) {
      float meanI = (float)sumI / sampleCount;
      float varianceI = ((float)sumSquaredI / sampleCount) - (meanI * meanI);
      if (varianceI < 0) varianceI = 0;
      
      float rawI_RMS = sqrt(varianceI);

      if (rawI_RMS <= 1.2) { 
        arusRMS = 0.0;
        rawI_PeakToPeak = 0;
      } else {
        rawI_PeakToPeak = (int)rawI_RMS;
        float arusInstan = rawI_RMS * KALIBRASI_ARUS;

        if (arusRMS == 0.0) {
          arusRMS = arusInstan;
        } else {
          arusRMS = (arusRMS * 0.92) + (arusInstan * 0.08);
        }
      }
    }

    maxV = 0; minV = 1023;
    sumI = 0;
    sumSquaredI = 0;
    sampleCount = 0;
    rmsStartTime = currentMillis;
  }
}

// ==========================================
// FUNGSI 2: LOGIKA STATE MACHINE UTAMA
// ==========================================
void logikaSistem(unsigned long currentMillis) {
  switch(currentState) {
    
    case STATE_INIT_DELAY:
      if (currentMillis - stateTimer >= STARTUP_DELAY) {
        if (teganganRMS >= VOLTAGE_UNDER_REC && teganganRMS <= VOLTAGE_OVER_REC) {
          digitalWrite(PIN_RELAY, HIGH);
          currentState = STATE_NORMAL_RUN;
          stateTimer = currentMillis; 
          lraTimer = 0;        // Reset timer LRA
          ocpRunTimer = 0;     // Reset timer OCP
          mulaiBuzzer(1, 1000); 
          Serial.println(F(">>> KOMPRESOR ON! Startup Delay Selesai."));
        } 
        else if (teganganRMS < VOLTAGE_UNDER || teganganRMS > VOLTAGE_OVER) {
          masukError(teganganRMS < VOLTAGE_UNDER ? ERR_UVP : ERR_OVP, currentMillis);
        }
      }
      break;

    case STATE_NORMAL_RUN:
      digitalWrite(PIN_LED, HIGH);

      // --- A1. PROTEKSI BLACKOUT / KEDIP SESAAT (CRITICAL SAG) ---
      if (teganganRMS < VOLTAGE_SAG_CRITICAL) {
        Serial.println(F(">>> TRIP: Blackout/Kedip Sesaat Terdeteksi! Safe Shutdown."));
        masukError(ERR_UVP, currentMillis);
        break;
      }

      // --- A2. PROTEKSI TEGANGAN ---
      if (teganganRMS < VOLTAGE_UNDER || teganganRMS > VOLTAGE_OVER) {
        if (voltageBadTimer == 0) voltageBadTimer = currentMillis; 
        if (currentMillis - voltageBadTimer >= V_DROP_TOLERANCE) {
           masukError(teganganRMS < VOLTAGE_UNDER ? ERR_UVP : ERR_OVP, currentMillis);
        }
      } else {
        voltageBadTimer = 0; 
      }

      // --- B. PROTEKSI ARUS DUA LAPIS (LRA & RUNNING OCP) ---
      
      // LAPIS 1: ARUS LRA / MACET PARAH (> 1.2A) -> Response Cepat 1 Detik
      if (arusRMS > CURRENT_LRA_MAX) {
        ocpRunTimer = 0; // Reset timer OCP running
        if (lraTimer == 0) lraTimer = currentMillis;
        
        if (currentMillis - lraTimer >= LRA_TIME) { 
          Serial.println(F(">>> TRIP: LRA/Macet Parah (>1.2A)! Cutoff Cepat 1s."));
          prosesOcpRetry(currentMillis);
        }
      } 
      // LAPIS 2: OVERCURRENT RUNNING (0.9A - 1.2A) -> Response Normal 3 Detik
      else if (arusRMS > CURRENT_RUN_MAX) {
        lraTimer = 0; // Reset timer LRA
        if (ocpRunTimer == 0) ocpRunTimer = currentMillis;
        
        if (currentMillis - ocpRunTimer >= OCP_RUN_DELAY) {
          Serial.println(F(">>> TRIP: Overcurrent Running (>0.9A)! Cutoff 3s."));
          prosesOcpRetry(currentMillis);
        }
      } 
      // KONDISI ARUS NORMAL (< 0.9A)
      else {
        lraTimer = 0;
        ocpRunTimer = 0;
      }
      break;

    case STATE_ERROR:
      // Auto Recovery jika penundaan 5 menit selesai DAN tegangan listrik aman
      if (currentMillis - stateTimer >= STARTUP_DELAY) {
        if (teganganRMS >= VOLTAGE_UNDER_REC && teganganRMS <= VOLTAGE_OVER_REC) {
           digitalWrite(PIN_RELAY, HIGH);       // Nyalakan relay langsung
           currentState = STATE_NORMAL_RUN;     // Langsung berpindah ke Normal Run
           currentError = ERR_NONE;
           stateTimer = currentMillis;
           lraTimer = 0;
           ocpRunTimer = 0;
           mulaiBuzzer(1, 1000);                // Bip penanda kompresor kembali ON
           Serial.println(F(">>> RECOVERY: Delay 5m Selesai. KOMPRESOR ON!"));
        }
      }
      
      // Pengulangan Buzzer 30 Menit Sekali saat Error biasa
      if (currentMillis - buzzerTimer >= BUZZER_INT_ERROR) {
        mulaiBuzzer(3, 100); 
        buzzerTimer = currentMillis;
      }
      break;

    case STATE_LOCKOUT:
      // Pengulangan Buzzer 15 Menit Sekali saat Lockout OCP (Perhatian Khusus)
      if (currentMillis - buzzerTimer >= BUZZER_INT_LOCKOUT) {
        mulaiBuzzer(3, 100); 
        buzzerTimer = currentMillis;
      }
      break;
  }
}

// Fungsi Bantu Handler Retry OCP
void prosesOcpRetry(unsigned long currentMillis) {
  ocpRetryCount++;
  ocpRunTimer = 0;
  lraTimer = 0;
  
  if (ocpRetryCount >= OCP_MAX_RETRY) {
    digitalWrite(PIN_RELAY, LOW);
    currentState = STATE_LOCKOUT;
    currentError = ERR_OCP;
    buzzerTimer = currentMillis;
    EEPROM.update(EEPROM_ADDR_LAST_ERR, ERR_OCP); 
    mulaiBuzzer(3, 100); 
    Serial.println(F(">>> LOCKOUT: OCP 3x! Butuh Reset Manual!"));
  } else {
    masukError(ERR_OCP, currentMillis);
  }
}

void masukError(ErrorType err, unsigned long currentMillis) {
  digitalWrite(PIN_RELAY, LOW); 
  currentState = STATE_ERROR;
  currentError = err;
  stateTimer = currentMillis;
  buzzerTimer = currentMillis; 
  voltageBadTimer = 0;
  ocpRunTimer = 0;
  lraTimer = 0;
  
  EEPROM.update(EEPROM_ADDR_LAST_ERR, err); 
  
  mulaiBuzzer(3, 100); 
  Serial.print(F(">>> SYSTEM CUT-OFF! Error Code: "));
  Serial.println(err);
}

// ==========================================
// FUNGSI 3: CETAK DATA KE SERIAL MONITOR
// ==========================================
void cetakDataSerial(unsigned long currentMillis) {
  static unsigned long lastSerialPrint = 0;
  
  if (currentMillis - lastSerialPrint >= 500) {
    lastSerialPrint = currentMillis;
    
    if (currentState != STATE_LOCKOUT) {
      Serial.print(F("[State: ")); 
      Serial.print(currentState);
      Serial.print(F("] | Raw V_PP: ")); 
      Serial.print(rawV_PeakToPeak);
      Serial.print(F(" => Volt: "));
      Serial.print(teganganRMS, 1);
      Serial.print(F(" V  ||  Raw I_PP: "));
      Serial.print(rawI_PeakToPeak);
      Serial.print(F(" => Arus: "));
      Serial.print(arusRMS, 3);
      Serial.println(F(" A"));
    }
  }
}

// ==========================================
// FUNGSI 4: ENGINE ANIMASI LED & BUZZER
// ==========================================
void engineLED(unsigned long currentMillis) {
  if (currentState == STATE_NORMAL_RUN) return; 
  
  if (currentError == ERR_NONE) {
    if (currentMillis - ledTimer >= 1000) {
      ledTimer = currentMillis;
      digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }
  } 
  else {
    static byte blinkCounter = 0;
    static bool isOn = false;
    static unsigned long errorLedTimer = 0;
    
    int targetBlinks = currentError; 
    
    if (blinkCounter < targetBlinks * 2) {
      if (currentMillis - errorLedTimer >= 300) { 
        errorLedTimer = currentMillis;
        isOn = !isOn;
        digitalWrite(PIN_LED, isOn);
        blinkCounter++;
      }
    } else {
      digitalWrite(PIN_LED, LOW);
      if (currentMillis - errorLedTimer >= 2000) { 
        errorLedTimer = currentMillis;
        blinkCounter = 0; 
      }
    }
  }
}

int buzzerTarget = 0;
int buzzerDurasi = 0;
byte currentBeeps = 0;
bool buzzerIsOn = false;

void mulaiBuzzer(int jmlBeep, int durasi) {
  buzzerTarget = jmlBeep * 2;
  buzzerDurasi = durasi;
  currentBeeps = 0;
  buzzerIsOn = false;
}

void engineBuzzer(unsigned long currentMillis) {
  static unsigned long lastBuzzerToggle = 0;
  
  if (currentBeeps < buzzerTarget) {
    if (currentMillis - lastBuzzerToggle >= buzzerDurasi) {
      lastBuzzerToggle = currentMillis;
      buzzerIsOn = !buzzerIsOn;
      digitalWrite(PIN_BUZZER, buzzerIsOn);
      currentBeeps++;
    }
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }
}
