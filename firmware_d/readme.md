# How the Compressor Protector works

A plain-English guide. Diagrams render on GitHub and in VS Code (Markdown Preview Mermaid Support extension).

**One sentence:** the chip watches the mains voltage and the compressor current; if anything is unsafe it opens the relay, then waits several minutes before allowing a restart.


## 1. The big picture

The chip sits between the mains and the compressor. It measures voltage and current many times per second, and decides whether the relay may stay closed.

```mermaid
flowchart LR
  M["220 V mains"] --> VS["Voltage sensor ZMPT101B"]
  M --> RL{{"Relay: switches the compressor"}}
  RL --> LD["Compressor"]
  LD --> CS["Current sensor ZHT103"]
  VS --> ADC["Chip ADC measures 6400 times per second"]
  CS --> ADC
  ADC --> DSP["Math: volts and amps every 20 ms"]
  DSP --> BR["Protection brain"]
  BT["3 buttons"] --> BR
  BR --> RL
  BR --> UI["Display, LED, buzzer"]
```


## 2. What happens at power-on (main.c)

Everything starts in main(). It prepares the hardware (always with the relay OFF), picks a mode, then repeats five small jobs forever. The urgent work happens in interrupts, not in this loop.

```mermaid
flowchart TD
  A(["Power on"]) --> B["hal_init: clock, pins safe, RELAY OFF, 1 ms timer, watchdog"]
  B --> C["adc_init + adc_service: ADC ready, measure real supply voltage"]
  C --> D["nvm_load: read saved settings from Flash"]
  D --> E["display_init, input_init, adc_start: sampling begins"]
  E --> F["Show ---- for 0.2 s"]
  F --> G{"CAL jumper shorted?"}
  G -- yes --> H["mode_cal: Calibration"] --> Z(["donE: power cycle needed"])
  G -- no --> I{"OK button held?"}
  I -- yes --> J["mode_settings: Settings menu"] --> Z
  I -- no --> K["fsm_init: start protection"]
  K --> L1["Main loop forever"]
  L1 --> L2["1 feed watchdog if sensing alive"] --> L3["2 adc_service"] --> L4["3 input_poll: buttons"] --> L5["4 fsm_step: decisions"] --> L6["5 display_flush"] --> L1
```


## 3. How a measurement is made (adc_dsp.c)

No program code is needed to take samples: a timer, the ADC and a DMA “robot” do it by themselves. Only once per 20 ms (one wave of 50 Hz mains) does an interrupt wake the CPU to do the math.

```mermaid
flowchart LR
  T["Timer 1 ticks 6400 times per second"] --> A["ADC snapshot: VOLTAGE then CURRENT"]
  A --> DM["DMA robot stores numbers in an array"]
  DM --> Q{"128 snapshots = 20 ms?"}
  Q -- no --> T
  Q -- yes --> I["INTERRUPT: process_block"]
  I --> R["RMS math: volts and amps"]
  R --> F["fsm_on_block: trip check"]
  F --> U["Update smooth display numbers"]
  U --> T
```


## 4. The protection brain: 4 states (fsm_protect.c)

The protector is always in exactly one of four situations. Events (timers, faults, long button press) move it to another one.

```mermaid
stateDiagram-v2
  [*] --> DELAY: power on
  [*] --> LOCKOUT: power on, trip counter already full
  DELAY --> RUN: countdown 0 and voltage OK
  RUN --> TRIP: fault found (UV, OV, OC or sensor)
  TRIP --> DELAY: after 3 s
  TRIP --> LOCKOUT: after 3 s if OC trip limit reached
  LOCKOUT --> DELAY: hold OK for 3 s
```


## 5. The 20 ms safety check (fsm_on_block)

This runs inside the 20 ms interrupt, only while the relay is ON. Voltage must be bad for 3 waves (60 ms) in a row, so one electrical spike cannot cut the compressor. Right after switch-on the current limit is higher because motors draw a surge (inrush).

```mermaid
flowchart TD
  A(["Every 20 ms"]) --> B{"Relay ON?"}
  B -- no --> Z(["Nothing to check"])
  B -- yes --> C{"Voltage below UV for 3 waves in a row?"}
  C -- yes --> T1["TRIP E-UV: relay OFF now"]
  C -- no --> D{"Voltage above OV for 3 waves in a row?"}
  D -- yes --> T2["TRIP E-OV: relay OFF now"]
  D -- no --> E{"Still inside the start-up window P06?"}
  E -- yes --> F{"Current above inrush limit?"}
  F -- yes --> T3["TRIP E-OC: relay OFF now"]
  F -- no --> OK(["All fine"])
  E -- no --> G{"Current above normal limit P04?"}
  G -- no --> R["Reset over-current timer"] --> OK
  G -- yes --> H["Add 20 ms to over-current timer"]
  H --> I{"Timer reached P07?"}
  I -- yes --> T3
  I -- no --> OK
```


## 6. The waiting countdown (DELAY state)

After power-on and after every trip the relay stays OFF for P08 minutes. The timer only runs while the mains voltage is in the safe window. This protects the compressor from quick restarts.

