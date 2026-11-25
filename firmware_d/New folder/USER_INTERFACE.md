# User Interface, Display & Button Operation

**Project:** CompressorProtect  
**Target:** WCH CH32V003 (32-bit RISC-V RV32EC @ 48 MHz)  
**Document:** `docs/USER_INTERFACE.md`  

---

## 1. Hardware Interface Overview

The user interface consists of:
1. **Four-Digit Seven-Segment Display (TM1637):**
   - Displays live measurements, status countdowns, fault diagnostic codes, and interactive parameter menus.
   - Connected via `PC6` (`CLK`) and `PC4` (`DIO`).
2. **Three-Button Resistor Ladder:**
   - Pushbuttons `PLUS` (`+`), `MINUS` (`-`), and `MENU/OK` (`OK`).
   - Connected as an analog resistor divider to MCU pin **`PD6` (ADC Channel AIN6)**.
3. **Status Yellow LED:**
   - Connected to **`PD5`** for immediate optical state indication.
4. **Audible Piezo Buzzer:**
   - Connected to **`PC1`** via S8050 NPN driver for key clicks and alarm sequences.

---

## 2. 3-Button Resistor Ladder Decoding

The three physical pushbuttons connect across a resistor divider with pull-up $R_{24} = 10\text{ k}\Omega$ to $+5.0\text{ V}$:

```
+5.0V ──[ R24: 10k ]──┬──────────────────────► MCU PD6 (AIN6)
                      │
                      ├──[ U11: PLUS (+)  ]──[ R21: 2.2k ]── GND
                      ├──[ U10: MENU (OK) ]──[ R20: 4.7k ]── GND
                      └──[ U9:  MINUS (-) ]──[ R19: 10k  ]── GND
```

### Voltage & ADC Threshold Windows (10-bit ADC, 5V Ref)

| Switch | Physical Label | Resistor to GND | Voltage at PD6 | Nominal ADC | Valid ADC Window | Decoded State |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **U11** | `Plus` (`+`) | $2.2\text{ k}\Omega$ | $0.90\text{ V}$ | **184** | $100 - 240$ | `BTN_ID_PLUS` |
| **U10** | `Menu` (`OK`)| $4.7\text{ k}\Omega$ | $1.60\text{ V}$ | **327** | $241 - 420$ | `BTN_ID_MENU` |
| **U9** | `Min` (`-`) | $10\text{ k}\Omega$ | $2.50\text{ V}$ | **512** | $421 - 650$ | `BTN_ID_MINUS` |
| *None* | *Idle (Open)* | Open | $5.00\text{ V}$ | **1023** | $> 850$ | `BTN_ID_NONE` |
| *Fault* | *Short to GND*| $0\ \Omega$ | $0.00\text{ V}$ | **0** | $< 80$ | Ambiguous / Ignore |

### Timing & Debounce Parameters
- **Debounce Filter:** $40\text{ ms}$ continuous stability required to register state change.
- **Short Press Event:** Key pressed and released within $< 1500\text{ ms}$.
- **Long Press Event:** Key held continuously for $\ge 1500\text{ ms}$.
- **Held Auto-Repeat (Plus & Minus only):** Begins after $500\text{ ms}$ continuous hold, repeating every $150\text{ ms}$.

---

## 3. Normal Operation: Rotating Telemetry Pages

When the system is in `STATE_NORMAL_RUN`, the display cycles through 5 information pages every 4 seconds. The user can also manually navigate pages at any time using `+` (Next) or `-` (Previous).

| Page Index | Display Output | Format Example | Meaning / Units |
|:---:|:---|:---:|:---|
| **0** | AC Mains Voltage | ` 220` | Live AC RMS Line Voltage ($V$) |
| **1** | Compressor Current | ` 1.20` | Live AC RMS Motor Current ($A$, DP on digit 1) |
| **2** | Compressor Shell Temp | ` -NC` | Shell Thermistor Status (Uncommissioned / No ADC) |
| **3** | Liquid-Line Temp | ` -NC` | Line Thermistor Status (Uncommissioned / No ADC) |
| **4** | System State | ` RUN` | Normal Running Operation |

