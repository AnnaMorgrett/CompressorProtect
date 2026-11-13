# System Flowcharts & Visual Architecture

**Project:** CompressorProtect  
**Target:** WCH CH32V003 (32-bit RISC-V RV32EC @ 48 MHz)  
**Document:** `docs/FLOWCHARTS.md`  

---

## 1. Overall System Architecture

```mermaid
flowchart TD
    subgraph Inputs["Analog & Digital Inputs"]
        V_IN["ZMPT101B (AC Voltage) -> PA2 (AIN0)"]
        I_IN["ZHT103 (CT Current) -> PA1 (AIN1)"]
        K_IN["3-Button Ladder -> PD6 (AIN6)"]
        NTC_IN["NTC1 (PC3) & NTC2 (PC2)<br/>(Uncommissioned - No ADC)"]
    end

    subgraph Core["CH32V003 Firmware Core"]
        SM["Sensor Manager<br/>(Acquisition & RMS Filtering)"]
        BM["Button Manager<br/>(Debounce & Events)"]
        PROT["Safety Supervisor FSM<br/>(Highest Priority)"]
        DM["Display Manager<br/>(TM1637 Bit-Bang)"]
        AM["Alarm Manager<br/>(LED & Buzzer)"]
        NVM["Storage Manager<br/>(Option Bytes EEPROM)"]
        WDG["IWDG Watchdog<br/>(2.0s Hardware Timeout)"]
    end

    subgraph Outputs["Actuators & Interface"]
        RELAY["Relay Driver (S8050 NPN) -> PD4"]
        LED["Yellow LED -> PD5"]
        BUZZ["Alarm Buzzer -> PC1"]
        DISP["TM1637 4-Digit Display -> PC4/PC6"]
    end

    V_IN & I_IN & K_IN --> SM
    K_IN --> BM
    SM & BM --> PROT
    PROT --> RELAY
    PROT & SM --> DM
    PROT --> AM
    PROT <--> NVM
    DM --> DISP
    AM --> LED & BUZZ
    PROT --> WDG
```

---

## 2. Power-On Initialization Sequence

```mermaid
flowchart TD
    Start([Power On / Hardware Reset]) --> A["Force PIN_RELAY (PD4) LOW immediately"]
    A --> B["Initialize GPIOs (PD5 LED, PC1 Buzzer, PC4/PC6 Display, PD6 Key)"]
    B --> C["Initialize Storage: Read Option Bytes & verify checksum"]
    C --> D{"Checksum & Magic Valid?"}
    D -- No --> E["Load Factory Defaults & Save to Option Bytes"]
    D -- Yes --> F["Load Parameters into Active RAM"]
    E --> F
    F --> G["Initialize Sensor Manager: Clear ADC accumulators"]
    G --> H["Initialize Button Manager: Set debounce state to IDLE"]
    H --> I["Initialize TM1637: Send 0x8F, Display '----'"]
    I --> J["Arm Hardware Independent Watchdog (IWDG ~2.0s)"]
    J --> K["Enter STATE_BOOT_SAFE (200 ms)"]
    K --> End([Advance to SELF_TEST])
```

---

## 3. Sensor Self-Test

```mermaid
flowchart TD
    Start([Enter STATE_SELF_TEST]) --> A["Wait 500 ms for power rails to stabilize"]
    A --> B["Sample PA2 (VAC ADC Channel 0)"]
    B --> C["Sample PA1 (CT ADC Channel 1)"]
    C --> D["Sample PD6 (KEY ADC Channel 6)"]
    D --> E{"KEY ADC Shorted (<80)?"}
    E -- Yes --> F["Flag Button Ladder Short"]
    E -- No --> G["Verify NTC1 (PC3) and NTC2 (PC2)"]
    F --> G
    G --> H["Record NTC Status: SENSOR_STATUS_UNAVAILABLE_NO_ADC"]
    H --> I["Self-Test Completed"]
    I --> End([Advance to STATE_POWER_QUALIFY])
```

---

## 4. Voltage Qualification

