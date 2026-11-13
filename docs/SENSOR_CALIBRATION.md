# Sensor Calibration & Analog Signal Processing

**Project:** CompressorProtect  
**Target:** WCH CH32V003 (32-bit RISC-V RV32EC @ 48 MHz)  
**Document:** `docs/SENSOR_CALIBRATION.md`  

---

## 1. Analog Front-End Overview

The CH32V003 features a 10-bit Successive Approximation Register (SAR) Analog-to-Digital Converter (ADC):
- **Resolution:** 10 bits ($2^{10} = 1024$ quantization levels: $0$ to $1023$).
- **Reference Voltage ($V_{REF}$):** Tied internally to $V_{DD} = +5.0\text{ V}$.
- **Least Significant Bit (LSB):**
  $$V_{LSB} = \frac{5.000\text{ V}}{1024} \approx 4.883\text{ mV / count}$$
- **Sampling Time:** Configured for 55.5 ADC clock cycles for low source-impedance distortion.

---

## 2. AC Mains Voltage Sensing (ZMPT101B)

### Circuit Topology (Schematic Reference `archive_old/CH32Version.png`)
- **Primary:** High-voltage AC line connects through limiting resistor $R_{14} = 820\text{ k}\Omega$ into U2 (ZMPT101B current-type potential transformer).
- **Secondary Burden:** Sampling burden resistor $R_8 = 100\ \Omega$.
- **DC Mid-Rail Bias:** Resistor divider $+5\text{V} \rightarrow R_5\ (10\text{ k}\Omega) \rightarrow R_6\ (10\text{ k}\Omega) \rightarrow \text{GND}$ with filter capacitor $C_4 = 1.0\text{ µF}$ creates a stable $+2.50\text{ V}$ virtual ground (ADC count $\sim 512$).
- **Filtering & Protection:** Low-pass resistor $R_7 = 1.0\text{ k}\Omega$ feeds into MCU pin **`PA2` (ADC Channel AIN0)** clamped by zener diode U6 (`ZMM5V1`).

### Mathematical Model & Scaling
Over a complete $20\text{ ms}$ AC mains window ($50\text{ Hz}$ cycle, $\sim 80\text{ samples}$), the firmware tracks $V_{max}$ and $V_{min}$:
$$V_{pp} = V_{max} - V_{min} \quad (\text{ADC counts})$$
$$V_{RMS} = V_{pp} \times K_{VOLT}$$

Where $K_{VOLT}$ is the provisional scaling constant:
$$K_{VOLT} = 1.635\text{ V / count}$$

### Bench Calibration Procedure
1. Connect the unit to an isolated, variable AC autotransformer (Variac) and reference True-RMS multimeter (Fluke 87V or equivalent).
2. Set Variac to $220.0\text{ V AC}$.
3. Observe the displayed voltage on the TM1637 screen (Page 0).
4. If reading deviates:
   $$K_{VOLT\_NEW} = K_{VOLT\_OLD} \times \left( \frac{V_{REFERENCE}}{V_{DISPLAYED}} \right)$$
5. Plausibility Range: Valid readings must fall between $50\text{ V}$ and $320\text{ V}$. Outside this window, `voltageValid` is cleared.

---

## 3. Compressor Motor Current Sensing (ZHT103)

### Circuit Topology (Schematic Reference `archive_old/CH32Version.png`)
- **Sensor:** L1 ZHT103 current transformer through which the compressor live conductor passes.
- **Burden Resistor:** $R_2 = 100\ \Omega$ across secondary terminals.
- **DC Mid-Rail Bias:** Resistor divider $+5\text{V} \rightarrow R_{11}\ (10\text{ k}\Omega) \rightarrow R_{12}\ (10\text{ k}\Omega) \rightarrow \text{GND}$ with filter capacitor $C_3 = 1.0\text{ µF}$ centers the AC current waveform at $+2.50\text{ V}$.
- **Interface:** Feeds through $R_{13} = 1.0\text{ k}\Omega$ into MCU pin **`PA1` (ADC Channel AIN1)** clamped by zener diode U7 (`ZMM5V1`).

