# CompressorProtect - Test Plan & Verification Specification

This document details the comprehensive testing protocol for the CH32V003 CompressorProtect firmware, encompassing automated host unit tests and bench electrical validation procedures.

---

## 1. Automated Host Unit Tests (`tests/test_logic.py`)

A standalone host unit test suite is maintained in `tests/test_logic.py`. It executes on any workstation with standard Python 3 (no external dependencies required) to mathematically validate algorithmic components independently of physical hardware.

### 1.1 Running the Automated Test Suite
From the root of the repository:
```bash
python tests/test_logic.py
```

### 1.2 Automated Test Coverage Matrix
| Test Case Identifier | Component Tested | Input Vector | Expected Output / Invariant | Status |
| :--- | :--- | :--- | :--- | :---: |
| `test_isqrt32_exact_squares` | Fast Integer Square Root | $0, 1, 4, 9, 16, \dots, 65536^2$ | $\text{isqrt32}(x^2) = x$ | **PASS** |
| `test_isqrt32_non_squares` | Fast Integer Square Root | Intermediate integers $(2, 3, 5, 10, \dots)$ | $\lfloor \sqrt{x} \rfloor$ matches `math.isqrt()` | **PASS** |
| `test_button_decode_idle` | Button Resistor Ladder | ADC reading $1023 \pm 10$ counts | `BUTTON_NONE` | **PASS** |
| `test_button_decode_plus` | Button Resistor Ladder | ADC reading $184 \pm 40$ counts | `BUTTON_PLUS` | **PASS** |
| `test_button_decode_menu` | Button Resistor Ladder | ADC reading $327 \pm 40$ counts | `BUTTON_MENU` | **PASS** |
| `test_button_decode_minus` | Button Resistor Ladder | ADC reading $512 \pm 40$ counts | `BUTTON_MINUS` | **PASS** |
| `test_voltage_normal_no_trip` | Voltage Protection Logic | 220 VAC steady-state | State = `NORMAL`, Relay = Closed | **PASS** |
| `test_voltage_ovp_trip` | Voltage Protection Logic | 255 VAC for 1,100 ms | Trip `E-01`, Relay = Open | **PASS** |
| `test_voltage_uvp_trip` | Voltage Protection Logic | 180 VAC for 1,100 ms | Trip `E-02`, Relay = Open | **PASS** |
| `test_voltage_critical_sag` | Voltage Protection Logic | 150 VAC step input | Instantaneous Trip `E-05` (<100 ms) | **PASS** |
| `test_current_normal_no_trip`| Current Protection Logic | 3.5 A RMS steady-state | State = `NORMAL`, Relay = Closed | **PASS** |
| `test_current_ocp_trip` | Current Protection Logic | 5.5 A RMS for 3,100 ms | Trip `E-03`, Relay = Open | **PASS** |
| `test_current_stall_trip` | Current Protection Logic | 15.0 A RMS step input | Trip `E-04` (<200 ms) | **PASS** |
| `test_storage_checksum_valid`| Storage XOR Algorithm | Struct bytes $[250, 185, 45, 300, 0]$ | Checksum matches byte 9 | **PASS** |
| `test_storage_corruption_fallback`| Storage Integrity | Corrupted byte in struct | Triggers fallback to default values | **PASS** |

---

## 2. Electrical Bench Testing Setup

### 2.1 Equipment Required
1. **Isolated Variable AC Autotransformer (Variac)**: 0 to 300 VAC, 50/60 Hz, $\ge 500\,\text{VA}$.
2. **AC Isolation Transformer**: 1:1 ratio for oscilloscope probe grounding safety.
3. **Calibrated True RMS Multimeter**: Fluke 87V or equivalent for precision reference.
4. **Adjustable AC Load Bank**: Rheostat or dummy halogen lamp bank (0 to 15 A draw).
5. **AC Current Clamp / Current Probe**: Tektronix TCP0030A or isolated Hall clamp.
6. **Digital Storage Oscilloscope**: 2-4 channels with isolated differential probes.
7. **Regulated DC Bench Power Supply**: 5.0 V / 1 A (for auxiliary circuit verification).
8. **WCH-LinkE Debugger**: For flashing, reset tracing, and SWIO verification.

```
       +-------------------------------------------------------------+
       |                  ELECTRICAL BENCH TOPOLOGY                  |
       +-------------------------------------------------------------+
 [Mains 230V] --> [Isolation Xfmr] --> [Variac (0-300V)] --> [DUT Mains In]
                                                                  |
                                                           [Relay Contacts]
                                                                  |
                                                           [AC Load Bank]
                                                                  |
                                                           [Current CT L1]
```

---

## 3. Step-by-Step Bench Verification Procedures

### 3.1 Procedure 1: Power-up, Watchdog & Option Bytes Test
1. Set Variac output to 220 VAC. Energize the board.
2. Verify regulated 5V (TP_5V) and 3.3V (TP_3V3) rails.
3. Observe TM1637 display: All segments briefly light, then display firmware banner `CP01` followed by `dLY` (countdown).
4. Verify Independent Watchdog (IWDG) operation: Force an infinite loop via debug command; ensure MCU automatically resets within $2.0 \pm 0.2\,\text{s}$.
5. Corrupt Option Bytes by clearing flash sector: Verify MCU detects invalid checksum and reloads factory defaults (`250V`, `185V`, `4.5A`, `300s`, `Mute=0`) cleanly.

