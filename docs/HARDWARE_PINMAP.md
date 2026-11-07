# Hardware Net-to-Pin Mapping & Package Audit

**Project:** CompressorProtect  
**Target:** WCH CH32V003A4M6 (SOP-16) / CH32V003F4P6 (TSSOP-20 Compatible)  
**Document:** `docs/HARDWARE_PINMAP.md`  
**Primary Schematic Reference:** `archive_old/CH32Version.png`  

---

## 1. Executive Summary: The Package Discrepancy

A critical investigation of the schematic and repository configuration was conducted:
1. **The Schematic MCU:** Component U1 in `archive_old/CH32Version.png` is explicitly designated **`CH32V003A4M6`** in a **16-pin SOP (SOP16)** package.
2. **The Repository / Arduino Core:** The official WCH Arduino Core (`arduino_core_ch32-main`) defines board variant `WCH:ch32v:CH32V00x_EVT` with chip target **`CH32V003F4`** (TSSOP-20).
3. **Silicon Reality:** In WCH's architecture, both packages share the exact same internal silicon die (QingKe V2A core, 16 KB Flash, 2 KB RAM, identical register base addresses). The SOP-16 package simply omits bringing out certain pads (such as Port C bit 5 and hardware SPI clock).
4. **Resolution:** By strictly referencing only the physical GPIOs bonded out on the SOP16 package, code compiled for the `CH32V003F4` target runs with 100% binary compatibility on the physical `CH32V003A4M6` hardware.

---

## 2. Complete Net-to-Pin Mapping Table

| Schematic Net | MCU Port | SOP16 Pin | TSSOP20 Pin | ADC Channel | Alternate Functions | Firmware Macro | Resolved Assignment | Electrical Description / Circuit Function |
|:---|:---:|:---:|:---:|:---:|:---|:---|:---:|:---|
| `BUZZ` | `PC1` | Pin 1 | Pin 5 | None | I2C1_SDA, TIM1_CH2 | `PIN_BUZZER` | **`PC1`** | Active-HIGH base drive for Q1 (S8050 NPN) driving 5V buzzer |
| `NTC2` | `PC2` | Pin 2 | Pin 6 | **None** | I2C1_SCL, TIM1_CH3 | `PIN_NTC2_RAW` | **`PC2`** | Connector CN2; $10\text{ k}\Omega$ pullup R23 (Liquid line; **NO ADC**) |
| `NTC1` | `PC3` | Pin 3 | Pin 9 | **None** | TIM1_CH4 | `PIN_NTC1_RAW` | **`PC3`** | Connector CN1; $10\text{ k}\Omega$ pullup R22 (Compressor body; **NO ADC**) |
| `DIO` | `PC4` | Pin 4 | Pin 10 | AIN2 | TIM1_CH4, MCO | `PIN_TM_DIO` | **`PC4`** | TM1637 4-digit display bidirectional data line |
| `CLK` | `PC6` | Pin 5 | Pin 12 | None | TIM1_CH3N, USART1_CTS| `PIN_TM_CLK` | **`PC6`** | TM1637 4-digit display clock line |
| `CAL` | `PC7` | Pin 6 | Pin 13 | None | USART1_RTS, TIM1_CH2 | `PIN_CAL_DEBUG`| **`PC7`** | Calibration header H1 Pin 3 (Optional Debug UART TX) |
| `TX / SWIO` | `PD1` | Pin 7 | Pin 15 | None | **SWIO**, USART1_TX_2 | `PIN_SWIO_DEBUG`| **`PD1`** | WCH-Link single-wire programming/debug line on Header H1 |
| `RELAY` | `PD4` | Pin 8 | Pin 1 | AIN7 | TIM2_CH1ETR | `PIN_RELAY` | **`PD4`** | Active-HIGH base drive for Q2 (S8050 NPN) driving 12V relay coil |
| `LED` | `PD5` | Pin 9 | Pin 2 | AIN5 | USART1_TX, TIM2_CH4 | `PIN_LED` | **`PD5`** | Active-HIGH drive for status yellow LED1 via R18 ($4.7\text{ k}\Omega$) |
| `KEY` | `PD6` | Pin 10 | Pin 3 | **AIN6** | USART1_RX, TIM2_CH3 | `PIN_KEY` | **`PD6` (AIN6)** | 3-button resistor ladder (+, Menu, -) with $10\text{ k}\Omega$ pullup R24 |
| `NC / RST` | `PD7` | Pin 11 | Pin 4 | None | NRST, TIM2_CH4 | `PD7` | **Unconnected** | Unconnected on PCB (Marked with red X on schematic) |
| `CT` | `PA1` | Pin 12 | Pin 18 | **AIN1** | OSC_IN, TIM1_CH1 | `PIN_CURRENT` | **`PA1` (AIN1)** | ZHT103 Current Transformer with burden R2 ($100\ \Omega$) & 2.5V bias |
| `VAC` | `PA2` | Pin 13 | Pin 19 | **AIN0** | OSC_OUT, TIM1_CH2 | `PIN_VOLTAGE` | **`PA2` (AIN0)** | ZMPT101B Voltage Transformer with burden R8 ($100\ \Omega$) & 2.5V bias|
| `VSS` | `GND` | Pin 14 | Pin 7/20 | Ground | Power Ground | `GND` | **`GND`** | Circuit 0V common ground return |
| `VDD` | `+5V` | Pin 15 | Pin 8 | Power | Power Supply | `VDD` | **`+5V`** | Regulated $+5.0\text{ V}$ logic power from AMS1117-5.0 regulator |
| `PROG` | `PC0` | Pin 16 | Pin 4 | None | TIM1_CH1 | `PIN_PROG_HEADER`| **`PC0`** | Header H1 Pin 2 |

