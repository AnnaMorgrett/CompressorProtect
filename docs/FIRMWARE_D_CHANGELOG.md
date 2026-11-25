# Engineering Changelog: Firmware_D Architecture & Specifications

**Document:** `docs/FIRMWARE_D_CHANGELOG.md`  
**Target:** `firmware_D` (WCH CH32V003A4M6)  

---

## [1.1.0] — 2026-11-24: Verbatim Original Master Flowchart Integration
### Added
- Integrated the original, unabridged 4-part Master System Flowchart and 10-File Balanced Module Architecture verbatim into [`docs/FIRMWARE_D_ORIGINAL_BASELINE.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_ORIGINAL_BASELINE.md).
- Added Section 6 to [`docs/FIRMWARE_D_ORIGINAL_BASELINE.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_ORIGINAL_BASELINE.md) providing an explicit, comparative domain audit contrasting early draft assumptions (PD0 relay, PD4 buzzer, PD7 NRST remapping, 2-step RC key scan on PC1, two-pass SRAM RMS) with physical EasyEDA V1.0 hardware.
- Expanded [`docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md) to 19 formal requirements, tracking REQ-18 (PD7 NRST unlock) and REQ-19 (PC1 RC decay timing) as OBSOLETE.
- Synced all documentation files across both `C:\Users\AVS\Documents\CompressorProtect\docs` and `C:\Users\AVS\Documents\PlatformIO\Projects\CompressorProtector\docs`.

---

## [1.0.0] — 2026-11-24: Baseline Establishment & Specification Harmonization
### Added
- Created permanent workspace instruction rule `AGENTS.md` enforcing 15 mandatory engineering invariants for future development sessions.
- Authored canonical living specification [`docs/FIRMWARE_D_MASTER_SPEC.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_MASTER_SPEC.md) documenting verified EasyEDA V1.0 hardware, 10-module architecture, true-RMS mathematics, FSM rules, and memory budgets.
- Authored historical reference [`docs/FIRMWARE_D_ORIGINAL_BASELINE.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_ORIGINAL_BASELINE.md) preserving original design intent and documenting superseded assumptions.
- Authored formal traceability matrix [`docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md) classifying system requirements across VALID, DEFECT, OBSOLETE, MISMATCH, PARTIAL, and UNVERIFIED states.
- Authored rigorous audit of the proposed DSP redesign specification (`firmware_D_dsp_redesign_spec.md`), analyzing 14 critical mathematical and physical claims.

### Audited / Verified
- **Commit `d98e879` Verified:** Inspected all 10 source files (`config.h`, `system.h`, `hal_sys.c`, `nvm_flash.c`, `adc_dsp.c`, `display_tm1637.c`, `input_alerts.c`, `fsm_protect.c`, `menu_cal.c`, `main.c`).
- **PlatformIO Compilation:** Verified clean compilation on `ch32v003fun` toolchain (8,928 B Flash / 54.5%, 1,308 B SRAM / 63.9%, 740 B free SRAM).
- **Physical Pin Mapping Confirmed:** Confirmed EasyEDA V1.0 routing: Buzzer on PC0, TM1637 DIO on PC3, TM1637 CLK on PC4, CAL on PC6, Relay on PD4, LED on PD5, KEY on PD6, VAC on PA2, CT on PA1. Historical pin assignments (PC1 Buzzer, PC4 DIO, PC6 CLK) marked OBSOLETE.

### Defect Identified (Pre-existing in Current Source)
- **Pre-Smoothing Protection Delay (`src/adc_dsp.c:120-135`):** `vac_raw_dv` and `i_raw_ca` are generated from a 4-block moving power average (`RMS_AVG_BLOCKS = 4`, 80 ms lag) before being evaluated by `fsm_on_block()`. A step voltage drop from 220V to 140V requires 3 blocks (60 ms) before crossing the 180V threshold, delaying fault clearing.
- **Analog Voltage Under-Gain (`src/config.h:60-64`):** Voltage divider ($820\text{ k}\Omega / 100\ \Omega$) delivers only $\approx 5.5\text{ ADC counts RMS}$ at 220V, with only $1.0\text{ count}$ separating 220V from the 180V undervoltage limit. BOM retuning ($R_{14} \to 220\text{ k}\Omega, R_8 \to 680\ \Omega$) specified to increase resolution by 2500% (25 counts delta).

---

## [Historical Evolution: Pre-Firmware_D]
### firmware_a
- Initial Arduino-core prototype with basic threshold comparisons and delay loops.
### firmware_b
- Introduction of 3-button ladder decoding, EEPROM emulation attempt via Option Bytes.
### firmware_c
- State machine structuring, initial peak-to-peak estimation, floating-point math removal.
### firmware_d (Initial Bare-Metal Release — Commit `1866d11` to `d98e879`)
- Complete migration from Arduino framework to bare-metal `ch32v003fun` on PlatformIO.
- TIM1 TRGO hardware-timed ADC scanning at 6.4 kS/s per channel into DMA1 circular ping-pong buffer.
- Single-pass DC-offset-compensated variance calculation in Q12 fixed-point.
- Fast-Page programming on top 64-byte Flash page with CRC16 data validation.
- 4-digit TM1637 bit-bang driver, keypad debouncing, SysTick non-blocking alert scheduling.