### Integer True-RMS Computation
To achieve microsecond execution times without floating-point division or square roots:
1. Accumulate $N$ samples over $20\text{ ms}$:
   $$\text{Sum} = \sum_{i=1}^N \text{ADC}_i, \quad \text{SumSq} = \sum_{i=1}^N \text{ADC}_i^2$$
2. Compute mean offset and variance:
   $$\text{Mean} = \frac{\text{Sum}}{N}, \quad \text{MeanSq} = \frac{\text{SumSq}}{N}$$
   $$\text{Variance} = \text{MeanSq} - \text{Mean}^2$$
3. Fast 32-bit Integer Square Root:
   $$\text{RMS Counts} = \text{isqrt32}(\text{Variance})$$
4. Scaled Current in Milliamperes:
   $$I_{mA} = \text{RMS Counts} \times (K_{CURRENT} \times 1000)$$
   Where $K_{CURRENT} = 0.01969\text{ A / count}$.
5. Noise Floor Clamping: If $I_{mA} < 50\text{ mA}$, current is forced to zero to prevent display jitter when the motor is de-energized.

---

## 4. Thermistor Sensors (NTC1 & NTC2) Status & Engineering Audit

### NTC1: External Compressor Body / Shell Thermistor
- **Intended Physical Location:** Mechanically bonded with thermal mastic to the top external shell of the hermetic compressor dome.
- **Purpose:** Monitors abnormal compressor envelope temperature caused by loss of charge, broken discharge valves, or severe internal friction.
- **Important Engineering Boundary:** External shell temperature lags internal motor winding temperature by several minutes. An external NTC **does NOT replace** the motor's internal bimetallic thermal overload protector (Klixon).

### NTC2: Refrigerant Liquid-Line Thermistor
- **Intended Physical Location:** Thermally bonded to the liquid line between the filter-drier outlet and the capillary tube inlet.
- **Purpose:** Diagnostic detection of liquid-line thermal anomalies (e.g., partial capillary tube restriction or moisture icing).
- **Important Engineering Boundary:** A single temperature point cannot definitively differentiate between a restricted filter-drier, an iced capillary, or low refrigerant charge. It serves as a diagnostic warning, not an unconditional emergency shutdown.

### Hardware Reality on Current Board (SOP-16 CH32V003A4M6)
- **Net NTC1:** Connects to **`PC3` (Pin 3)**.
- **Net NTC2:** Connects to **`PC2` (Pin 2)**.
- **Silicon Audit:** The CH32V003 ADC multiplexer **does not connect to Port C pins PC0, PC1, PC2, PC3, PC5, PC6, or PC7**. Only `PC4` has analog routing (`AIN2`).
- **Firmware Status:** Both thermistors are explicitly flagged as **`SENSOR_STATUS_UNAVAILABLE_NO_ADC`**.
- **Display Output:** Displays ` -NC` (Not Connected / No ADC).
- **Safety Commitment:** The firmware strictly prohibits fabricating or simulating temperature readings. If a future board revision routes NTC1/NTC2 to analog-capable pins (such as `PD2` / `PD3` on TSSOP20), the Steinhart-Hart / Beta equation models documented below can be activated.

### Mathematical Beta Model (For Future Hardware Revisions)
$$R_{NTC} = R_{PULLUP} \times \left( \frac{\text{ADC}}{1023 - \text{ADC}} \right)$$
$$\frac{1}{T_K} = \frac{1}{T_0} + \frac{1}{\beta} \ln\left( \frac{R_{NTC}}{R_0} \right)$$
$$T_C = T_K - 273.15$$

- $R_0 = 10,000\ \Omega$ at $T_0 = 298.15\text{ K}$ ($25^\circ\text{C}$)
- $\beta = 3950\text{ K}$
- $R_{PULLUP} = 10,000\ \Omega$ ($R_{22}$ / $R_{23}$)

