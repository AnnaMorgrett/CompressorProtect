# CompressorProtect

**Professional AC Mains Refrigerator Compressor Protection Controller**  
*Target Architecture: WCH CH32V003A4M6 (32-bit RISC-V RV32EC @ 48 MHz)*  
*Framework: Bare-Metal C (`ch32v003fun` via PlatformIO)*

[![Platform](https://img.shields.io/badge/platform-WCH%20CH32V003A4M6-blue.svg)](https://www.wch-ic.com/products/CH32V003.html)
[![Framework](https://img.shields.io/badge/framework-ch32v003fun-orange.svg)](https://github.com/cnlohr/ch32v003fun)
[![Build System](https://img.shields.io/badge/build-PlatformIO-brightgreen.svg)](https://platformio.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

---

## 1. Project Overview

**CompressorProtect** is a bare-metal embedded controller built on the WCH `ch32v003fun` framework to protect hermetic refrigeration and air conditioning compressors against severe AC mains anomalies, brownouts, voltage surges, stalled-rotor conditions (LRA), continuous motor overloads (OCP), undercurrent/dry-run states (UCP), and excessive continuous run durations (ORP).

Compressors in closed refrigeration circuits require balanced high/low side differential pressures before starting. Starting against high head pressure or operating outside safe voltage tolerances causes rotor stalling, winding overheating, and catastrophic failure.

This firmware implements single-cycle (20 ms @ 50 Hz) Q8 fixed-point True-RMS sampling (`isqrt64`), fast 2-sample surge/blackout tripping (~40–60 ms), non-blocking state machine delays, a 3-button resistor-ladder UI, a 4-digit TM1637 display, a GRBL-style bit-banged UART CLI (`9600 bps`), CRC-verified Option-Bytes EEPROM storage, and a 2.0-second hardware Independent Watchdog (IWDG).

---

## 2. Key Engineering Features

- **AC Mains Voltage Protection:**
  - **Fast Surge & Blackout Cutoff:** Trips within 2 consecutive 20 ms AC cycle samples (~40–60 ms) when voltage reaches $\ge 270\text{ V}$ (`Err1` Surge) or drops $\le 140\text{ V}$ (`Err2` Blackout).
  - **Over-Voltage Protection (OVP - `Err3`):** Trips when voltage exceeds `$2` (Default: `250 V`) for the configured delay (`$4`, Default: `5 s`).
  - **Under-Voltage Protection (UVP - `Err4`):** Trips when voltage drops below `$3` (Default: `180 V`) for the configured delay (`$4`, Default: `5 s`).
  - **Recovery Hysteresis (`$5`):** Enforces configurable voltage hysteresis (Default: `10 V`) before transitioning from `STATE_FAULT` back to `STATE_STARTUP_DELAY`.
- **Motor Current & Thermal Protection:**
  - **Locked Rotor Amperage (LRA - `Err5`):** Monitors initial startup window (`$7`, Default: `2 s`). Trips if current exceeds `$6` (Default: `1500 mA`), escalating to permanent `LOC1` lockout after `$8` consecutive failures (Default: `3` retries).
  - **Running Overload Protection (OCP - `Err6`):** Trips after `$10` seconds (Default: `4 s`) if continuous running current exceeds `$9` (Default: `800 mA`).
  - **Under-Current / Dry-Run Protection (UCP - `Err7`):** Optional trip (`$11`, Default: `0` / Disabled) after 60 seconds if running current falls below threshold (`200–1000 mA`).
  - **Over-Run Protection (ORP):** Forces a compressor rest cycle (`STATE_ORP_REST`, default `30 min`) if the compressor runs continuously above minimum threshold (`$12`, Default: `250 mA`) for `$19` hours (Default: `7 h`).
  - **NTC Thermal & Flow Logic (Compile-Time Gated):** Supports compressor shell OTP (`Err8`) and refrigerant flow delta check (`Err9` / `LOC2`) via `NTC1_ENABLED` and `NTC2_ENABLED` in `src/params.h` (disabled by default due to SOP-16 pin mux limits).
- **Signal Processing & Non-Volatile Storage:**
  - **Q8 Sub-LSB True-RMS:** Computes DC-offset-compensated variance over 20 ms (`get_true_rms_q8`) using a 64-bit integer square root (`isqrt64`), achieving $0.1\text{ V}$ and $1\text{ mA}$ resolution with adaptive step-response filtering.
  - **Option-Bytes EEPROM (`eeprom_ob.c`):** Stores 26 bytes (`0xA8` magic header, 22-byte `system_params_t`, and 8-bit rotating XOR CRC) directly in CH32V003 User Option Bytes flash.
- **Dual Configuration Interfaces:**
  - **On-Device Button & TM1637 UI:** Boot-time entry into `SETT` (Settings `$1`–`$20`) or `CAL` (Live telemetry & multiplier calibration `C-1`/`C-2`).
  - **Bit-Banged Serial CLI (`9600 bps`):** Interactive GRBL-style command interface (`$$`, `$id=val`, `read`, `stream`, `defaults`, `run`, `reboot`) over `PC7`/`PC6`.

---

## 3. Hardware Architecture & Pin Mapping

### MCU Pinout (`CH32V003A4M6` SOP-16 — `src/board.h`)

| Macro / Net | Port | SOP-16 Pin | ADC / Function | Direction / Mode | Purpose / Hardware Connection |
|:---|:---:|:---:|:---:|:---:|:---|
| `PIN_TEMP_DRYER` | `PC1` | Pin 1 | Digital Only (No ADC) | Disabled (`NTC2_ENABLED 0`) | Liquid/dryer NTC2 thermistor input |
| `PIN_TEMP_COMP` | `PC2` | Pin 2 | Digital Only (No ADC) | Disabled (`NTC1_ENABLED 0`) | Compressor shell NTC1 thermistor input |
| `PIN_TM_DAT` | `PC3` | Pin 3 | Digital Only | Push-Pull / Pull-Up | TM1637 4-digit display `DIO` |
| `PIN_TM_CLK` | `PC4` | Pin 4 | AIN2 (Unused) | Push-Pull Output | TM1637 4-digit display `CLK` |
| `PIN_DEBUG_RX` / `PIN_CALIBRATION` | `PC6` | Pin 5 | Digital Only | Input Pull-Up | Header H1 Pin 3: CAL jumper at boot (Active LOW) & UART RX |
| `PIN_DEBUG_TX` | `PC7` | Pin 6 | Digital Only | Push-Pull Output | Header H1 Pin 2: Soft-Serial Debug/CLI TX (`9600 bps`) |
| `PIN_SWIO_DEBUG` | `PD1` | Pin 7 | SWIO | Bidirectional | Single-wire debug and firmware flashing interface |
| `PIN_RELAY` | `PD4` | Pin 8 | AIN7 (Unused) | Push-Pull (Active HIGH) | Compressor relay driver |
| `PIN_LED_STATUS` | `PD5` | Pin 9 | AIN5 (Unused) | Push-Pull (Active HIGH) | Status & fault blink-code indicator LED |
| `PIN_BUTTONS` | `PD6` | Pin 10 | **AIN6 (Channel 6)** | Analog Input | 3-button resistor ladder (`+`, `Menu/OK`, `-`) |
| `PD7` / `NRST` | `PD7` | Pin 11 | Reset / GPIO | Unconnected | Unconnected on PCB |
| `PIN_CURRENT_SENS` | `PA1` | Pin 12 | **AIN1 (Channel 1)** | Analog Input | ZMCT103 current transformer signal |
| `PIN_VOLTAGE_SENS` | `PA2` | Pin 13 | **AIN0 (Channel 0)** | Analog Input | ZMPT101 voltage transformer signal |
| `VSS` | `GND` | Pin 14 | Power | Ground | $0\text{ V}$ Ground reference |
| `VDD` | `VDD` | Pin 15 | Power | Supply | $+5.0\text{ V}$ / $+3.3\text{ V}$ logic supply |
| `PIN_BUZZER` | `PC0` | Pin 16 | Digital Only | Push-Pull (Active HIGH) | Active buzzer (or passive PWM @ `2700 Hz` if `BUZZER_PASSIVE 1`) |

### Hardware Silicon Note: NTC Thermistors
On the CH32V003A4M6 SOP-16 package, `PC1` (Pin 1) and `PC2` (Pin 2) do not have internal ADC multiplexer channels. Consequently, `NTC1_ENABLED` and `NTC2_ENABLED` are set to `0` in `src/params.h`, and `sensors_read_temp1()` / `sensors_read_temp2()` return `0` without generating false thermal faults.

---

## 4. Fault Codes & Status Indicators

### Display & LED Blink Codes

| Fault Enum | Code | Display | LED Blink Pattern | Description & Recovery Condition |
|:---|:---:|:---:|:---:|:---|
| `FAULT_NONE` | `0` | Live V / mA | Solid ON (`RUN`) / `500 ms` toggle (`Delay`) | Normal operation (alternates `xxxU` volts and `xxxx` mA every 2 s) |
| `FAULT_SURGE` | `1` | `Err1` | 1 blink / 1.5 s | Fast mains surge ($\ge 270\text{ V}$ for 2 cycles). Recovers at $\le \text{OVP} - \text{Hys}$ |
| `FAULT_BLACKOUT` | `2` | `Err2` | 2 blinks / 1.5 s | Fast mains blackout ($\le 140\text{ V}$ for 2 cycles). Recovers at $\ge \text{UVP} + \text{Hys}$ |
| `FAULT_OVP` | `3` | `Err3` | 3 blinks / 1.5 s | Sustained over-voltage ($> \$2$ for $\$4\text{ s}$). Recovers at $\le \text{OVP} - \text{Hys}$ |
| `FAULT_UVP` | `4` | `Err4` | 4 blinks / 1.5 s | Sustained under-voltage ($< \$3$ for $\$4\text{ s}$). Recovers at $\ge \text{UVP} + \text{Hys}$ |
| `FAULT_LRA` | `5` | `Err5` | 5 blinks / 1.5 s | Locked-rotor overcurrent during startup window ($\$7\text{ s}$) |
| `FAULT_OCP` | `6` | `Err6` | 6 blinks / 1.5 s | Continuous running overcurrent ($> \$9\text{ mA}$ for $\$10\text{ s}$) |
| `FAULT_UCP` | `7` | `Err7` | 7 blinks / 1.5 s | Running undercurrent ($< \$11\text{ mA}$ for $60\text{ s}$) |
| `FAULT_OTP` | `8` | `Err8` | 8 blinks / 1.5 s | Compressor over-temperature (when `NTC1_ENABLED == 1`) |
| `FAULT_REF_FLOW` | `9` | `Err9` | 9 blinks / 1.5 s | Insufficient refrigerant temperature rise (when `NTC2_ENABLED == 1`) |
| `STATE_ORP_REST` | — | Countdown | 10 blinks / 1.5 s | Mandatory rest after continuous run limit ($\$19\text{ h}$) |
| `STATE_LOCKOUT` | — | `LOC1` / `LOC2` | 1 blink (`LRA`) or 5 blinks (`FLOW`) | Permanent lockout after max retries (`LOC1`: LRA, `LOC2`: Flow). Requires power cycle/reboot |

*Audible Alerts:* In `STATE_FAULT`, the buzzer sounds 5 beeps every 30 minutes; in `STATE_LOCKOUT`, it sounds 5 beeps every 15 minutes.

---

## 5. User Interface & Configuration Parameters

### Boot Modes & Button Ladder (`PD6` / `AIN6`)
- **Button 1 (`+` / Next):** ADC `< 240`
- **Button 2 (`Menu / OK`):** ADC `240 – 420` (Hold at power-on to enter **`SETT`** mode; long-press `> 1 s` to confirm value)
- **Button 3 (`-` / Prev):** ADC `420 – 650`
- **Calibration Mode (`CAL`):** Pull `PC6` (Pin 5) LOW to GND at boot to enter **`CAL`** mode (`U-rt` live voltage, `A-rt` live current, `t-rt` temp, `C-1` voltage multiplier, `C-2` current multiplier, `SAUE` save). Note: Relay turns **ON** during `CAL` mode for live load calibration.

### Parameter Reference Table (`$1` – `$22`)

| ID | CLI / UI Key | Description | Range | Step | Default | Unit |
|:---:|:---|:---|:---:|:---:|:---:|:---:|
| **`$1`** | `S-01` | Startup / Restart Delay (`time_delay_mins`) | `2 – 10` | `1` | **`5`** | Minutes |
| **`$2`** | `S-02` | Over-Voltage Protection Limit (`OVP`) | `230 – 260` | `10` | **`250`** | Volts AC |
| **`$3`** | `S-03` | Under-Voltage Protection Limit (`UVP`) | `150 – 200` | `10` | **`180`** | Volts AC |
| **`$4`** | `S-04` | OVP / UVP Trip Delay (`ovp_uvp_delay`) | `3 – 10` | `1` | **`5`** | Seconds |
| **`$5`** | `S-05` | Voltage Recovery Hysteresis (`voltage_hysteresis`) | `10 – 40` | `1` | **`10`** | Volts AC |
| **`$6`** | `S-06` | Locked Rotor Current Limit (`lra_limit_ma`) | `800 – 5000` | `100` | **`1500`** | mA |
| **`$7`** | `S-07` | LRA Startup Detection Window (`lra_time`) | `1 – 5` | `1` | **`2`** | Seconds |
| **`$8`** | `S-08` | LRA Max Retries Before Lockout (`lra_retry_limit`) | `2 – 5` | `1` | **`3`** | Retries |
| **`$9`** | `S-09` | Running Over-Current Limit (`ocp_limit_ma`) | `300 – 4500` | `50` | **`800`** | mA |
| **`$10`** | `S-10` | OCP Trip Delay (`ocp_delay`) | `2 – 10` | `1` | **`4`** | Seconds |
| **`$11`** | `S-11` | Under-Current Limit (`ucp_limit_x10`) | `0` (Off), `200 – 1000` | `50` | **`0`** | mA |
| **`$12`** | `S-12` | Min Current for ORP Timer (`min_current_threshold`) | `0` (Off), `150 – 300` | `50` | **`250`** | mA |
| **`$13`** | `S-13` | NTC1 Enable Status | Locked (`0`) | — | **`0`** | Hardware Lock |
| **`$14`** | `S-14` | Over-Temperature Limit (`otp_limit`) | `70 – 90` | `1` | **`85`** | °C |
| **`$15`** | `S-15` | OTP Recovery Hysteresis (`otp_hysteresis`) | `5 – 20` | `1` | **`10`** | °C |
| **`$16`** | `S-16` | NTC2 Enable Status | Locked (`0`) | — | **`0`** | Hardware Lock |
| **`$17`** | `S-17` | Refrigerant Flow Check Delay (`ref_flow_delay_mins`) | `2 – 10` | `1` | **`5`** | Minutes |
| **`$18`** | `S-18` | Flow Fault Max Retries (`flow_fault_retry_limit`) | `2 – 5` | `1` | **`5`** | Retries |
| **`$19`** | `S-19` | Over-Run Continuous Limit (`orp_time_limit_hours`) | `2 – 12` | `1` | **`7`** | Hours |
| **`$20`** | `S-20` | Over-Run Rest Duration (`orp_rest_delay_mins`) | `20 – 60` | `1` | **`30`** | Minutes |
| **`$21`** | `C-1` | Voltage Calibration Multiplier (`voltage_multiplier_x1000`) | `1 – 5000` | `1` | **`3420`** | Scale ($\times 1000$) |
| **`$22`** | `C-2` | Current Calibration Multiplier (`current_multiplier_x1000`) | `1 – 5000` | `1` | **`1880`** | Scale ($\times 1000$) |

*(In `SETT` mode, `S-21` executes **Save & Reboot** and `S-22` executes **Reset Defaults & Reboot**.)*

---

## 6. Serial CLI Protocol (`9600 bps`, 8N1)

Connect a USB-to-TTL serial adapter (`9600 8N1`) to Header H1 (`PC7` = MCU TX, `PC6` = MCU RX). When not running, the controller outputs live telemetry every 1 second (`TM1637:[...] | MODE:... | ST:... | FLT:... | V:... | A:...`).

Sending any command pauses the relay (`FUN_LOW`), displays `PC` on the TM1637, and enters interactive menu mode:

| Command | Alias | Description |
|:---|:---|:---|
| `menu` | `m` | Pause protection relay and enter interactive CLI menu |
| `-h` | `help`, `?` | Display command list and usage examples |
| `$$` | — | Dump all current parameter values (`$1` through `$22`) |
| `$<id>=<val>` | `$C1=<val>`, `$C2=<val>` | Set parameter value and commit immediately to Option Bytes EEPROM |
| `read` | `status` | Take an immediate single snapshot of AC voltage (`V`) and current (`mA`) |
| `stream` | — | Toggle live 1 Hz telemetry streaming while inside menu mode |
| `defaults` | `$RST=*` | Restore all factory default settings and save to EEPROM |
| `run` | `exit` | Exit menu mode and resume protection (starts in `STATE_STARTUP_DELAY`) |
| `reboot` | — | Trigger an immediate hardware watchdog reset |

---

## 7. Build & Flashing Instructions (PlatformIO)

The project is configured in `platformio.ini` for `genericCH32V003A4M6` using `framework = ch32v003fun` and `minichlink` with `-Os -flto` optimization flags.

### Build Firmware
```bash
pio run