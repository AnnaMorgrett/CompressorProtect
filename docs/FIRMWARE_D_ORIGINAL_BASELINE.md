# Historical Design Baseline: Firmware_D Original Specification

**Document:** `docs/FIRMWARE_D_ORIGINAL_BASELINE.md`  
**Status:** Canonical Historical Reference Baseline (Preserved Verbatim)  
**Target:** WCH CH32V003 (SOP16 Package)  

---

> [!IMPORTANT]
> **Historical Design Intent Notice:**
> The content in Section 1 through Section 5 below is the **original, unabridged Master System Flowchart and 10-File Architecture** preserved **verbatim** exactly as authored.
> It serves as the authoritative baseline for original design intent, timing constants, state machines, and parameter ranges.
> 
> As documented in [`docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_REQUIREMENTS_TRACEABILITY.md) and [`docs/FIRMWARE_D_MASTER_SPEC.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_MASTER_SPEC.md), certain hardware pin mappings in this early design draft (such as PD0 Relay, PD4 Buzzer, PD7 NRST unlock, and PC1 RC button scanner) were superseded by the confirmed physical **EasyEDA V1.0 SOP16 schematic** (PD4 Relay, PC0 Buzzer, PD6 ADC button ladder, PA2 VAC, PA1 CT, PC3 DIO, PC4 CLK, PC6 CAL).

---

## 1. 10-File Module Map (Balanced Split — Never Exceeding 10 Files)
*(Excludes rarely edited build files platformio.ini and funconfig.h)*

