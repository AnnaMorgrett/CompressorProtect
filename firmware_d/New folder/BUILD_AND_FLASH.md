# CompressorProtect - Build & Flashing Guide

This document provides step-by-step instructions for building the CH32V003 CompressorProtect firmware and flashing it to the target microcontroller using either the Arduino environment or command-line utilities.

---

## 1. Toolchain & Dependencies

### 1.1 Hardware Programmer
- **Recommended**: **WCH-LinkE** (High-speed RISC-V debug probe) or compatible CMSIS-DAP / minichlink adapter.
- **Flashing Protocol**: 1-wire Single-Wire Debug Interface (SDI) on pin `PD1`.

### 1.2 Software Requirements
- **Arduino IDE 2.x** or **Arduino CLI** ($\ge 0.35.0$).
- **WCH CH32V Core**: Package `WCH:ch32v` (version 1.0.4 or 1.0.5).
  - Board Package Manager URL: `https://github.com/openwch/board_manager_files/raw/main/package_ch32v_index.json`
- **Flashing Utility**: `minichlink` (from open-source [`ch32fun`](https://github.com/cnlohr/ch32fun)) or `WCHISPTool`.

---

## 2. Hardware Pin Connections (Programming Header H1)

The target PCB features programming header `H1` adjacent to the microcontroller:

```
               +---------------------------------------+
               |        PROGRAMMING CONNECTIONS        |
               +---------------------------------------+

       WCH-LinkE Programmer                        Target Board (Header H1)
       +-------------------+                       +----------------------+
       |       3V3         |---------------------->| Pin 1: 3.3V (VCC)    |
       |       SWDIO       |---------------------->| Pin 2: PD1 (SWIO)    |
       |       GND         |---------------------->| Pin 4: GND (Ground)  |
       |    (Optional TX)  |<----------------------| Pin 3: PC7 (Cal/Dbg) |
       +-------------------+                       +----------------------+
```

> [!WARNING] High Voltage Hazard!
> **NEVER connect the WCH-LinkE programmer while the board is connected to AC mains (220 VAC)!**
> Always disconnect AC mains power completely before attaching the programming header. Power the microcontroller during flashing via the 3.3V pin from the WCH-LinkE probe.

---

## 3. Building via Arduino CLI

The project is structured with a root sketch entry point at `firmware/firmware.ino` and modular `.cpp`/`.h` files.

### 3.1 Standard CLI Compilation Command
From the root directory of the repository, execute:
```bash
arduino-cli compile \
  -b WCH:ch32v:CH32V00x_EVT:pnum=CH32V003F4,clock=48MHz_HSI,opt=osstd,dbg=none,rtlib=nano \
  firmware \
  --export-binaries
```

### 3.2 Key Compiler Flags Explained
- `pnum=CH32V003F4`: Directs the toolchain to compile for the CH32V003 family. *(Note: The physical PCB uses `CH32V003A4M6` SOP16; both chips share identical QingKe V2A silicon die, register sets, and pinout allocations)*.
- `clock=48MHz_HSI`: Configures the internal high-speed oscillator (HSI) with 2x PLL to run at maximum 48 MHz system clock.
- `opt=osstd`: Enables `-Os` optimization with link-time function/data section garbage collection (`-ffunction-sections -fdata-sections -Wl,--gc-sections`).
- `dbg=none`: Disables heavy hardware breakpoint runtime stub code.
- `rtlib=nano`: Links the ultra-compact `nano.specs` C standard library to conserve flash and RAM.

### 3.3 Output Binaries
Upon successful compilation, binaries are generated in:
```
firmware/build/WCH.ch32v.CH32V00x_EVT/
  ├── firmware.ino.hex     (Intel HEX format)
  ├── firmware.ino.bin     (Raw binary image)
  ├── firmware.ino.elf     (Executable and Linkable Format)
  └── firmware.ino.map     (Symbol and section memory map)
```

---

## 4. Building via Arduino IDE 2.x

1. Open Arduino IDE 2.x.
2. Go to **File -> Open** and select `CompressorProtect/firmware/firmware.ino`.
3. In **Tools -> Board**, select `CH32V00x_EVT`.
4. Configure the **Tools** menu settings:
   - **Part Number**: `CH32V003F4`
   - **Clock Source**: `48MHz (Internal / HSI)`
   - **Optimize**: `Smallest (-Os)`
   - **Runtime Library**: `Nano (-specs=nano.specs)`
   - **Debug**: `None`
5. Click the **Verify** (checkmark) button.
6. Verify output confirms:
   - Flash size: $\le 16,384\,\text{bytes}$
   - RAM size: $\le 2,048\,\text{bytes}$

---

## 5. Flashing with minichlink

`minichlink` is a high-speed command-line flashing tool that connects via WCH-LinkE in RV (RISC-V) mode.

### 5.1 Verification and Unprotecting Chip
If flashing a fresh factory chip that has readout protection (RDP) enabled:
```bash
minichlink -u
```

### 5.2 Flashing HEX Binary
```bash
minichlink -w firmware/build/WCH.ch32v.CH32V00x_EVT/firmware.ino.hex flash -b
```

### 5.3 Flashing Raw Binary
```bash
minichlink -w firmware/build/WCH.ch32v.CH32V00x_EVT/firmware.ino.bin flash -b
```
*(The `-b` flag automatically triggers a hardware system reboot after verification)*.

---

## 6. Flashing with WCHISPTool (USB-Serial / Bootloader)

If using the internal bootloader via USB-to-UART:
1. Connect USB-UART TX to board `PD1`, RX to `PC7`, GND to GND.
2. Hold `BOOT0` high (if pin available) or use WCHISPTool in WCH-Link mode.
3. Open **WCHISPTool**, choose **CH32V00x Series**, select `firmware.ino.hex`, and click **Download**.

---

## 7. Troubleshooting

| Symptom | Probable Cause | Corrective Action |
| :--- | :--- | :--- |
| `minichlink: Could not find debugger` | WCH-LinkE is in ARM/DAP mode instead of RV mode | Press the mode switch button on the WCH-LinkE probe until the blue LED illuminates, or run `minichlink -a` |
| `Target not responding / not halted` | Cable too long or missing common ground | Ensure ground wire is connected directly to target PCB; keep SWIO wire $< 15\,\text{cm}$ |
| `Flash write error / protected` | Readout protection (RDP) is active | Execute `minichlink -u` to erase and unprotect the flash |
| `Compilation error: region 'FLASH' overflowed` | Debug logging enabled or `-Os` missing | Ensure `ENABLE_DEBUG_LOG 0` in `pins.h`, and verify `opt=osstd` and `rtlib=nano` flags are present |
| `Relay clicks rapidly on power-up` | Unstable 5V rail or brownout | Ensure 5V DC supply rail can deliver at least 150 mA to energize the relay coil |

