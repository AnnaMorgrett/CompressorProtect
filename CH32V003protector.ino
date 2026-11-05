/*
 * ============================================================================
 * PROTEKSI KOMPRESOR KULKAS - CH32V003 (48 MHz HSI Internal)
 * Port dari ATmega328P ke CH32V003 (TSSOP-20 / CH32V003F4P6)
 *
 * Features: Dual-Current Protection (LRA & Running OCP), Auto-Zeroing,
 *           EMA Filter, IWDG Watchdog ~2s, Option-Byte EEPROM Emulation,
 *           Hysteresis, Differentiated Buzzer (15m Lockout / 30m Error),
 *           Integer-math sqrt (no FPU needed), Hardware UART via Serial
 *
 * Target Board : WCH:ch32v:CH32V00x_EVT  (Tools > Board > CH32V00x_EVT)
 * Chip Select  : CH32V003F4              (Tools > Chip Select > CH32V003F4)
 * Clock        : 48MHz Internal (HSI)   (Tools > Clock > 48MHz Internal)
 * Upload       : minichlink (ch32fun)
 *
 * EEPROM LIBRARY (place in sketch folder):
 *   EEPROM.h / EEPROM.cpp by Maxint R&D for CH32V003
 *   Uses Option Bytes area (26 bytes total, survives code reflash!).
 *   API: EEPROM.begin(), EEPROM.read(idx), EEPROM.write(idx, val), EEPROM.commit()
 *   NOTE: .update() does NOT exist in this library. Use .write() + .commit() instead.
 *
 * PORT NOTES (ATmega328P -> CH32V003):
 *   WATCHDOG : avr/wdt.h -> IWDG HAL (ch32v00x_iwdg.h, included via ch32v00x_conf.h)
 *              LSI ~128kHz, /64 prescaler -> 2000 Hz. Reload 3999 -> 2.000s timeout.
 *   EEPROM   : EEPROM.update() (AVR) -> EEPROM.write() + EEPROM.commit() (Maxint lib).
 *   SERIAL   : 8947 baud (ATmega 8MHz trick) -> 115200 baud (48MHz HSI, 0.01% error).
 *   ADC      : pinMode(pin, INPUT_ANALOG) required before analogRead() on CH32V003.
 *   MATH     : sqrt() -> isqrt32() (pure integer, saves ~2KB of soft-float libgcc).
 *
 * SCHEMATIC PIN MAPPING (CH32Version.png):
 *   PIN_VOLTAGE = PA2  (AIN0) - VAC sensor (voltage divider + clamp)
 *   PIN_CURRENT = PA1  (AIN1) - CT sensor (burden resistor + clamp)
 *   PIN_RELAY   = PD6         - Relay driver (via S8050 transistor)
 *   PIN_LED     = PD4         - Status LED (via resistor)
 *   PIN_BUZZER  = PC1         - Buzzer driver (via S8050 transistor)
 *   UART TX     = PD5         - Hardware USART1 TX (auto-configured by Serial)
 * ============================================================================
 */

#include <EEPROM.h>   // Maxint R&D library for CH32V003 (Option Bytes emulation)
                      // Files EEPROM.h + EEPROM.cpp must be in this sketch folder.

// --- PIN MAPPING ---
#define PIN_VOLTAGE  PA2
#define PIN_CURRENT  PA1
#define PIN_RELAY    PD6
#define PIN_LED      PD4
#define PIN_BUZZER   PC1

// --- EEPROM ADDRESS ---
#define EEPROM_ADDR_LAST_ERR  0

// ============================================================================
// EEPROM HELPER: write + commit in one call (replaces AVR EEPROM.update)
//   - Reads current value first; skips write if unchanged (wear protection).
//   - commit() physically programs the Option Bytes flash.
// ============================================================================
static void eeprom_save(int addr, uint8_t val) {
  if (EEPROM.read(addr) != val) {
    EEPROM.write(addr, val);
    EEPROM.commit();
  }
}

// ============================================================================
// IWDG WATCHDOG HELPERS (ch32v00x_iwdg.h — always included via ch32v00x_conf.h)
//   LSI ~128kHz, Prescaler /64 -> 2000 Hz tick rate.
//   Reload = 3999 -> timeout = (3999+1)/2000 = 2.000 s.
// ============================================================================
static inline void wdt_enable_ch32() {
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_64);
  IWDG_SetReload(3999);
  IWDG_ReloadCounter();
  IWDG_Enable();
}

