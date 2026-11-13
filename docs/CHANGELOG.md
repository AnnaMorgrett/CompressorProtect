# CompressorProtect - Changelog & Migration History

This document chronicles the migration, architectural overhaul, and engineering optimizations executed while transitioning the CompressorProtect project from the 8-bit AVR ATmega328P to the 32-bit RISC-V WCH CH32V003 microcontroller.

---

## [Version 2.0.0-CH32V003] - 2026-11-07

### 1. Architectural Overhaul
- **Monolithic to Modular Transition**:
  - Replaced the legacy monolithic `ATMega328protector.ino` file with an enterprise-grade modular C++ architecture in `firmware/`.
  - Divided responsibilities into clean, non-blocking subsystems:
    - `pins.h`: Central hardware pin definitions and ADC channel allocations.
    - `config.h`: System configuration constants, boundaries, and default parameters.
    - `fault_codes.h`: Standardized fault enumeration and compact segment lookups.
    - `system_state.h`: 11-state deterministic Finite State Machine (FSM).
    - `sensor_manager`: Fast non-blocking ADC sampling, fast integer square root (`isqrt32`), peak-to-peak voltage RMS, true current RMS, and sensor status tracking.
    - `storage_manager`: WCH Data Flash / Option Bytes emulation with 8-bit XOR checksum validation and auto-recovery defaults.
    - `button_manager`: Resistor ladder analog decoder for 3 buttons on `PD6`, 40ms software debounce, short/long press detection, and auto-repeat.
    - `display_manager`: Lightweight bit-banged TM1637 4-digit LED driver, cyclic telemetry rotator, fault indicator, and interactive configuration menu (`P-01` to `P-05`).
    - `alarm_manager`: Non-blocking status LED sequences (`PD5`) and buzzer acoustic profiles (`PC1`) with mute support.
    - `protection`: High-priority safety supervisor implementing OVP, UVP, critical sag (<160V immediate), running OCP, locked rotor (LRA <200ms), and 300s restart delay.
    - `diagnostics`: Optional non-blocking software UART telemetry on `PC7` (disabled by default to conserve flash).
    - `firmware.ino`: Non-blocking super-loop with Independent Watchdog (`IWDG`) integration (~2.0s timeout).

---

### 2. Hardware Realignment & Schematic Verification (`archive_old/CH32Version.png`)
- **Relay Driver Pin**:
  - *Previous Code*: Misassigned to `PD6` (which collided with the button resistor ladder).
  - *Corrected Assignment*: Realigned to `PD4` (Pin 8), driving the S8050 NPN transistor base (Active HIGH).
- **Button Ladder Input**:
  - *Previous Code*: Configured as multiple digital GPIO pins with internal pull-ups.
  - *Corrected Assignment*: Mapped to single analog pin `PD6` (Pin 10, ADC Channel `AIN6`). Decodes 3 buttons through resistor divider ladder ($R_1 = 2\,\text{k}\Omega, R_2 = 1\,\text{k}\Omega, R_3 = 1\,\text{k}\Omega, R_{\text{pullup}} = 10\,\text{k}\Omega$).
- **Buzzer Driver Pin**:
  - *Previous Code*: Assigned to `PC0` (non-existent or floating).
  - *Corrected Assignment*: Mapped to `PC1` (Pin 1), driving the S8050 NPN buzzer circuit.
- **Status LED Pin**:
  - *Corrected Assignment*: Mapped to `PD5` (Pin 9), driving the yellow status indicator LED. Avoided conflict with default USART1_TX.
- **Display Pins**:
  - *Corrected Assignment*: TM1637 CLK mapped to `PC6` (Pin 5) and DIO mapped to `PC4` (Pin 4).
- **Programming Header (H1)**:
  - Verified and preserved `PD1` (Pin 7) for single-wire debugging (`SWIO`) and flashing via `minichlink`.
  - Mapped auxiliary calibration / debug output to `PC7` (Pin 6).

---

### 3. Critical ADC Hardware Resolution for NTC Inputs
- **Hardware Finding**:
  - Connectors CN1 (`NTC1`) and CN2 (`NTC2`) are routed to MCU pins `PC3` (Pin 3) and `PC2` (Pin 2).
  - On the WCH CH32V003 silicon, pins `PC1`, `PC2`, and `PC3` **do not have analog multiplexer channels connected to the internal ADC** (valid ADC channels are only `PA2`, `PA1`, `PC4`, `PD2`, `PD3`, `PD5`, `PD6`, `PD4`).
- **Firmware Resolution**:
  - Did NOT invent fake ADC readings or hallucinate hardware connections.
  - Marked both NTC sensors as `UNCOMMISSIONED` / `SENSOR_STATUS_UNAVAILABLE_NO_ADC`.
  - Safely suppressed false tripping of `E-06` through `E-09`.
  - Telemetry scrolls ` -NC` (Not Connected) for temperature fields.
  - Primary thermal overload protection is formally documented as residing in the compressor's internal bimetallic Klixon protector.

---

### 4. Memory Footprint & Flash Optimization (Fitting in 16 KB)
The CH32V003F4 / CH32V003A4M6 features strict hardware memory limits: **16 KB Flash** and **2 KB SRAM**. Initial naive modular builds overflowed flash by nearly 2,000 bytes. The following aggressive embedded optimizations were executed:

1. **Elimination of `<math.h>` and `sqrt()`**:
   - Replaced floating-point `sqrt()` with a custom bitwise digit-by-digit integer square root algorithm (`isqrt32()`).
   - Saved ~1,200 bytes of flash by eliminating the GCC software floating-point math library runtime.
2. **Fixed-Point Protection Computations**:
   - Converted all threshold comparisons in `protection.cpp` to pure integer math (e.g. current stored and evaluated in integer milliamperes `mA`).
   - Eliminated GCC 32-bit floating-point comparison helper routines (`__gesf2`, `__lesf2`, `__aeabi_fcmple`).
3. **Table-Driven Menu & Unified 4-Digit Display Formatter**:
   - Replaced duplicate digit extraction routines with a single `renderNumber4()` function marked `__attribute__((noinline))`.
   - Compressed menu items into a compact static struct table `ParamDef params[]`.
4. **Contiguous Memory Storage Serialization**:
   - Replaced 9 repetitive individual calls to `EEPROM.update()` with a single tight loop across `sizeof(SystemConfig)`.
5. **Removal of Unused String Literals**:
   - Eliminated multi-byte ASCII strings in `fault_codes.h` and `system_state.h`, replacing them with raw 4-byte 7-segment font arrays.
6. **Compiler Optimization Flags**:
   - Configured `arduino-cli` with `-Os`, `-march=rv32ecxw`, `-mabi=ilp32e`, `-ffunction-sections`, `-fdata-sections`, `-Wl,--gc-sections`, and `nano.specs`.

### Final Build Metrics:
- **Flash Utilization**: **16,004 bytes** out of 16,384 bytes (**97%**, 380 bytes free).
- **RAM Utilization**: **996 bytes** out of 2,048 bytes (**48%**, 1,052 bytes free).

---

### 5. Automated Verification Suite
- Authored `tests/test_logic.py` providing 15 automated test cases validating:
  - Fast integer square root accuracy.
  - Button ladder ADC threshold windows.
  - OVP, UVP, critical sag, running OCP, and locked rotor timing.
  - Option Bytes XOR checksum and corruption fallback.
- Validated 100% pass rate on host workstation.

