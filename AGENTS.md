# AGENTS.md — Permanent Engineering Rules for CompressorProtect firmware_D

> **Mandatory Workspace Directive:** These rules apply to all AI agents, engineers, and automated tools working on the `AnnaMorgrett/CompressorProtect` project (`firmware_D` target). These rules are permanently active and strictly enforced.

---

## 1. Scope & Focus
1. **Exclusive Target:** Work exclusively on `CompressorProtect firmware_D` (WCH CH32V003A4M6, SOP16, bare-metal `ch32v003fun` on PlatformIO) unless explicitly instructed otherwise.
2. **Authoritative Specification:** Read [`docs/FIRMWARE_D_MASTER_SPEC.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_MASTER_SPEC.md) before designing or making any firmware architecture or behavioral changes.
3. **Historical vs. Living Code:** Treat the original Master System Flowchart as historical design intent, not a literal description of the current implementation.
4. **Physical Reality Rule:** Verify all proposed changes against actual current source code and confirmed physical board hardware.
5. **No Obsolete Regression:** Never restore obsolete GPIO assignments, disproven hardware assumptions (such as ADC on PC2/PC3), or uncalibrated algorithms simply because they appear in historical documents or early firmware revisions (firmware_a/b/c).
6. **Preserve Modular Architecture:** Preserve the verified 10-module architecture (`config.h`, `system.h`, `hal_sys.c`, `nvm_flash.c`, `adc_dsp.c`, `display_tm1637.c`, `input_alerts.c`, `fsm_protect.c`, `menu_cal.c`, `main.c`) where practical.

---

## 2. Safety & Protection Invariants
7. **Fail-Safe Relay Supremacy:** The compressor relay defaults to de-energized (`LOW` on PD4) at power-on, reset, watchdog trigger, or any fault state. Protection trip decisions take absolute precedence over display updates, keypad handling, and telemetry.
8. **Deterministic Acquisition:** Keep ADC acquisition strictly deterministic (hardware-timer triggered scan via DMA ping-pong buffering). Never introduce blocking loops or variable-delay execution in the sampling path.
9. **Total True-RMS Integrity:** Inductive motor protection requires total true-RMS (including harmonic energy that heats windings), not fundamental-only estimators or rectified absolute approximations.
10. **Protection Decoupling:** Keep protective trip processing completely independent from visual display smoothing. Never insert moving-average display filters into the trip detection path.
11. **Strict Calibration & Threshold Integrity:** Never make unverified changes to sensor turns ratios, burden resistor values, calibration math, Flash storage layouts, or user protection thresholds.

---

## 3. Engineering Rigor & Discipline
12. **Verification Honesty:** Do not claim a build, simulation, or hardware validation succeeded unless it was executed and verified with actual toolchain output. Clearly distinguish analytical estimates from in-circuit bench measurements.
13. **Documentation Currency:** Every important architectural change must update [`docs/FIRMWARE_D_MASTER_SPEC.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_MASTER_SPEC.md), [`docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md), and [`docs/FIRMWARE_D_CHANGELOG.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_CHANGELOG.md).
14. **Audit Before Modifying:** Audit first, mathematically prove and explain the proposed change, and implement only when fully justified and authorized.
15. **No Unrelated Refactoring:** Do not perform gratuitous or stylistic refactoring. Keep pull requests and changesets tightly focused on validated requirements.