static inline void wdt_reset_ch32() {
  IWDG_ReloadCounter();
}

// ============================================================================
// CALIBRATION & NOISE FILTER
//   Both MCUs: 10-bit ADC (0-1023).
//   CH32V003 Vref = 3.3V (AMS1117-3.3 on schematic) vs ATmega 5V.
//   Re-calibrate against a multimeter after first build.
// ============================================================================
#define KALIBRASI_VOLT  1.635f
#define KALIBRASI_ARUS  0.01969f
#define NOISE_CUTOFF_V  4
#define NOISE_CUTOFF_I  5

// --- SYSTEM PROTECTION PARAMETERS ---
#define VOLTAGE_OVER         260.0f
#define VOLTAGE_OVER_REC     250.0f
#define VOLTAGE_UNDER        175.0f
#define VOLTAGE_UNDER_REC    185.0f
#define CURRENT_RUN_MAX      1.2f
#define CURRENT_LRA_MAX      1.6f
#define LRA_TIME             2000UL
#define OCP_RUN_DELAY        4000UL
#define VOLTAGE_SAG_CRITICAL 160.0f
#define STARTUP_DELAY        300000UL
#define V_DROP_TOLERANCE     5000UL
#define OCP_MAX_RETRY        3
#define BUZZER_INT_ERROR     1800000UL
#define BUZZER_INT_LOCKOUT    900000UL

// --- STATE MACHINE ENUMS ---
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

// --- GLOBAL VARIABLES ---
SystemState currentState = STATE_INIT_DELAY;
ErrorType   currentError = ERR_NONE;

unsigned long stateTimer       = 0;
unsigned long voltageBadTimer  = 0;
unsigned long ocpRunTimer      = 0;
unsigned long lraTimer         = 0;
byte          ocpRetryCount    = 0;

float teganganRMS = 0.0f;
float arusRMS     = 0.0f;

int rawV_PeakToPeak = 0;
int rawI_PeakToPeak = 0;

unsigned long ledTimer    = 0;
unsigned long buzzerTimer = 0;

int  buzzerTarget = 0;
int  buzzerDurasi = 0;
byte currentBeeps = 0;
bool buzzerIsOn   = false;

// ============================================================================
// INTEGER SQUARE ROOT — avoids ~2KB soft-float libgcc sqrt on RV32EC (no FPU)
//   isqrt32(n) = floor(sqrt(n)) for 32-bit unsigned input.
// ============================================================================
static uint32_t isqrt32(uint32_t n) {
  if (n == 0) return 0;
  uint32_t op  = n;
  uint32_t res = 0;
  uint32_t one = 1UL << 30;
  while (one > op) one >>= 2;
  while (one != 0) {
    if (op >= res + one) {
      op  -= res + one;
      res  = (res >> 1) + one;
    } else {
      res >>= 1;
    }
    one >>= 2;
  }
  return res;
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void masukError(ErrorType err, unsigned long currentMillis);
void prosesOcpRetry(unsigned long currentMillis);
void mulaiBuzzer(int jmlBeep, int durasi);
void bacaSensor(unsigned long currentMillis);
void logikaSistem(unsigned long currentMillis);
void engineLED(unsigned long currentMillis);
void engineBuzzer(unsigned long currentMillis);
void cetakDataSerial(unsigned long currentMillis);

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  // 1. IWDG watchdog ~2s
  wdt_enable_ch32();

  // 2. ADC pins — INPUT_ANALOG required on CH32V003 before analogRead()
  pinMode(PIN_VOLTAGE, INPUT_ANALOG);
  pinMode(PIN_CURRENT, INPUT_ANALOG);

  // 3. Set outputs LOW before direction (prevents glitches)
  digitalWrite(PIN_RELAY,  LOW);
  digitalWrite(PIN_LED,    LOW);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_RELAY,  OUTPUT);
  pinMode(PIN_LED,    OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // 4. Hardware UART — 115200 baud, USART1 TX = PD5
  Serial.begin(115200);

  // 5. Init EEPROM (Maxint lib loads Option Bytes into RAM buffer)
  EEPROM.begin();

  // 6. Record startup time
  stateTimer = millis();

  // 7. Report last saved error
  byte lastErr = EEPROM.read(EEPROM_ADDR_LAST_ERR);
  Serial.println(F("===================================="));
  Serial.println(F("Sistem Proteksi Mulai. CH32V003 Port."));
  Serial.println(F("Dual OCP Active. IWDG ~2s Enabled."));
  Serial.print(F("[OB BLACKBOX] Last Error Code: "));
  Serial.println(lastErr);
  Serial.println(F("===================================="));
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  wdt_reset_ch32();

  unsigned long currentMillis = millis();

  bacaSensor(currentMillis);
  logikaSistem(currentMillis);
  engineLED(currentMillis);
  engineBuzzer(currentMillis);
  cetakDataSerial(currentMillis);
}