```mermaid
flowchart TD
    Start([Enter STATE_POWER_QUALIFY]) --> A["Relay remains forced OFF"]
    A --> B["Acquire 20 ms AC cycle on PA2"]
    B --> C["Compute V_fast = V_pp * 1.635"]
    C --> D{"Is Voltage Valid (50V..320V)?"}
    D -- No --> E["Reset Qualification Timer"]
    D -- Yes --> F{"Is V_fast within Recovery Window?<br/>(185V <= V <= 250V)"}
    F -- No --> E
    F -- Yes --> G{"Qualification Timer >= 3000 ms?"}
    G -- No --> H["Increment Timer & Continue Qualification"]
    G -- Yes --> I["Voltage verified healthy & stable"]
    I --> End([Advance to STATE_WAIT_MIN_OFF])
```

---

## 5. Compressor Startup Sequence

```mermaid
flowchart TD
    Start([Countdown Expired in STATE_WAIT_MIN_OFF]) --> A["Enter STATE_STARTING"]
    A --> B["Energize Compressor Relay (PD4 = HIGH)"]
    B --> C["Record Start Timestamp (startElapsed = 0)"]
    C --> D["Sample AC Mains Voltage (PA2)"]
    D --> E{"V_fast < 160V (Critical Sag)?"}
    E -- Yes --> F["Trip immediately: FAULT_CRITICAL_VOLTAGE_SAG"]
    E -- No --> G{"startElapsed < 1500 ms<br/>(Inrush Blanking Window)?"}
    G -- Yes --> H["Tolerate Motor Starting Surge; Continue"]
    G -- No --> I["Sample Compressor Current (PA1)"]
    I --> J{"Current > I_LRA (1.6A)?"}
    J -- Yes --> K{"LRA Timer >= 2000 ms?"}
    K -- Yes --> L["Trip: FAULT_LOCKED_ROTOR (Stalled Motor)"]
    K -- No --> M["Increment LRA Timer"]
    J -- No --> N["Current settled into normal operating range"]
    N --> End([Advance to STATE_NORMAL_RUN])
```

---

## 6. Normal Operation Loop

```mermaid
flowchart TD
    Start([STATE_NORMAL_RUN]) --> A["Relay remains ENERGIZED (PD4 = HIGH)"]
    A --> B["Sample Voltage (PA2) & Current (PA1)"]
    B --> C["Evaluate Critical Voltage Sag (< 160V)"]
    C -- Detected --> D["Immediate Trip (0 ms delay)"]
    C -- Normal --> E["Evaluate Sustained Voltage Limits (OVP / UVP)"]
    E -- Exceeded --> F["Evaluate Overvoltage / Undervoltage Timers"]
    E -- Normal --> G["Evaluate Motor Current Limits (LRA / OCP)"]
    G -- Exceeded --> H["Evaluate LRA / OCP Timers"]
    G -- Normal --> I{"Continuous Run >= 10 Minutes?"}
    I -- Yes --> J["Reset Retry Counter to 0 (Rolling Window)"]
    I -- No --> K["Continue Monitoring"]
    J --> K
    K --> End([Repeat Loop])
```

---

## 7. Voltage Protection Flow

```mermaid
flowchart TD
    Start([Evaluate AC Voltage]) --> A{"V_fast < 160V?"}
    A -- Yes --> B["Trip immediately: FAULT_CRITICAL_VOLTAGE_SAG"]
    A -- No --> C{"V_fast > OVP Limit (260V)?"}
    C -- Yes --> D{"OVP Timer >= 5000 ms?"}
    D -- Yes --> E["Trip: FAULT_OVERVOLTAGE"]
    D -- No --> F["Increment OVP Timer"]
    C -- No --> G{"V_fast < UVP Limit (175V)?"}
    G -- Yes --> H{"UVP Timer >= 5000 ms?"}
    H -- Yes --> I["Trip: FAULT_UNDERVOLTAGE"]
    H -- No --> J["Increment UVP Timer"]
    G -- No --> K["Clear Voltage Abnormal Timers"]
    B & E & I --> L["De-energize Relay (PD4 = LOW)<br/>Enter STATE_FAULT_RECOVERY"]
```