### Startup Delay Screen
During `STATE_WAIT_MIN_OFF` or `STATE_POWER_QUALIFY`, the display shows the remaining countdown in `MM:SS` format (e.g. `05:00` down to `00:00`) with a $1\text{ Hz}$ blinking colon.

---

## 4. Fault & Lockout Display Behavior

When an abnormal condition or safety shutdown occurs:
1. **Fault Recovery Mode (`STATE_FAULT_RECOVERY`):**  
   The display alternates every $1.5\text{ seconds}$ between:
   - **Fault Diagnostic Code:** `E-01` (OVP), `E-02` (UVP), `E-03` (Sag), `E-04` (OCP), etc.
   - **Time Remaining Countdown:** `MM:SS` countdown before restart attempt.
2. **Permanent Lockout Mode (`STATE_LOCKOUT`):**  
   The display alternates every $1.5\text{ seconds}$ between:
   - **Active Fault Code:** e.g. `E-04` or `E-06`.
   - **Lockout Word:** `LOCK`.

### Buzzer Alarm Mute (User Acknowledgment)
- If an alarm is sounding during a fault or lockout, pressing **Menu/OK short** immediately silences the buzzer.
- **Safety Invariant:** Muting the buzzer **does NOT reset the fault** or bypass safety interlocks.

---

## 5. Interactive Settings Menu (`P-01` to `P-05`)

### Entering & Navigating the Menu
1. **Enter Menu:** From the home telemetry screen, press and hold **Menu/OK for $> 1.5\text{ seconds}$**. The display shows `P-01`.
2. **Select Parameter:** Press **`+`** (Next) or **`-`** (Previous) to cycle through `P-01` .. `P-05`.
3. **Edit Value:** Press **Menu/OK short** to enter value edit mode. The display shows the current parameter value.
4. **Adjust Value:** Press **`+`** to increment or **`-`** to decrement (holding either button initiates auto-repeat).
5. **Save Parameter:** Press **Menu/OK short**. The controller recalculates the XOR checksum, updates the internal Option Bytes, flashes **`SAUE`** for $1.2\text{ seconds}$, and emits a confirmation chirp.
6. **Cancel / Exit:** Press and hold **Menu/OK long** at any time to discard changes and return to the home screen.

### Parameter Directory

| Code | Parameter | Safe Range | Increment Step | Factory Default | Unit |
|:---:|:---|:---:|:---:|:---:|:---:|
| **`P-01`** | Over-Voltage Cutoff (`OVP`) | $230 - 280$ | 5 | **260** | Volts AC |
| **`P-02`** | Under-Voltage Cutoff (`UVP`) | $150 - 210$ | 5 | **175** | Volts AC |
| **`P-03`** | Continuous Running Overload (`OCP`) | $0.5 - 3.0$ | 0.1 | **1.2** | Amperes |
| **`P-04`** | Stalled Motor / Locked Rotor (`LRA`) | $1.0 - 5.0$ | 0.1 | **1.6** | Amperes |
| **`P-05`** | Compressor Minimum OFF Timer | $10 - 600$ | 10 | **300** | Seconds |

---

## 6. Special Button Actions

### Quick-Start Delay Bypass (Technician Commissioning)
- **When Applicable:** Only while counting down in `STATE_WAIT_MIN_OFF`.
- **Action:** Press and hold **PLUS (`+`) continuously for $\ge 3\text{ seconds}$**.
- **Safety Check:** The controller samples AC line voltage. If and only if voltage is within the safe recovery window ($185\text{ V} \le V \le 250\text{ V}$), the countdown is immediately bypassed, and the compressor starts. If voltage is outside the safe window, the bypass request is ignored.

### Manual Lockout Reset
- **When Applicable:** Only when locked out in `STATE_LOCKOUT`.
- **Action:** Press **Menu/OK short**.
- **Safety Check:** The controller checks live AC line voltage. If voltage is within healthy recovery limits, the lockout is cleared, retry count is reset, and the controller transitions to `STATE_POWER_QUALIFY`. If voltage is abnormal, the reset is rejected, and the system remains locked out.