// ============================================================================
// FUNCTION 1: NON-BLOCKING SENSOR SAMPLING & EMA FILTERING
//
// Current RMS math uses integer isqrt32() on scaled variance (×1024)
// so rawI_RMS_scaled = raw_ADC_RMS × 32 (no soft-float sqrt needed).
// Noise threshold: original ≤1.2 ADC units → ≤1.2×32 = 38 (scaled).
// Final: arusInstan = (rawI_RMS_scaled × KALIBRASI_ARUS) / 32.0f
// Only 1 float multiply per 20ms window.
// ============================================================================
const unsigned long RMS_PERIOD = 20;

void bacaSensor(unsigned long currentMillis) {
  static unsigned long rmsStartTime = 0;
  static long     sumSquaredI  = 0;
  static long     sumI         = 0;
  static uint16_t sampleCount  = 0;
  static int      maxV = 0, minV = 1023;

  int valV = analogRead(PIN_VOLTAGE);
  int valI = analogRead(PIN_CURRENT);

  if (valV > maxV) maxV = valV;
  if (valV < minV) minV = valV;
  sumI        += valI;
  sumSquaredI += (long)valI * valI;
  sampleCount++;

  if (currentMillis - rmsStartTime >= RMS_PERIOD) {

    // --- Voltage: Peak-to-Peak ---
    rawV_PeakToPeak  = maxV - minV;
    int filteredRawV = (rawV_PeakToPeak <= NOISE_CUTOFF_V) ? 0 : rawV_PeakToPeak;
    float teganganInstan = (float)filteredRawV * KALIBRASI_VOLT;
    if (teganganRMS == 0.0f) {
      teganganRMS = teganganInstan;
    } else {
      teganganRMS = (teganganRMS * 0.92f) + (teganganInstan * 0.08f);
    }

    // --- Current: True-RMS via integer variance + isqrt32 ---
    if (sampleCount > 0) {
      uint32_t sc = (uint32_t)sampleCount;
      uint32_t sI = (sumI >= 0) ? (uint32_t)sumI : (uint32_t)(-sumI);
      uint32_t sSq = (uint32_t)sumSquaredI;

      uint32_t meanI_x8 = (sI * 8) / sc;
      uint32_t meanSqI_x64 = (sSq / sc) * 64 + ((sSq % sc) * 64) / sc;
      uint32_t sqMeanI_x64 = meanI_x8 * meanI_x8;
      uint32_t var_x64 = (meanSqI_x64 > sqMeanI_x64) ? (meanSqI_x64 - sqMeanI_x64) : 0;

      uint32_t rawI_scaled = isqrt32(var_x64);  // raw ADC RMS * 8

      if (rawI_scaled <= 10) {  // 1.2 * 8 ~= 10
        arusRMS = 0.0f;
        rawI_PeakToPeak = 0;
      } else {
        rawI_PeakToPeak  = (int)(rawI_scaled >> 3);
        float arusInstan = ((float)rawI_scaled * KALIBRASI_ARUS) / 8.0f;
        if (arusRMS == 0.0f) {
          arusRMS = arusInstan;
        } else {
          arusRMS = (arusRMS * 0.92f) + (arusInstan * 0.08f);
        }
      }
    }

    maxV = 0; minV = 1023;
    sumI = 0; sumSquaredI = 0; sampleCount = 0;
    rmsStartTime = currentMillis;
  }
}