---

## 8. Current Protection Flow

```mermaid
flowchart TD
    Start([Evaluate Compressor Current]) --> A{"Current > I_LRA (1.6A)?"}
    A -- Yes --> B{"LRA Timer >= 2000 ms?"}
    B -- Yes --> C["Trip: FAULT_LOCKED_ROTOR"]
    B -- No --> D["Increment LRA Timer"]
    A -- No --> E["Clear LRA Timer"]
    E --> F{"Current > I_RUN (1.2A)?"}
    F -- Yes --> G{"OCP Run Timer >= 4000 ms?"}
    G -- Yes --> H["Trip: FAULT_RUNNING_OVERCURRENT"]
    G -- No --> I["Increment OCP Run Timer"]
    F -- No --> J["Clear OCP Run Timer"]
    C & H --> K["De-energize Relay (PD4 = LOW)<br/>Increment Retry Counter<br/>Enter STATE_FAULT_RECOVERY"]
```

---

## 9. Compressor Thermal Protection Logic (Hardware Constrained)

```mermaid
flowchart TD
    Start([Read NTC1 on PC3]) --> A["Check CH32V003 Hardware Capability"]
    A --> B["Identify: PC3 has NO ADC multiplexer channel"]
    B --> C["Assign Status: SENSOR_STATUS_UNAVAILABLE_NO_ADC"]
    C --> D["Set Temperature Value: Sentinel -999 C"]
    D --> E["Display Reports: ' -NC' (Not Connected / No ADC)"]
    E --> F["Flag Software Status: Uncommissioned"]
    F --> G["Safety Rule: Do NOT fabricate simulated Celsius data"]
    G --> H["Rely on compressor manufacturer's internal thermal protector"]
```

---

## 10. Refrigeration-Line Diagnostic Processing (Hardware Constrained)

```mermaid
flowchart TD
    Start([Read NTC2 on PC2]) --> A["Check CH32V003 Hardware Capability"]
    A --> B["Identify: PC2 has NO ADC multiplexer channel"]
    B --> C["Assign Status: SENSOR_STATUS_UNAVAILABLE_NO_ADC"]
    C --> D["Set Temperature Value: Sentinel -999 C"]
    D --> E["Display Reports: ' -NC' (Not Connected / No ADC)"]
    E --> F["Safety Rule: Inhibit arbitrary automated line clog shutdown"]
    F --> G["Mark Diagnostics: Unavailable without hardware rev"]
```

---

## 11. Fault Recovery Flow

```mermaid
flowchart TD
    Start([Enter STATE_FAULT_RECOVERY]) --> A["Relay forced OFF (PD4 = LOW)"]
    A --> B{"Did current trip occur?"}
    B -- Yes --> C{"Retry Counter >= 3?"}
    C -- Yes --> D["Enter STATE_LOCKOUT"]
    C -- No --> E["Wait for Voltage Qualification"]
    B -- No --> E
    E --> F{"Voltage in Recovery Window?<br/>(185V <= V <= 250V)"}
    F -- No --> G["Reset Qualification Timer"]
    F -- Yes --> H{"Recovery Voltage Stable >= 3000 ms?"}
    H -- No --> I["Increment Qualification Timer"]
    H -- Yes --> J["Clear Active Fault<br/>Begin 300s Countdown in STATE_WAIT_MIN_OFF"]
```

---

## 12. Lockout and Manual Reset Flow

```mermaid
flowchart TD
    Start([Enter STATE_LOCKOUT]) --> A["Relay locked permanently OFF"]
    A --> B["Display flashes 'LOCK' alternating with Fault Code"]
    B --> C["Buzzer emits 2 short chirps every 15 minutes"]
    C --> D{"User presses OK/Menu button?"}
    D -- No --> C
    D -- Yes --> E["Sample AC Mains Voltage (PA2)"]
    E --> F{"Is Voltage in Recovery Window?<br/>(185V <= V <= 250V)?"}
    F -- No --> G["SAFETY INTERLOCK: REJECT RESET<br/>Sound error chirp; Stay in Lockout"]
    F -- Yes --> H["RESET ACCEPTED<br/>Clear Retry Counter & Fault<br/>Enter STATE_POWER_QUALIFY"]
```

