# Historical Design Baseline: Firmware_D Original Specification

**Document:** `docs/FIRMWARE_D_ORIGINAL_BASELINE.md`  
**Status:** Historical Reference Baseline (Frozen)  
**Target:** WCH CH32V003 (SOP16 Package)  

---

## 1. Document Purpose & Historical Scope
This document records the original engineering baseline, architectural intent, and historical design specifications that guided the initial development of the `CompressorProtect` project. 

> [!IMPORTANT]
> **Historical Baseline Rule:** This document reflects the historical design intent. It must **never** be silently rewritten to match modern code. Where modern implementation or verified hardware diverges from this baseline, the discrepancies are tracked and governed by [`docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md) and [`docs/FIRMWARE_D_MASTER_SPEC.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_MASTER_SPEC.md).

---

## 2. Historical 10-File Modular Architecture
The foundational architecture established a 10-file modular structure designed to run on the WCH CH32V003 microcontroller:

1. **`[#01] config.h`**: Hardware pin assignments, clock parameters, timing constants, and default protection thresholds.
2. **`[#02] system.h`**: Shared system data structures, enums, global telemetry (`SensorData_t`), and persistent configuration schema (`SystemConfig_t`).
3. **`[#03] hal_sys.c`**: Hardware abstraction layer, GPIO direction, fail-safe pin initialization, 1 ms SysTick timer, and Independent Watchdog (IWDG).
4. **`[#04] nvm_flash.c`**: Non-volatile storage management, utilizing the top 64-byte Flash page with CRC16 data integrity checks.
5. **`[#05] adc_dsp.c`**: Hardware-timed ADC sampling (TIM1 TRGO @ 6400 Hz), circular DMA ping-pong buffering, DC-offset compensation, and true-RMS power calculation.
6. **`[#06] display_tm1637.c`**: Non-blocking TM1637 4-digit 7-segment LED display driver with formatting for voltage, current, timers, and fault codes.
7. **`[#07] input_alerts.c`**: Analog resistor-ladder button scanning, debouncing, auto-repeat, boot jumper detection, status LED sequencing, and buzzer alert tones.
8. **`[#08] fsm_protect.c`**: Multi-state compressor protection finite state machine (Delay, Run, Trip, Lockout) and 20 ms interrupt-driven trip execution.
9. **`[#09] menu_cal.c`**: Front-panel parameter configuration menu (`Sett`, P01..P09) and multi-step factory calibration wizard (`C-0`, `C-V`, `C-A`).
10. **`[#10] main.c`**: System boot orchestrator, hardware self-test, mode dispatcher, and cooperative non-blocking superloop.

---

## 3. Original Four-Part Master System Flowchart

> [!NOTE]
> **Canonical Master Flowchart Placeholder:**  
> The original four-part Master System Flowchart text will be inserted directly into this section as provided by the project architect. Do not reconstruct missing sections or invent requirements.

### Part 1: Power-On Initialization & Sensor Calibration
*(Reserved for exact user baseline text)*

### Part 2: Operating States, Delay Countdown & Compressor Startup
*(Reserved for exact user baseline text)*

### Part 3: Real-Time Protection Supervisor & Fault Trip Execution
*(Reserved for exact user baseline text)*

### Part 4: Lockout Recovery, Parameter Menu & Non-Volatile Storage
*(Reserved for exact user baseline text)*

---

## 4. Known Historical Assumptions Identified as Obsolete
During the evolution from initial prototyping to verified physical hardware, several initial assumptions were superseded:

1. **NTC Thermistor Analog Channels:**
   - *Historical Assumption:* Dual NTC thermistors (NTC1 Compressor Body, NTC2 Suction/Liquid Line) were routed to header pins PC3 and PC2.
   - *Physical Reality:* On the CH32V003A4M6 SOP16 package, pins PC2 and PC3 have **no internal connection to the ADC multiplexer**. Temperature protection cannot be performed via analog sampling on these pins. Firmware_D correctly disables NTC features (`ENABLE_NTC 0`).
2. **EEPROM Emulation via Option Bytes:**
   - *Historical Assumption:* Early Arduino prototypes attempted to use WCH Option Bytes as pseudo-EEPROM.
   - *Physical Reality:* Option Bytes on CH32V003 are extremely limited (user data area is only 4 to 8 bytes). Firmware_D utilizes the top 64-byte Flash page (`0x08003FC0`) via the CH32V003 Fast-Page Program controller.
3. **Historical GPIO Net Discrepancies:**
   - Early schematics (`archive_old/CH32Version.png`) routed buzzer to PC1 and display to PC4/PC6.
   - Verified physical boards (EasyEDA V1.0) route buzzer to PC0, TM1637 DIO to PC3, TM1637 CLK to PC4, and CAL jumper to PC6.