---

## 3. Detailed Audit of Known Hardware Conflicts & Resolutions

### Conflict A: Relay (`PD4`) vs KEY Resistor Ladder (`PD6`)
- **Prior Code Defect:** In earlier firmware iterations, comments declared `PIN_RELAY = PD6`, while `debug_report.ino` executed `analogRead(PD6)` as the button ladder.
- **Circuit Analysis:** Tracing `archive_old/CH32Version.png` definitively demonstrates:
  - Relay transistor Q2 base connects to **`PD4` (Pin 8)** through R16 ($2.2\text{ k}\Omega$) with pull-down R15 ($10\text{ k}\Omega$) to ground.
  - The 3-button resistor ladder connects to **`PD6` (Pin 10 / ADC Channel AIN6)** with pull-up R24 ($10\text{ k}\Omega$) to $+5\text{V}$.
- **Resolution:** `PIN_RELAY` is assigned to `PD4`; `PIN_KEY` is assigned to `PD6`. There is zero pin-sharing conflict.

### Conflict B: Status LED (`PD5`) vs Hardware UART1 TX
- **Circuit Analysis:** Net `LED` terminates on **`PD5` (Pin 9)**. On the CH32V003, `PD5` is also the default hardware TX pin for USART1.
- **Resolution:** If `Serial.begin()` were initialized, the USART1 peripheral would commandeer `PD5`, driving continuous mark/space levels and conflicting with the yellow status LED. Hardware `Serial` is left permanently disabled in production code.

### Conflict C: Buzzer Pin (`PC1`) vs `PC0`
- **Circuit Analysis:** The buzzer driver transistor Q1 base connects via R1 ($2.2\text{ k}\Omega$) directly to **`PC1` (Pin 1)**. Net `PROG` connects to **`PC0` (Pin 16)**.
- **Resolution:** `PIN_BUZZER` is firmly mapped to `PC1`.

### Conflict D: TM1637 Display Clock and Data Lines
- **Circuit Analysis:**
  - Net `DIO` connects directly to MCU **`PC4` (Pin 4)**.
  - Net `CLK` connects directly to MCU **`PC6` (Pin 5)**.
- **Resolution:** `PIN_TM_DIO = PC4`, `PIN_TM_CLK = PC6`. Direct register bit-banging on Port C (`GPIOC->BSHR`, `GPIOC->BCR`) is used to control both lines with microsecond nop loops.

### Conflict E: Debug Software Serial on `PC7` (`CAL`)
- **Circuit Analysis:** `debug_tx.h` implements a bit-banged software UART on `PC7` (Pin 6). Net `CAL` connects `PC7` to Header H1 Pin 3.
- **Resolution:** In production, software UART output on `PC7` is disabled by default (`#define ENABLE_DEBUG_LOG 0` in `config.h`) to prevent unintended driving of the CAL test point.

---

## 4. Analysis of ADC Channel Limitations on CH32V003

The CH32V003 10-bit SAR ADC multiplexer connects **exclusively** to the following 8 external channels:

| ADC Channel | GPIO Pin | Physical SOP16 Pin | Schematic Usage | Analog Usability |
|:---:|:---:|:---:|:---|:---:|
| **AIN0** | `PA2` | Pin 13 | Net `VAC` (ZMPT101B Voltage Transformer) | **VERIFIED ANALOG** |
| **AIN1** | `PA1` | Pin 12 | Net `CT` (ZHT103 Current Transformer) | **VERIFIED ANALOG** |
| **AIN2** | `PC4` | Pin 4 | Net `DIO` (TM1637 Display Data) | Used as Digital I/O |
| **AIN3** | `PD2` | Not Bonded | Not available on SOP-16 package | N/A |
| **AIN4** | `PD3` | Not Bonded | Not available on SOP-16 package | N/A |
| **AIN5** | `PD5` | Pin 9 | Net `LED` (Status LED) | Used as Digital Output |
| **AIN6** | `PD6` | Pin 10 | Net `KEY` (3-Button Resistor Ladder) | **VERIFIED ANALOG** |
| **AIN7** | `PD4` | Pin 8 | Net `RELAY` (Relay Driver) | Used as Digital Output |

### The Thermistor Routing Reality
- **NTC1 (Compressor Body):** Connected to `PC3` (Pin 3). `PC3` has **NO ADC channel**.
- **NTC2 (Filter-Drier Outlet / Capillary Inlet):** Connected to `PC2` (Pin 2). `PC2` has **NO ADC channel**.

Attempting `analogRead(PC2)` or `analogRead(PC3)` in software produces invalid results or zeroes because the internal ADC analog mux physically cannot select `PC2` or `PC3`. The firmware treats both thermistor channels as **uncommissioned** and displays ` -NC`.