### 3.2 Procedure 2: Button Ladder Resistor Divider Validation
Measure DC voltage on pin `PD6` (`PIN_KEY`, Pin 10) with a digital multimeter while pressing front-panel buttons:

| Button State | Expected DC Voltage on PD6 | Expected 10-Bit ADC Raw Value | Allowed Tolerance Window |
| :--- | :---: | :---: | :---: |
| **Idle (No button pressed)** | $3.30\,\text{V}$ | 1023 | $980 - 1023$ |
| **PLUS Button Pressed** | $0.59\,\text{V}$ | 184 | $144 - 224$ |
| **MENU Button Pressed** | $1.05\,\text{V}$ | 327 | $287 - 367$ |
| **MINUS Button Pressed** | $1.65\,\text{V}$ | 512 | $472 - 552$ |

*Pass Criteria*: Each button event registers reliably with zero bounce false-triggers or wrong-key assignments.

### 3.3 Procedure 3: AC Voltage Protection Accuracy & Hysteresis
1. Calibrate reference: Adjust Variac from 180V to 260V in 10V steps. Verify displayed voltage on TM1637 matches reference DMM within $\pm 2.0\,\text{VAC}$.
2. **OVP Verification**:
   - Slowly raise Variac from 220 VAC to 252 VAC ($P\text{-}01 + 2\,\text{V}$).
   - Start stopwatch upon reaching 252 VAC.
   - Verify relay de-energizes (`PD4` goes LOW) after exactly $1,000 \pm 100\,\text{ms}$.
   - Verify display flashes `E-01` and buzzer sounds double-beep alarm.
   - Lower Variac to 247 VAC ($P\text{-}01 - 3\,\text{V}$). Confirm relay remains OPEN (hysteresis active).
   - Lower Variac to 244 VAC ($P\text{-}01 - 6\,\text{V}$). Confirm system transitions to `DELAY` countdown after 3 seconds.
3. **UVP Verification**:
   - Lower Variac from 220 VAC to 183 VAC ($P\text{-}02 - 2\,\text{V}$).
   - Verify relay opens after $1,000 \pm 100\,\text{ms}$, display flashes `E-02`.
   - Raise Variac to 191 VAC ($P\text{-}02 + 6\,\text{V}$). Confirm transition to `DELAY` countdown after 3 seconds.
4. **Critical Sag Verification**:
   - Step Variac rapidly from 220 VAC to 140 VAC.
   - Using storage scope on `PD4`, measure relay turn-off propagation delay.
   - Confirm relay opens in $< 100\,\text{ms}$ with fault code `E-05`.

### 3.4 Procedure 4: AC Current Protection & Locked Rotor Test
1. Connect load bank through the on-board current transformer (CT).
2. Set load to 3.0 A RMS. Verify TM1637 displays ` 3.0A` during telemetry rotation within $\pm 0.2\,\text{A}$.
3. **Running OCP Verification**:
   - Increase load to 5.0 A RMS (threshold $P\text{-}03 = 4.5\,\text{A}$).
   - Start stopwatch. Confirm relay opens after $3,000 \pm 200\,\text{ms}$ with fault code `E-03`.
4. **Locked Rotor / Inrush Verification**:
   - Set load bank to 14.0 A RMS.
   - Capture relay control pin `PD4` and current waveform on digital oscilloscope.
   - Trigger contact closure. Verify relay disconnects within $\le 200\,\text{ms}$ with fault code `E-04`.

### 3.5 Procedure 5: Anti-Short-Cycle Restart Delay & Quick-Bypass
1. Trip the controller via momentary undervoltage.
2. Return voltage to 220 VAC. Controller enters `DELAY` state.
3. Confirm display alternates between `dLY` and remaining countdown seconds (e.g., ` 299`, ` 298` ...).
4. Verify relay remains strictly DE-ENERGIZED throughout the countdown.
5. **Quick-Bypass Test**:
   - While countdown is active, press and hold **PLUS + MINUS** simultaneously.
   - Confirm remaining time jumps to 5 seconds.
   - Confirm relay energizes (`PD4` goes HIGH) smoothly at 0 seconds.

### 3.6 Procedure 6: Consecutive Retry Accountant & Persistent Lockout
1. Induce an electrical fault (e.g. UVP) 3 consecutive times with less than 2 minutes between events.
2. On the 3rd fault, confirm controller enters `LOCK` state:
   - Display shows `LOCK`.
   - Yellow LED flashes double-pulse sequence.
   - Relay remains locked OPEN regardless of voltage recovery.
3. Attempt to bypass by pressing normal keys: confirm system remains locked.
4. Hold **MENU** button for $> 5.0\,\text{s}$: confirm lockout clears and controller restarts delay sequence.

### 3.7 Procedure 7: Temperature Sensor Status Verification
1. Ensure connectors CN1 and CN2 are left open or terminated with 10k NTC thermistors.
2. Observe telemetry loop on TM1637 display:
   - Verify display renders ` -NC` (Not Connected / Uncommissioned).
   - Verify the system DOES NOT trip `E-06`, `E-07`, `E-08`, or `E-09` nuisance faults.
   - Confirm the controller operates normally and safely solely on voltage, current, and internal timing protections.