---

## 13. Three-Button Navigation Flow

```mermaid
flowchart TD
    Start([Sample PD6 Resistor Ladder]) --> A{"Decode ADC Range"}
    A -- "100..240" --> B["Key: PLUS (+)"]
    A -- "241..420" --> C["Key: MENU (OK)"]
    A -- "421..650" --> D["Key: MINUS (-)"]
    A -- ">850" --> E["Key: IDLE (Released)"]
    A -- Other --> F["Filter Transient Bounce"]

    B & C & D --> G{"Evaluate Hold Duration"}
    G -- "< 1500 ms release" --> H["Emit SHORT Press Event"]
    G -- ">= 1500 ms hold" --> I["Emit LONG Press Event"]
    G -- "Held > 500 ms (Plus/Minus)" --> J["Emit REPEAT Event (every 150ms)"]
```

---

## 14. Parameter Editing & Option Bytes Validation Flow

```mermaid
flowchart TD
    Start([Hold Menu for >1.5s in Home Mode]) --> A["Enter DISP_MODE_MENU_PARAM<br/>Show 'P-01'"]
    A --> B{"Button Pressed?"}
    B -- Plus / Minus --> C["Cycle parameter index (P-01 .. P-05)"]
    B -- Menu Long --> D["Exit Menu without saving"]
    B -- Menu Short --> E["Enter DISP_MODE_MENU_EDIT<br/>Display current value"]
    E --> F{"Edit Mode Button Pressed?"}
    F -- Plus / Minus --> G["Increment / Decrement within safe bounds"]
    F -- Menu Long --> H["Cancel edit; return to P-xx"]
    F -- Menu Short --> I["Calculate XOR Checksum<br/>Write to Option Bytes via EEPROM.update<br/>Commit Flash"]
    I --> J["Display 'SAUE' for 1.2s<br/>Sound confirmation chirp"]
    J --> A
```

---

## 15. Status LED & Alarm Buzzer Behavior

```mermaid
flowchart TD
    Start([alarmUpdate]) --> A{"Current System State"}
    A -- "BOOT / SELF_TEST" --> B["LED: Solid ON, Buzzer: OFF"]
    A -- "WAIT_MIN_OFF / QUALIFY" --> C["LED: 1 Hz Slow Blink (500ms ON / 500ms OFF)"]
    A -- "NORMAL_RUN" --> D["LED: Solid ON"]
    A -- "FAULT_RECOVERY" --> E["LED: Double Pulse (Voltage) or Triple Pulse (Current)<br/>Buzzer: Chirp every 30 minutes"]
    A -- "LOCKOUT" --> F["LED: Rapid Strobe (5 Hz)<br/>Buzzer: 2 Chirps every 15 minutes"]
    
    G{"User presses Menu/OK during alarm?"} --> H["alarmMuteBuzzer(): Silence buzzer until next fault"]
```

---

## 16. Watchdog Servicing & Stall Recovery

```mermaid
flowchart TD
    Start([Main Loop Execution]) --> A["1. sensorUpdate (ADC & AC Cycle)"]
    A --> B["2. buttonUpdate (Resistor Ladder)"]
    B --> C["3. protectionUpdate (Safety Supervisor FSM)"]
    C --> D["4. displayUpdate (TM1637 200ms)"]
    D --> E["5. alarmUpdate (LED & Buzzer)"]
    E --> F["6. diagnosticsUpdate (Optional Telemetry)"]
    F --> G["7. IWDG_ReloadCounter() (Feed Watchdog)"]
    G --> End([Loop Iteration Complete])

    H["Loop hangs / blocked > 2.0s"] -.-> I["IWDG Hardware Counter reaches 0"]
    I -.-> J["Microcontroller Hardware Reset Triggered"]
    J -.-> K["Relay de-energizes instantly via 10k pulldown<br/>Restarts at STATE_BOOT_SAFE"]
```