// ============================================================================
// FUNCTION 2: MAIN STATE MACHINE
// ============================================================================
void logikaSistem(unsigned long currentMillis) {
  switch (currentState) {

    case STATE_INIT_DELAY:
      if (currentMillis - stateTimer >= STARTUP_DELAY) {
        if (teganganRMS >= VOLTAGE_UNDER_REC && teganganRMS <= VOLTAGE_OVER_REC) {
          digitalWrite(PIN_RELAY, HIGH);
          currentState = STATE_NORMAL_RUN;
          stateTimer = currentMillis;
          lraTimer = 0; ocpRunTimer = 0;
          mulaiBuzzer(1, 1000);
          Serial.println(F(">>> KOMPRESOR ON! Startup Delay Selesai."));
        } else if (teganganRMS < VOLTAGE_UNDER || teganganRMS > VOLTAGE_OVER) {
          masukError(teganganRMS < VOLTAGE_UNDER ? ERR_UVP : ERR_OVP, currentMillis);
        }
      }
      break;

    case STATE_NORMAL_RUN:
      digitalWrite(PIN_LED, HIGH);

      if (teganganRMS < VOLTAGE_SAG_CRITICAL) {
        Serial.println(F(">>> TRIP: Blackout/Kedip Sesaat! Safe Shutdown."));
        masukError(ERR_UVP, currentMillis);
        break;
      }
      if (teganganRMS < VOLTAGE_UNDER || teganganRMS > VOLTAGE_OVER) {
        if (voltageBadTimer == 0) voltageBadTimer = currentMillis;
        if (currentMillis - voltageBadTimer >= V_DROP_TOLERANCE)
          masukError(teganganRMS < VOLTAGE_UNDER ? ERR_UVP : ERR_OVP, currentMillis);
      } else {
        voltageBadTimer = 0;
      }

      if (arusRMS > CURRENT_LRA_MAX) {
        ocpRunTimer = 0;
        if (lraTimer == 0) lraTimer = currentMillis;
        if (currentMillis - lraTimer >= LRA_TIME) {
          Serial.println(F(">>> TRIP: LRA/Macet (>1.6A)! Cutoff 2s."));
          prosesOcpRetry(currentMillis);
        }
      } else if (arusRMS > CURRENT_RUN_MAX) {
        lraTimer = 0;
        if (ocpRunTimer == 0) ocpRunTimer = currentMillis;
        if (currentMillis - ocpRunTimer >= OCP_RUN_DELAY) {
          Serial.println(F(">>> TRIP: Overcurrent Running (>1.2A)! Cutoff 4s."));
          prosesOcpRetry(currentMillis);
        }
      } else {
        lraTimer = 0; ocpRunTimer = 0;
      }
      break;

    case STATE_ERROR:
      if (currentMillis - stateTimer >= STARTUP_DELAY) {
        if (teganganRMS >= VOLTAGE_UNDER_REC && teganganRMS <= VOLTAGE_OVER_REC) {
          digitalWrite(PIN_RELAY, HIGH);
          currentState = STATE_NORMAL_RUN;
          currentError = ERR_NONE;
          stateTimer = currentMillis;
          lraTimer = 0; ocpRunTimer = 0;
          mulaiBuzzer(1, 1000);
          Serial.println(F(">>> RECOVERY: Delay 5m Selesai. KOMPRESOR ON!"));
        }
      }
      if (currentMillis - buzzerTimer >= BUZZER_INT_ERROR) {
        mulaiBuzzer(3, 100);
        buzzerTimer = currentMillis;
      }
      break;

    case STATE_LOCKOUT:
      if (currentMillis - buzzerTimer >= BUZZER_INT_LOCKOUT) {
        mulaiBuzzer(3, 100);
        buzzerTimer = currentMillis;
      }
      break;
  }
}

// ============================================================================
// HELPER: OCP RETRY
// ============================================================================
void prosesOcpRetry(unsigned long currentMillis) {
  ocpRetryCount++;
  ocpRunTimer = 0; lraTimer = 0;

  if (ocpRetryCount >= OCP_MAX_RETRY) {
    digitalWrite(PIN_RELAY, LOW);
    currentState = STATE_LOCKOUT;
    currentError = ERR_OCP;
    buzzerTimer  = currentMillis;
    eeprom_save(EEPROM_ADDR_LAST_ERR, (uint8_t)ERR_OCP);
    mulaiBuzzer(3, 100);
    Serial.println(F(">>> LOCKOUT: OCP 3x! Butuh Reset Manual!"));
  } else {
    masukError(ERR_OCP, currentMillis);
  }
}