```mermaid
flowchart TD
  A(["fsm_step in DELAY state"]) --> B{"Measuring still alive?"}
  B -- no --> C["Show E-SE and restart countdown"]
  B -- yes --> D{"Voltage inside safe start window?"}
  D -- no --> E["Restart countdown at full P08 minutes, blink E-UV or E-OV"]
  D -- yes --> F["Count down one second at a time, show MM:SS"]
  F --> G{"Countdown reached 0?"}
  G -- yes --> H["enter_run: relay ON, 1 beep, LED solid"]
  G -- no --> W(["Wait for next loop"])
```


## 7. The watchdog safety net

The main loop feeds the watchdog only if measuring is still alive. This stops a frozen ADC from leaving the relay ON with no protection.

```mermaid
flowchart TD
  A["Main loop calls hal_wdg_kick"] --> B{"Fresh 20 ms block in the last 300 ms?"}
  B -- yes --> C["Feed the watchdog: chip keeps running"]
  B -- no --> D["Do NOT feed it"]
  D --> E["About 1 s later the chip resets"]
  E --> F["Relay pin goes LOW: compressor OFF"]
```


## 8. Settings menu (hold OK at power-on)

Change the limits P01 to P09. Each one has a minimum and maximum so you cannot enter a dangerous value.

```mermaid
flowchart TD
  S(["Hold OK while powering on"]) --> A["Display shows Sett"] --> B["Press OK"]
  B --> C["Browse P01 to P09, SAVE, rSt with plus and minus"]
  C --> D{"Press OK on which item?"}
  D -- "P01 to P09" --> E["Edit value with plus and minus, OK to return"] --> C
  D -- SAVE --> F{"UV and OV limits leave a usable window?"}
  F -- no --> G["Show Err"] --> C
  F -- yes --> H["Save to Flash and show donE"]
  D -- rSt --> I["Restore factory limits and save"] --> C
```


## 9. Calibration (short CAL jumper at power-on)

Do this once per board with a multimeter so the display shows true volts and amps.

```mermaid
flowchart TD
  A(["Short the CAL jumper, power on"]) --> B["C-0: unplug mains and load, press OK"]
  B --> C{"Noise small enough?"}
  C -- no --> D["Err: try again"] --> B
  C -- yes --> E["C-V: connect mains, press plus or minus until display = multimeter, OK"]
  E --> F["C-A: OK closes relay, adjust until display = clamp meter, OK. Plus skips this step"]
  F --> G["SAVE: OK writes Flash"] --> H(["donE: remove jumper, power cycle"])
```


## Display messages cheat sheet
| Display | Meaning |
|---|---|
| `----` | Starting up, no measurement yet |
| `05:00` | Waiting countdown (minutes:seconds) before the relay may switch on |
| `220` | Running: mains voltage in volts |
| `A140` | Running: compressor current 1.40 A (alternates with voltage every 2 s) |
| `E-UV` / `E-OV` | Under-voltage / over-voltage |
| `E-OC` | Over-current (overload or stalled compressor) |
| `E-SE` | Sensor error: measuring stopped, relay forced OFF |
| `LOC3` | Locked out after too many over-current trips: hold OK 3 s to unlock |
| `Sett`, `P01`..`P09`, `SAVE`, `rSt`, `donE`, `Err` | Settings menu |
| `C-0`, `C-V`, `C-A` | Calibration steps |

## LED and buzzer
| LED | Meaning | Buzzer | Meaning |
|---|---|---|---|
| Solid | Relay ON | 1 beep | Relay switched on, or saved |
| Slow blink (1 Hz) | Countdown or Settings | 3 short beeps | Fault trip |
| Fast blink (4 Hz) | Fault shown, or Calibration | 2 long beeps | Lockout (repeats every 30 s) |
| Double flash | Lockout | | |

## Settings (defaults)
| Code | Meaning | Default | Range |
|---|---|---|---|
| P01 | Over-voltage trip | 245 V | 230-270 |
| P02 | Under-voltage trip | 180 V | 140-210 |
| P03 | Start hysteresis | 10 V | 2-20 |
| P04 | Over-current limit | 1.40 A | 0.50-9.99 |
| P05 | Inrush multiplier | x2.5 | x1.5-x5.0 |
| P06 | Inrush window | 1.5 s | 0.5-5.0 |
| P07 | Over-current delay | 0.5 s | 0.1-5.0 |
| P08 | Restart delay | 5 min | 1-10 |
| P09 | OC trips before lockout | 3 | 1-5 |

## Glossary
- **ADC**: turns a voltage on a pin into a number 0..1023.
- **RMS**: the "effective" size of an AC wave (220 V RMS heats like 220 V DC).
- **Interrupt**: an automatic pause of the main loop to run an urgent short function.
- **DMA**: a helper inside the chip that moves data without using the CPU.
- **Watchdog**: a countdown that resets the chip if the program stops feeding it.
- **Flash**: memory that keeps data without power (our settings notebook).
- **CRC**: a checksum that detects damaged data.
- **Q6 / Q8 / Q12**: fixed-point numbers, value multiplied by 64 / 256 / 4096 so the chip can avoid decimals.
- **Hysteresis**: a gap between "trip" and "start" levels so the relay does not chatter.
- **Inrush**: the big current surge when a motor starts.