* **[#01] `config.h`** — All GPIO #defines (PC4, PD0, PD4, PA1, PD7, PC1, PA2, PD1, unused pull-ups), clock/ADC constants (48 MHz SYSCLK, 12 MHz ADCCLK, 6,400 Hz TIM1), feature switches (ENABLE_CT, ENABLE_NTC, ENABLE_SWIO_LOG), and Indonesian 220V/50Hz defaults.
* **[#02] `system.h`** — Unified structs (SystemConfig_t with P01–P09 + reserved P10–P11 & calibration/fault fields, SensorData_t, FaultCode_t, AppMode_t, KeyEvent_t) and all inter-module prototypes.
* **[#03] `hal_sys.c`** — Fail-safe GPIO (PD0/PD4), Option Byte PD7 NRST unlock, 24 MHz HSI × PLL 2 = 48 MHz clock (HSE off, HPE off), free-running 32-bit SysTick (STRE = 0, 1 ms IRQ), 128 kHz LSI IWDG, unused pin pull-ups, and optional SWIO debug telemetry.
* **[#04] `nvm_flash.c`** — Top 64-byte Flash page (0x08003FC0) read/erase/program with CRC16, BSY polling + IWDG feed, factory defaults, and OC_Fail_Count (LOC3) persistence.
* **[#05] `adc_dsp.c`** — TIM1 (6,400 Hz TRGO) $\to$ ADC1 (12 MHz, Ts = 43 cycles for R_AIN = 6 kΩ) $\to$ DMA1 Ch1 circular ping-pong buffer (2 × 128), SRAM .srodata two-pass True-RMS (isqrt64), live VREFINT (1.20V) +5V rail compensation, EMA ($\alpha = 1/8$, snap > 2.0V) + 0.3V Schmitt deadband display filter, plus #if ENABLE_CT and #if ENABLE_NTC channels.
* **[#06] `display_tm1637.c`** — TM1637 bit-bang driver (PA1 CLK, PD7 DIO), full 7-segment alphanumeric glyph table (E-UV, E-OV, E-OC, E-Ot, LOC3, Sett, C-0, C-V, C-A, SAVE, donE), MM:SS countdown & 0.1V/0.01A formatters.
* **[#07] `input_alerts.c`** — 2-step RC-Schmitt SysTick->CNT button scanner (PC1), debounce, > 0.5s fast auto-repeat, 3s long-press (LOC3 unlock), PA2 (CAL) reader, non-blocking LED blink & active buzzer (PD4) sequencer.
* **[#08] `fsm_protect.c`** — Mode 1 (Normal Protection Run): 05:00 safety delay with hysteresis hold/reset, running monitor, 20.0 ms raw 1-cycle OV/UV cutoff, #if ENABLE_CT (Inrush, OC, LOC3, 3 min healthy auto-clear), and #if ENABLE_NTC (OT).
* **[#09] `menu_cal.c`** — Mode 2 (Sett editor P01–P09 + reserved P10–P11, rSt, SAVE) and Mode 3 (CAL wizard C-0, C-V, #if ENABLE_CT C-A, SAVE & safe halt).
* **[#10] `main.c`** — System orchestrator: initializes modules, routes boot priority (CAL $\to$ Sett $\to$ Normal), runs main superloop & IWDG supervision.

---

## 2. Part 1: Master Boot & Hardware Initialization Flowchart
This startup sequence enforces fail-safe relay control, unlocks PD7 (TM1637 DIO) from hardware NRST, configures ch32fun clock/interrupt safety rules, and routes into one of the three operating modes:

```
========================================================================
                         [ POWER ON / RESET ]
                        [#10] main.c :: main()
========================================================================
                                  |
                                  v
+----------------------------------------------------------------------+
| 1. FAIL-SAFE GPIO & OPTION BYTE CHECK        [#03] hal_sys.c         |
|  - Force Relay (PD0) = LOW (OFF) & Buzzer (PD4) = LOW (OFF)          |
|    using hardware pin maps from [#01] config.h                       |
|  - Read Option Bytes (0x1FFFF800): Is PD7 still configured as NRST?  |
|    * YES -> Unlock OB, clear RST_MODE (PD7 = GPIO), wait Flash BSY,  |
|             and trigger NVIC_SystemReset()                           |
+----------------------------------------------------------------------+
                                  | (PD7 confirmed as GPIO)
                                  v
+----------------------------------------------------------------------+
| 2. CH32FUN CORE, CLOCK & WATCHDOG INIT       [#03] hal_sys.c         |
|  - Clock: HSI 24MHz * PLL2 = 48MHz SYSCLK (HSE OFF, SWIO PD1 active) |
|  - Interrupts: Keep HPE disabled (0x804 = 0) so s0/s1 stay safe      |
|  - SysTick: 48MHz HCLK, STRE = 0 (free-running CNT for Delay_Us),    |
|             advance CMP by 48,000 ticks every 1.0 ms                 |
|  - IWDG: Start Independent Watchdog on 128 kHz LSI oscillator        |
|  - Pull-Ups: Enable internal pull-ups on PA2 (CAL) & unused pins     |
|              (PC0, PC3, PC6, PD5, PD6) to prevent floating inputs    |
+----------------------------------------------------------------------+
                                  |
                                  v
+----------------------------------------------------------------------+
| 3. ZERO-JITTER ADC + DMA & NON-VOLATILE FLASH LOAD                   |
|  - [#05] adc_dsp.c:                                                  |
|    * Configure ADC1: ADCCLK = 12MHz, Ts = 43 cycles (R_AIN = 6 kOhm) |
|    * Sample internal VREFINT (1.20V) -> compute live +5V rail factor |
|    * Start TIM1 (6,400 Hz TRGO) -> ADC1 (PC4/A2) -> DMA1 Ch1 Circular|
|      Ping-Pong Buffer (2 x 128 half-words = 512 B SRAM)              |
|  - [#04] nvm_flash.c -> populates SystemConfig_t ([#02] system.h):   |
|    * Read Top Flash Page (0x08003FC0) & verify CRC16:                |
|      - Valid   -> Load P01..P09 (+ P10..P11), VAC Cal & OC_Fail_Count|
|      - Invalid -> Load Indonesian 220V/50Hz Factory Defaults & Save  |
|  - [#06] display_tm1637.c & [#07] input_alerts.c: Init TM1637 & keys |
+----------------------------------------------------------------------+
                                  |
                                  v
                     /-------------------------\
                    /  Is CAL Pin (PA2) == GND? \--- YES --> [ MODE 3 ]
                   /     [#07] input_alerts.c    \           [#09] menu_cal.c
                   \     (Highest Priority)      /           (CALIBRATION)
                    \---------------------------/
                                  | NO
                                  v
                     /-------------------------\
                    / Is "Menu/OK" Held at Boot?\--- YES --> [ MODE 2 ]
                   /  (2-Step PC1 RC Scanner)    \           [#09] menu_cal.c
                   \     [#07] input_alerts.c    /           (SETTINGS)
                    \---------------------------/
                                  | NO
                                  v
                  =================================
                  [ MODE 1: NORMAL PROTECTION RUN ]
                         [#08] fsm_protect.c
                  =================================
```

* **Framework Compliance:** HSI stays enabled so SWIO (PD1) remains permanently accessible, HPE stays off to avoid s0/s1 ISR register corruption, SysTick->CNT runs continuously (STRE = 0) to preserve Delay_Us()/Delay_Ms(), and all unused pins use internal pull-ups to eliminate floating Schmitt-trigger leakage.

---

## 3. Part 2: Parallel Real-Time Interrupt & SRAM DSP Pipeline
While the main superloop handles state transitions, TM1637 updates, and IWDG kicks, two deterministic hardware pipelines run concurrently in the background:

```
+-----------------------------------+  +-------------------------------+
| HARDWARE TIM1 -> ADC1 -> DMA1 CH1 |  | SYSTICK_HANDLER (1.0 ms IRQ)  |
| (6,400 Hz = 128 samples / 20 ms)  |  | [#03] hal_sys.c               |
| [#05] adc_dsp.c                   |  | (__attribute__((interrupt)))  |
+-----------------------------------+  +-------------------------------+
                  |                                    |
   (Every 128 samples = 20.0 ms)         (Every 1.0 ms @ 48 MHz HCLK)
                  v                                    v
+-----------------------------------+  +-------------------------------+
| DMA1_Channel1_IRQHandler          |  | 1. Advance SysTick->CMP +=    |
| [#05] adc_dsp.c                   |  |    48,000 (keeps CNT free)    |
| Placed in SRAM (.srodata) for     |  |    [#03] hal_sys.c            |
| 0-wait 1-CPI execution (479 ns):  |  | 2. Increment ms_ticks [#03]   |
|  1. Pass 1: Mean x_bar = Sum >> 7 |  | 3. Every 10 ms [#07]          |
|     (removes 2.50V DC bias drift) |  |    input_alerts.c: 2-Step RC- |
|  2. Pass 2: S = Sum((x - x_bar)^2)|  |    Schmitt Scan on PC1 (KEY)  |
|  3. isqrt64(S >> 7) * Cal_Gain    |  |    using SysTick->CNT ticks:  |
|     -> Raw 1-Cycle True-RMS VAC   |  |    * Phase 1: Charge 50us ->  |
|  4. Refresh live VREFINT ratio    |  |      float 400us -> '+' = 0   |
|  5. [#if ENABLE_CT / ENABLE_NTC]: |  |    * Phase 2: Discharge 50us  |
|     Sample CT / NTC1 / NTC2       |  |      -> float & time to 1:    |
+-----------------------------------+  |      - Timeout 1.2ms = OK     |
         |                  |          |      - ~870us (41.7k) = '-'   |
   [FAST PATH]        [STABLE PATH]    |      - ~530us (25.4k) = Idle  |
  (20.0 ms Trip)     (100% No Flicker) | 4. Update 1s countdown [#08], |
         |                  |          |    LED blink & beep [#07]     |
         v                  v          +-------------------------------+
+-----------------+ +---------------+                  |
| [#08]           | | [#05]         |                  v
| fsm_protect.c:  | | adc_dsp.c:    |  +-------------------------------+
| If Relay == ON  | | EMA (a = 1/8, |  | MAIN SUPERLOOP [#10] main.c   |
| & (VAC < UV or  | | snap > 2.0V)  |  | - Process Button Events [#07] |
|    VAC > OV):   | | + 0.3V Schmitt|  | - Execute Active Mode FSM     |
| -> [#03]        | | Deadband ->   |  |   ([#08] or [#09])            |
|    hal_sys.c    | | SensorData_t  |  | - Refresh TM1637 (PA1 / PD7)  |
|    PD0 = LOW    | | ([#02]) ->    |  |   [#06] display_tm1637.c      |
|    IMMEDIATELY! | | TM1637 Buffer |  | - [#if ENABLE_SWIO_LOG]:      |
+-----------------+ +---------------+  |   Debug Printf ([#03])        |
                                       | - Kick IWDG (128 kHz LSI)     |
                                       |   [#03] hal_sys.c             |
                                       +-------------------------------+
```

* **Latency & Resolution Architecture:** Following ch32fun's fixed-sample-rate DSP design (`adc_fixed_fs`) and SRAM execution placement (`.srodata`), each 20.0 ms AC cycle yields ~13.5-bit effective True-RMS resolution (< 0.05V raw step) with < 20.5 ms fault cutoff latency and zero TM1637 digit jitter.

---

## 4. Part 3: Mode 1: Normal Protection State Machine ([#08] fsm_protect.c)

```
                =================================
                [ MODE 1: NORMAL PROTECTION RUN ]
                       [#08] fsm_protect.c
                =================================
                                |
                                v
                   /-------------------------\
                  / ENABLE_CT &&              \--- YES ---> [ STATE D: LOCKOUT ]
                  \ OC_Fail_Count >= P09 (3)? /              - [#06] Display: "LOC3"
                   \-------------------------/               - [#03] Relay PD0 = LOW
                                | NO                         - [#07] LED: Double-Flash
                                v                            - [#07] Buzzer: Alert Beep
+--------------------------------------------------------+   - [#07] Unlock: Hold OK 3s
| STATE A: 5-MINUTE SAFETY DELAY ("05:00" Countdown)     |     -> [#04] Reset FailCount
|  - [#03] Relay PD0 = LOW | [#07] LED = Slow Blink (1 Hz)|        in Flash -> State A
|  - Check Raw 20ms True-RMS VAC ([#05]) every cycle:    |
|    * Is VAC < (UV + HYS) [190V] OR > (OV - HYS) [235V]?|
|      - YES: Reset timer to P08 ("05:00"), [#06]        |
|             alternate "E-UV" or "E-OV" <-> "05:00"     |
|      - NO : Decrement timer every 1.0s toward "00:00"  |
+--------------------------------------------------------+
                                | (Timer == 00:00 & VAC in 190V..235V)
                                v
+--------------------------------------------------------+
| STATE B: COMPRESSOR RUNNING (Relay PD0 = HIGH)         |
|  - [#03] Energize Relay PD0 = HIGH | [#07] LED Solid ON|
|  - [#07] Buzzer: 1x 150ms confirmation beep, then silent|
|  - [#06] Display: Stabilized VAC ([#05]) (or alternate |
|          VAC <-> Amps every 2.0s if ENABLE_CT == 1)    |
|  - Continuous 20.0ms Raw Protection Watch ([#05]):     |
|    1. VAC < UV (180V) or VAC > OV (245V)? -> STATE C1  |
|    2. [If ENABLE_CT == 1] Current > Inrush/OC? -> C2   |
|    3. [If ENABLE_NTC == 1] Temp > P10 (OT)? -> C2      |
|    4. [If ENABLE_CT == 1] Healthy > 3 min -> [#04]     |
|       FailCnt = 0                                      |
+--------------------------------------------------------+
           |                                   |
  (Voltage Fault in 20ms)             (Over-Current / Over-Temp)
           |                                   |
           v                                   v
+----------------------------------+ +----------------------------------+
| STATE C1: VOLTAGE FAULT TRIP     | | STATE C2: OVER-CURRENT / OT TRIP |
|  - [#03] Relay PD0 = LOW IMMEDIATELY| - [#03] Relay PD0 = LOW IMMEDIATELY|
|  - [#07] LED = Fast Blink (4 Hz) | |  - [#07] LED = Fast Blink (4 Hz) |
|  - [#07] Buzzer = 3x Short Alert | |  - [#07] Buzzer = 3x Short Alert |
|    Beeps                         | |    Beeps                         |
|  - OC_Fail_Count unchanged       | |  - [#04] OC_Fail_Count++ -> Save |
+----------------------------------+ |    Flash                         |
           |                         +----------------------------------+
           |                                   |
           |                         /-------------------\
           |                        / OC_Fail_Count >= P09\-- YES -> [STATE D]
           |                        \         (3)?        /
           |                         \-------------------/
           |                                   | NO
           +---------------->+-----------------+
                             |
                             v
                   [ Return to STATE A ]
```

---

## 5. Part 4: Mode 2 (Sett) & Mode 3 (CAL) Sub-Flowcharts ([#09] menu_cal.c)
Both configuration modes keep the Relay locked OFF ([#03] `hal_sys.c`) during editing and commit to the top 64-byte Flash page (0x08003FC0) via [#04] `nvm_flash.c` only upon explicit confirmation on SAVE:

```
+-----------------------------------+  +-----------------------------------+
| MODE 2: BOOT SETTINGS ("Sett")    |  | MODE 3: CALIBRATION ("CAL"==GND)  |
| [#09] menu_cal.c (Relay OFF [#03])|  | [#09] menu_cal.c (Relay OFF [#03])|
+-----------------------------------+  +-----------------------------------+
                 |                                      |
                 v                                      v
+-----------------------------------+  +-----------------------------------+
| 1. [#06] Display "Sett"           |  | 1. Step "C-0": Zero Offset        |
|    (Relay = OFF [#03])            |  |    - Ensure 0V AC / 0A input      |
|    [#07] Wait for user to release |  |    - [#07] Press OK -> [#05]      |
|    & press Menu/OK                |  |      capture 256-pt baseline &    |
+-----------------------------------+  |      advance to C-V               |
                 |                     +-----------------------------------+
                 v                                      |
+-----------------------------------+                   v
| 2. Parameter Loop (P01 .. SAVE):  |  +-----------------------------------+
|    - [#06] Display code ("P01")   |  | 2. Step "C-V": VAC Gain Cal       |
|      <-> current value ("245")    |  |    - [#06] Display live True-RMS  |
|    - [#07] '-' / '+' adjusts value|  |      (0.1V) from [#05] adc_dsp.c  |
|      (1 step click, fast > 0.5s)  |  |    - [#07] '-' / '+' tunes        |
|    - [#07] 'OK' advances next:    |  |      Cal_Gain_V to ref meter      |
|      P01: OV   (245V, 230..270V)  |  |    - [#07] Press OK -> advance    |
|      P02: UV   (180V, 140..210V)  |  +-----------------------------------+
|      P03: HYS  (10V,  2..20V)     |                   |
|      P04: OC   (1.40A, 0.50..10A) |        [If ENABLE_CT == 1: Step C-A]
|      P05: INR  (2.5x, 1.5..5.0x)  |        ([#03] Relay ON w/ safety cutoff,
|      P06: t-IN (1.5s, 0.5..5.0s)  |         [#05]/[#06]/[#07] tune
|      P07: t-OC (0.5s, 0.1..5.0s)  |         Cal_Gain_A to 0.01A)
|      P08: dEL  (05:00, 01..10m)   |                   |
|      P09: rEt  (3,    1..5 trips) |                   v
|      [P10/P11 if ENABLE_NTC == 1] |  +-----------------------------------+
|      rSt: Press '+' -> [#04]      |  | 3. Step "SAVE": Commit to Flash   |
|           Factory Rst             |  |    - [#07] Press OK -> [#04]      |
|      SAVE: Press OK to commit     |  |      nvm_flash.c Erase & Program  |
+-----------------------------------+  |      0x08003FC0 w/ CRC16 (poll    |
                 |                     |      Flash BSY, kick IWDG [#03])  |
                 v                     |    - [#06] Display "donE"         |
+-----------------------------------+  |      (Relay = OFF [#03])          |
| 3. [#04] Write Flash 0x08003FC0   |  |    - Halt until CAL removed &     |
|    (CRC16, poll BSY, kick IWDG)   |  |      power unplugged/replugged    |
|    -> [#06] Display "donE"        |  +-----------------------------------+
|       (Relay OFF [#03])           |
|    -> Halt until power re-plugged |
+-----------------------------------+
```

---

## 6. Engineering Comparative Audit: Baseline Intent vs. Verified Physical Implementation

The table below preserves the exact design intent from the original baseline above, while explicitly recording the physical and electrical realities verified in the current production codebase:

| Architecture Domain | Original Baseline Intent (Above) | Current Verified Implementation ([`docs/FIRMWARE_D_MASTER_SPEC.md`](file:///c:/Users/AVS/Documents/CompressorProtect/docs/FIRMWARE_D_MASTER_SPEC.md)) | Justification / Physical Evidence |
|:---|:---|:---|:---|
| **Compressor Relay** | Net `PD0` | **Net `PD4`** | Verified EasyEDA V1.0 SOP16 schematic routes relay driver Q2 base to PD4. (PD0 does not exist on SOP16). |
| **Piezo Buzzer** | Net `PD4` | **Net `PC0`** | Verified EasyEDA V1.0 SOP16 schematic routes buzzer driver Q1 base to PC0. |
| **TM1637 Display** | PA1 (CLK), PD7 (DIO w/ NRST unlock) | **PC4 (CLK), PC3 (DIO)** | EasyEDA V1.0 routes TM1637 directly to Port C push-pull lines PC3 & PC4. Eliminates risky NRST remapping on PD7. |
| **Keypad Input** | 2-step RC-Schmitt decay on PC1 | **Analog Resistor Ladder on PD6 (ADC CH6)** | EasyEDA V1.0 uses a 3-button precision resistor ladder ('-', 'OK', '+') on dedicated ADC channel 6 (PD6). |
| **Calibration Header** | PA2 (CAL) pulled to GND | **PC6 (CAL) pulled to GND** | Board connects Calibration jumper H1 Pin 3 to PC6. PA2 is dedicated to analog VAC input. |
| **VAC Sensor Input** | PC4 / A2 | **PA2 (ADC CH0)** | CH32V003 ADC channel 0 is on PA2. (PC4 is used as digital TM1637 CLK). |
| **CT Sensor Input** | Undefined pin in early draft | **PA1 (ADC CH1)** | ZHT103 current transformer connects to ADC Channel 1 on PA1. |
| **True-RMS Math** | Two-pass mean and variance in `.srodata` SRAM (`isqrt64`) | **Single-pass algebraic variance $N^2 \sigma^2 = N \sum x^2 - (\sum x)^2$ in Q12 (`isqrt32`)** | Single-pass formula eliminates second loop and SRAM buffer requirement, cutting ISR execution time in half while preserving exact 32-bit integer True-RMS precision. |
| **Flash Persistence** | Top 64-byte Flash page (`0x08003FC0`) with CRC16 | **Top 64-byte Flash page (`0x08003FC0`) with CRC16** | **100% IDENTICAL.** Both baseline and current code use the Fast-Page 64-byte controller with CCITT-FALSE CRC16. |
| **Protection Parameters** | P01 (245V), P02 (180V), P03 (10V), P04 (1.40A), P05 (2.5x), P06 (1.5s), P07 (0.5s), P08 (5 min), P09 (3 trips) | **P01..P09 with identical defaults, bounds, and step sizes** | **100% IDENTICAL.** Default values and calibration ranges are fully preserved in `src/config.h` and `src/system.h`. |
| **Safety Delay Countdown** | 5:00 safety delay with hysteresis start window (190V..235V) | **5:00 safety delay with hysteresis start window (190V..235V)** | **100% IDENTICAL.** Restart hysteresis window and delay countdown match baseline exactly. |
| **Lockout Protection** | P09 consecutive OC trips triggers `LOC3` lockout; hold OK 3s to unlock | **P09 consecutive OC trips triggers `LOC3` lockout; hold OK 3s to unlock** | **100% IDENTICAL.** Persistent Flash tracking of OC trips and long-press manual reset match baseline exactly. |