// ============================================================================
// HELPER: ENTER ERROR STATE
// ============================================================================
void masukError(ErrorType err, unsigned long currentMillis) {
  digitalWrite(PIN_RELAY, LOW);
  currentState    = STATE_ERROR;
  currentError    = err;
  stateTimer      = currentMillis;
  buzzerTimer     = currentMillis;
  voltageBadTimer = 0; ocpRunTimer = 0; lraTimer = 0;

  eeprom_save(EEPROM_ADDR_LAST_ERR, (uint8_t)err);

  mulaiBuzzer(3, 100);
  Serial.print(F(">>> SYSTEM CUT-OFF! Error Code: "));
  Serial.println((int)err);
}

// ============================================================================
// FUNCTION 3: SERIAL PRINT (every 500ms)
// ============================================================================
// Helper: print float with 1 decimal without pulling in double-precision Print::print(double)
static void printVolt(float v) {
  if (v < 0.0f) { Serial.print('-'); v = -v; }
  long ip = (long)v;
  long fp = (long)((v - (float)ip) * 10.0f + 0.5f);
  if (fp >= 10) { ip++; fp -= 10; }
  Serial.print(ip);
  Serial.print('.');
  Serial.print(fp);
}

// Helper: print float with 3 decimals without pulling in double-precision Print::print(double)
static void printCurrent(float a) {
  if (a < 0.0f) { Serial.print('-'); a = -a; }
  long ip = (long)a;
  long fp = (long)((a - (float)ip) * 1000.0f + 0.5f);
  if (fp >= 1000) { ip++; fp -= 1000; }
  Serial.print(ip);
  Serial.print('.');
  if (fp < 100) Serial.print('0');
  if (fp < 10)  Serial.print('0');
  Serial.print(fp);
}

void cetakDataSerial(unsigned long currentMillis) {
  static unsigned long lastSerialPrint = 0;
  if (currentMillis - lastSerialPrint >= 500) {
    lastSerialPrint = currentMillis;
    if (currentState != STATE_LOCKOUT) {
      Serial.print(F("[State: ")); Serial.print((int)currentState);
      Serial.print(F("] | Raw V_PP: ")); Serial.print(rawV_PeakToPeak);
      Serial.print(F(" => Volt: ")); printVolt(teganganRMS);
      Serial.print(F(" V  ||  Raw I_PP: ")); Serial.print(rawI_PeakToPeak);
      Serial.print(F(" => Arus: ")); printCurrent(arusRMS);
      Serial.println(F(" A"));
    }
  }
}

// ============================================================================
// FUNCTION 4: LED ENGINE (non-blocking blink)
// ============================================================================
void engineLED(unsigned long currentMillis) {
  if (currentState == STATE_NORMAL_RUN) return;

  if (currentError == ERR_NONE) {
    if (currentMillis - ledTimer >= 1000) {
      ledTimer = currentMillis;
      digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }
  } else {
    static byte          blinkCounter  = 0;
    static bool          isOn          = false;
    static unsigned long errorLedTimer = 0;
    int targetBlinks = (int)currentError;
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
        blinkCounter  = 0;
      }
    }
  }
}

// ============================================================================
// FUNCTION 5: BUZZER ENGINE (non-blocking)
// ============================================================================
void mulaiBuzzer(int jmlBeep, int durasi) {
  buzzerTarget = jmlBeep * 2;
  buzzerDurasi = durasi;
  currentBeeps = 0;
  buzzerIsOn   = false;
}

void engineBuzzer(unsigned long currentMillis) {
  static unsigned long lastBuzzerToggle = 0;
  if (currentBeeps < buzzerTarget) {
    if (currentMillis - lastBuzzerToggle >= (unsigned long)buzzerDurasi) {
      lastBuzzerToggle = currentMillis;
      buzzerIsOn = !buzzerIsOn;
      digitalWrite(PIN_BUZZER, buzzerIsOn);
      currentBeeps++;
    }
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }
}
