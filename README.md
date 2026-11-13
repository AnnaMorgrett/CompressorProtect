# CompressorProtect

**Professional AC Mains Refrigerator Compressor Protection Controller**  
*Target Architecture: WCH CH32V003 (32-bit RISC-V RV32EC @ 48 MHz)*  
*Hardware Reference: `archive_old/CH32Version.png` (CH32V003A4M6 SOP-16 / TSSOP-20)*  

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#compilation-and-build-verification)
[![Platform](https://img.shields.io/badge/platform-WCH%20CH32V003-blue.svg)](https://www.wch-ic.com/products/CH32V003.html)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

---

## 1. Project Overview

**CompressorProtect** is an intelligent embedded controller designed to safeguard refrigeration and air conditioning compressors from severe electrical anomalies, brownouts, sustained overvoltage, stalled-rotor conditions (LRA), and continuous motor overloads. 

Compressors in hermetic refrigeration circuits rely on proper suction-gas cooling and balanced differential pressures before starting. Attempting to start against unequal head pressure or running at abnormal mains voltages causes rotor stalling, overheating, capacitor degradation, and catastrophic motor winding failure. 

CompressorProtect integrates continuous cycle-by-cycle AC mains voltage sensing (ZMPT101B), true current RMS monitoring (ZHT103), a non-blocking 5-minute pressure equalization restart delay, a 3-button resistor-ladder user interface, a 4-digit TM1637 display, Option-Bytes non-volatile configuration memory, and a hardware independent watchdog (IWDG).

---

## 2. Key Engineering Features

- **AC Mains Voltage Protection:**
  - **Over-Voltage Protection (OVP):** Trips when voltage exceeds configured threshold (Default: `260 V`) for $> 5000\text{ ms}$.
  - **Under-Voltage Protection (UVP):** Trips when voltage drops below configured threshold (Default: `175 V`) for $> 5000\text{ ms}$.
  - **Critical Voltage Sag Cutoff:** Trips immediately ($0\text{ ms}$ delay) when line voltage plunges below `160 V` to protect motor torque.
  - **Hysteresis Recovery Window:** Enforces $+10\text{ V}$ recovery hysteresis for UVP (`185 V`) and $-10\text{ V}$ for OVP (`250 V`).
  - **Dead-Band Elimination:** Dedicated power qualification state prevents indefinite waiting when voltage sits between trip and recovery limits.
- **Dual-Threshold Motor Current Protection:**
  - **Locked Rotor Current (LRA):** High-speed trip ($2000\text{ ms}$) when current exceeds stalled threshold (Default: `1.6 A`).
  - **Running Overload (OCP):** Inverse-time trip ($4000\text{ ms}$) when continuous running current exceeds limit (Default: `1.2 A`).
  - **Motor Inrush Blanking:** $1500\text{ ms}$ blanking window during starting state accommodates normal inductive starting surges without nuisance tripping.
- **Rolling Retry & Lockout Supervisor:**
  - Permitted up to 3 automated restart attempts.
  - **Rolling Window Reset:** Continuous successful operation in `STATE_NORMAL_RUN` for 10 minutes automatically clears the retry counter.
  - **Lockout (`LOCK`):** After 3 consecutive trips without a sustained run, the system locks the relay OFF permanently, sounding periodic alerts. Requires manual reset via OK/Menu button once line voltage is confirmed healthy.
- **Compressor Pressure Equalization Restart Timer:**
  - Enforces a 300-second ($5\text{ minute}$) non-blocking countdown following boot, brownout, or fault trip to allow refrigerant high/low side pressure equalization.
  - **Quick-Start Bypass:** Holding the PLUS (`+`) button for 3 seconds during the countdown allows authorized technicians to bypass the delay if line voltage is verified safe.
- **TM1637 4-Digit Display & Resistor Ladder UI:**
  - Display rotates live telemetry: Voltage (` 220`), Current (` 1.20`), Thermistor Status (` -NC`), and State (` RUN`).
  - Settings Menu (`P-01` to `P-05`) for in-field tuning of OVP, UVP, Running Current, LRA Current, and Delay Timer.
  - 3-Button ladder on ADC pin `PD6` (`+`, `-`, `Menu/OK`) with debouncing, long press, and auto-repeat.
- **Microcontroller Reliability & Resource Footprint:**
  - Emulates 26-byte EEPROM using the internal CH32 Option Bytes flash page with XOR checksum validation.
  - Pure-integer 32-bit square root algorithm (`isqrt32`) eliminates soft-float square-root library overhead.
  - Hardware Independent Watchdog (IWDG ~2.0s) fed only after all critical protection tasks complete.
  - Total Flash usage: **16,004 bytes (97%)**; RAM usage: **996 bytes (48%)**.

---

## 3. Hardware Architecture & Schematic Reference

The primary hardware reference is **`archive_old/CH32Version.png`**.

```
                           +----------------------+
                           |   +12V Power Supply  |
                           +----------+-----------+
                                      |
                           +----------v-----------+
                           |  AMS1117-5.0 Regulator
                           +----------+-----------+
                                      | +5.0V Logic
                                      |
         +----------------------------+----------------------------+
         |                                                         |
+--------v---------+                                      +--------v---------+
|  ZMPT101B Sensor |                                      |  ZHT103 Current  |
|  VAC -> PA2 (A0) |                                      |  CT  -> PA1 (A1) |
+------------------+                                      +------------------+
         |                                                         |
         +--------------------+              +---------------------+
                              |              |
                    +---------v--------------v---------+
                    |        WCH CH32V003A4M6          |
                    |     (32-bit RISC-V RV32EC)       |
                    +----+-------+--------+-------+----+
                         |       |        |       |
      +------------------+       |        |       +------------------+
      |                          |        |                          |
+-----v-------+            +-----v----+  +-v--------+          +-----v-------+
| S8050 NPN   |            | S8050 NPN|  | Yellow   |          | Resistor    |
| Relay Driver|            | Buzzer   |  | LED      |          | Ladder (3B) |
| PD4 (Pin 8) |            | PC1 (P1) |  | PD5 (P9) |          | PD6 (P10/A6)|
+-------------+            +----------+  +----------+          +-------------+
      |                          |                                   |
[12V Relay K1]            [Buzzer 5V]                          [+, -, Menu]
```

### Verified MCU Pin Mapping Table

| Net Name | Schematic Net | Port | SOP16 Pin | ADC Capability | Configured Mode | Purpose / Connection |
|:---|:---|:---:|:---:|:---:|:---:|:---|
| `BUZZ` | `BUZZ` | `PC1` | Pin 1 | Digital Only | Output (Active HIGH) | Buzzer driver base (S8050 NPN) via R1 ($2.2\text{ k}\Omega$) |
| `NTC2` | `NTC2` | `PC2` | Pin 2 | **NO ADC** | Input (Uncommissioned) | Liquid-line thermistor (Hardware lacks ADC channel) |
| `NTC1` | `NTC1` | `PC3` | Pin 3 | **NO ADC** | Input (Uncommissioned) | Compressor shell thermistor (Hardware lacks ADC channel) |
| `DIO` | `DIO` | `PC4` | Pin 4 | AIN2 (Unused) | Digital Open-Drain | TM1637 4-digit display data line |
| `CLK` | `CLK` | `PC6` | Pin 5 | Digital Only | Digital Output | TM1637 4-digit display clock line |
| `CAL` | `CAL` | `PC7` | Pin 6 | Digital Only | Input / Debug TX | Calibration header H1 Pin 3 (Optional Debug UART) |
| `TX / SWIO`| `TX` | `PD1` | Pin 7 | Digital Only | Bidirectional | Single-wire debug & programming interface (SWIO) |
| `RELAY` | `RELAY` | `PD4` | Pin 8 | AIN7 (Unused) | Output (Active HIGH) | Compressor relay driver base (S8050 NPN) via R16 |
| `LED` | `LED` | `PD5` | Pin 9 | AIN5 (Unused) | Output (Active HIGH) | Status Yellow LED via R18 ($4.7\text{ k}\Omega$) |
| `KEY` | `KEY` | `PD6` | Pin 10 | **AIN6 (Channel 6)** | Analog Input | 3-button resistor ladder (+, Menu, -) via R24 |
| `NC / RST` | `PD7` | `PD7` | Pin 11 | Digital Only | Unconnected / Reset | Unconnected on PCB (Marked with red X) |
| `CT` | `CT` | `PA1` | Pin 12 | **AIN1 (Channel 1)** | Analog Input | ZHT103 Current Transformer signal with DC bias |
| `VAC` | `VAC` | `PA2` | Pin 13 | **AIN0 (Channel 0)** | Analog Input | ZMPT101B Voltage Transformer signal with DC bias |
| `VSS` | `GND` | `GND` | Pin 14 | Ground | Power Supply | Ground reference ($0\text{ V}$) |
| `VDD` | `+5V` | `VDD` | Pin 15 | Power | Power Supply | Regulated $+5.0\text{ V}$ logic power rail |
| `PROG` | `PROG` | `PC0` | Pin 16 | Digital Only | Input | Programming Header H1 Pin 2 |

---

## 4. Hardware Limitations & Safety Integrity

### Critical Hardware Finding: NTC1 & NTC2 Thermistors
In the schematic `archive_old/CH32Version.png`, connectors CN1 (`NTC1`) and CN2 (`NTC2`) are routed to `PC3` (Pin 3) and `PC2` (Pin 2).  
- **Silicon Reality:** On the WCH CH32V003 silicon, the internal analog multiplexer connects ADC channels **only** to `PA2` (AIN0), `PA1` (AIN1), `PC4` (AIN2), `PD2` (AIN3), `PD3` (AIN4), `PD5` (AIN5), `PD6` (AIN6), and `PD4` (AIN7).  
- **Conclusion:** Neither `PC2` nor `PC3` has ADC hardware capabilities.
- **Firmware Handling:** The firmware explicitly marks both NTC sensors as `UNCOMMISSIONED` / `SENSOR_STATUS_UNAVAILABLE_NO_ADC`. The display reports ` -NC` (Not Connected / No ADC). The firmware **never fabricates Celsius readings** or falsely advertises active thermal protection. Primary compressor motor overtemperature protection continues to be reliably provided by the compressor's independent internal bimetallic thermal overload protector.

---

## 5. User Interface & Menu Parameters

The 3-button resistor ladder is sampled through ADC pin `PD6`:
- **Plus (`+`, U11, $2.2\text{ k}\Omega$):** ADC range $100 - 240$ (Nominal ~184)
- **Menu/OK (`OK`, U10, $4.7\text{ k}\Omega$):** ADC range $240 - 420$ (Nominal ~327)
- **Minus (`-`, U9, $10\text{ k}\Omega$):** ADC range $420 - 650$ (Nominal ~512)
- **Idle (No key pressed):** ADC $> 850$ (Nominal ~1023)

### Configurable Parameter Table

| Code | Parameter Description | Minimum | Maximum | Step | Factory Default | Unit |
|:---:|:---|:---:|:---:|:---:|:---:|:---:|
| **`P-01`** | Over-Voltage Trip Threshold (OVP) | 230 | 280 | 5 | **260** | Volts AC |
| **`P-02`** | Under-Voltage Trip Threshold (UVP) | 150 | 210 | 5 | **175** | Volts AC |
| **`P-03`** | Continuous Running Overload Limit (OCP) | 0.5 | 3.0 | 0.1 | **1.2** | Amperes |
| **`P-04`** | Stalled Motor / Locked Rotor Limit (LRA) | 1.0 | 5.0 | 0.1 | **1.6** | Amperes |
| **`P-05`** | Compressor Minimum OFF Restart Delay | 10 | 600 | 10 | **300** | Seconds |

---

## 6. Compilation and Build Verification

The firmware compiles cleanly under the official WCH toolchain (`arduino-cli` / Arduino IDE 2.x):
```bash
# Compile and export binaries
arduino-cli compile -b WCH:ch32v:CH32V00x_EVT:pnum=CH32V003F4,clock=48MHz_HSI,opt=osstd,dbg=none,rtlib=nano firmware --export-binaries
```

**Build Output:**
```text
Sketch uses 16004 bytes (97%) of program storage space. Maximum is 16384 bytes.
Global variables use 996 bytes (48%) of dynamic memory, leaving 1052 bytes for local variables. Maximum is 2048 bytes.
```

### Flashing via `minichlink`
Connect a WCH-LinkE (or compatible SWIO programmer) to the board:
- `3V3` or `5V` $\rightarrow$ Header H1 Pin 1 (`+5V`)
- `SWIO` $\rightarrow$ MCU Pin 7 (`PD1` / `TX` on H1 Pin 2)
- `GND` $\rightarrow$ Header H1 Pin 4 (`GND`)

```powershell
cd firmware/build/WCH.ch32v.CH32V00x_EVT
minichlink -w firmware.ino.hex 0x08000000 -b
```

---

## 7. Project Documentation

Comprehensive technical documentation is provided in the [`docs/`](docs/) directory:
- [Architecture & Module Breakdown](docs/ARCHITECTURE.md)
- [System Flowcharts (16 Mermaid Diagrams)](docs/FLOWCHARTS.md)
- [Hardware Net-to-Pin Mapping & Package Audit](docs/HARDWARE_PINMAP.md)
- [Protection Logic & Safety Hysteresis](docs/PROTECTION_LOGIC.md)
- [Sensor Calibration & Analog Conversion](docs/SENSOR_CALIBRATION.md)
- [User Interface & Button Operation](docs/USER_INTERFACE.md)
- [Non-Volatile Storage & Option Bytes](docs/CONFIGURATION.md)
- [Centralized Fault Code Specification](docs/FAULT_CODES.md)
- [Testing & Validation Plan](docs/TEST_PLAN.md)
- [Toolchain Setup, Build & Flashing Guide](docs/BUILD_AND_FLASH.md)
- [Firmware Changelog](docs/CHANGELOG.md)

---

## 8. Safety Disclaimer

> [!WARNING]
> This firmware controls high-voltage alternating current (AC mains: $110\text{V} - 240\text{V}$) and mechanical refrigeration equipment. Mains electricity presents severe risks of electric shock, electrocution, and electrical fire. Always disconnect mains power before servicing. Ensure the compressor retains its original manufacturer-installed thermal overload protector.

