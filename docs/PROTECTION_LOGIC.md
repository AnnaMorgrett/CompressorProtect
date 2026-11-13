# Compressor Protection Logic & Safety Hierarchy

**Project:** CompressorProtect  
**Target:** WCH CH32V003 (32-bit RISC-V RV32EC @ 48 MHz)  
**Document:** `docs/PROTECTION_LOGIC.md`  

---

## 1. Safety Hierarchy

The protection supervisor enforces a multi-tiered safety hierarchy:

```
Tier 1 (Instantaneous Cutoff - 0 ms Delay):
    └── Critical AC Voltage Sag (< 160V)
    └── Hardware Reset / Watchdog Reset / Power Loss (Relay defaults OFF)

Tier 2 (Fast Motor Protection - 2000 ms Delay):
    └── Locked Rotor Current / Stalled Motor (I > 1.6A)

Tier 3 (Sustained Electrical Overload - 4000 to 5000 ms Delay):
    └── Running Current Overload (I > 1.2A for 4s)
    └── AC Mains Over-Voltage (V > 260V for 5s)
    └── AC Mains Under-Voltage (V < 175V for 5s)

Tier 4 (Qualification & Cycling Interlocks):
    └── Minimum Compressor OFF Timer (300s non-blocking delay)
    └── Stable Voltage Recovery Qualification Window (185V <= V <= 250V for 3s)
    └── Rolling Retry Limit (Lockout after 3 unrecovered trips)
```

---

## 2. Protection Thresholds Specification Table

| Protection Parameter | Trigger Threshold | Trip Delay | Recovery Threshold | Recovery Action | Status / Calibration |
|:---|:---:|:---:|:---:|:---|:---:|
| **Critical Voltage Sag** | $< 160\text{ V}$ | **$0\text{ ms}$ (Instant)** | $\ge 185\text{ V}$ for $3\text{ s}$ | Enforce $300\text{ s}$ Delay | Provisional Default |
| **Over-Voltage Protection (OVP)** | $> 260\text{ V}$ | $5000\text{ ms}$ | $\le 250\text{ V}$ for $3\text{ s}$ | Enforce $300\text{ s}$ Delay | User Configurable (`P-01`) |
| **Under-Voltage Protection (UVP)** | $< 175\text{ V}$ | $5000\text{ ms}$ | $\ge 185\text{ V}$ for $3\text{ s}$ | Enforce $300\text{ s}$ Delay | User Configurable (`P-02`) |
| **Locked Rotor Current (LRA)** | $> 1.6\text{ A}$ | $2000\text{ ms}$ | $0\text{ A}$ (Motor Stopped) | Auto-retry (up to 3x) | User Configurable (`P-04`) |
| **Running Overcurrent (OCP)** | $> 1.2\text{ A}$ | $4000\text{ ms}$ | $0\text{ A}$ (Motor Stopped) | Auto-retry (up to 3x) | User Configurable (`P-03`) |
| **Starting Inrush Blanking** | N/A | $1500\text{ ms}$ | N/A | Blanking Window | Hardcoded |
| **Minimum OFF Delay Timer** | Shutdown event | $300\text{ s}$ ($5\text{ min}$) | Countdown = 0 | Engage Compressor | User Configurable (`P-05`) |
| **Rolling Retry Reset Window** | Continuous Run | $10\text{ min}$ | Clears Retries | Resets Retry Count to 0 | Hardcoded |
| **Persistent Lockout Limit** | 3 Failed Starts | Immediate | Manual OK Button | Requires Safe Voltage | Hardcoded |

> [!NOTE]
> Voltage and current thresholds originate from the proven legacy ATmega328P design. They represent **provisional engineering defaults** and should be calibrated against specific compressor ratings in production.

---

## 3. Voltage Protection & Hysteresis Specifications

### Over-Voltage Protection (OVP)
- **Rationale:** Sustained overvoltage increases motor magnetic core saturation, elevates iron losses, causes winding insulation degradation, and risks dielectric breakdown of the motor start/run capacitors.
- **Trip Condition:** $V_{RMS} > V_{OVP\_TRIP}$ (`260 V`) continuously for $\ge 5000\text{ ms}$.
- **Recovery Hysteresis:** Voltage must drop $\ge 10\text{ V}$ below trip limit ($V_{RMS} \le 250\text{ V}$) for at least $3000\text{ ms}$ (`VOLTAGE_QUALIFY_TIME_MS`) before the system initiates the restart delay.

### Under-Voltage Protection (UVP)
- **Rationale:** Hermetic induction motors produce torque proportional to the square of voltage ($T \propto V^2$). When voltage sags, available starting and breakdown torque decreases drastically. The compressor draws higher current to maintain shaft power, causing stator winding overheating and potential rotor stall.
- **Trip Condition:** $V_{RMS} < V_{UVP\_TRIP}$ (`175 V`) continuously for $\ge 5000\text{ ms}$.
- **Recovery Hysteresis:** Voltage must rise $\ge 10\text{ V}$ above trip limit ($V_{RMS} \ge 185\text{ V}$) for at least $3000\text{ ms}$ before recovery is authorized.

### Critical Voltage Sag Cutoff
- **Rationale:** Severe line drops (e.g., fault on local transformer or heavy inductive line load) drop compressor torque instantaneously below the load torque, causing instantaneous motor stall.
- **Trip Condition:** $V_{RMS} < 160\text{ V}$ evaluated on the fast $20\text{ ms}$ cycle. Trips immediately ($0\text{ ms}$ tolerance).

### Elimination of the Startup Deadband Bug
- **Legacy Defect:** In old firmware, power-up qualification checked recovery limits ($185\text{ V}$ to $250\text{ V}$), but error transitions only checked trip limits ($175\text{ V}$ and $260\text{ V}$). If mains energized at $180\text{ V}$, the system remained indefinitely stuck in standby without entering an error state or starting.
- **Resolution:** A dedicated `STATE_POWER_QUALIFY` state explicitly monitors line stability. If voltage is between trip and recovery limits ($175\text{ V} - 185\text{ V}$), the display indicates `QUAL` / `WAIT` and restarts the qualification timer until voltage cleanly satisfies the recovery bounds.

---

## 4. Dual-Threshold Motor Current Protection

Single-phase hermetic refrigeration compressors exhibit two distinct overcurrent failure modes:

```
Current (Amperes)
    ▲
5.0 ┼               [Inrush Spike: < 1.5s - BLANKED]
    │                 │
1.6 ┼ - - - - - - - - ┼ - - - - - - - - - [LRA Limit: Trips if > 2.0s]
    │                 │                      │
1.2 ┼ - - - - - - - - ┼ - - - - - - - - - - -┼ - - [OCP Limit: Trips if > 4.0s]
    │                 │                      │        │
0.8 ┼─────────────────┴──────────────────────┴────────┴──── [Normal Running ~0.8A]
    └────────────────────────────────────────────────────────► Time
```

1. **Starting Inrush Blanking Window ($1500\text{ ms}$):**
   - Upon relay closure, a split-phase or capacitor-start motor exhibits a brief inductive current inrush of $4\times$ to $6\times$ nominal running current while accelerating up to synchronous speed.
   - For the first $1500\text{ ms}$, this starting surge is blanked to avoid nuisance trips.
2. **Locked Rotor Current / Stalled Motor (LRA):**
   - If the compressor fails to start within $1500\text{ ms}$ (due to unequal head pressure, low line voltage, or mechanical seizure), current remains at the locked-rotor level ($> 1.6\text{ A}$).
   - The LRA supervisor trips after $2000\text{ ms}$, de-energizing the relay to prevent winding insulation burnout.
3. **Running Overload (OCP):**
   - During steady-state operation, elevated condensing temperatures, restricted airflow, or high ambient load can push motor current beyond rated full-load amps ($1.2\text{ A}$).
   - The OCP supervisor enforces an inverse-time trip delay of $4000\text{ ms}$ before opening the relay.

---

## 5. Rolling Retry Window & Lockout Supervisor

To prevent an unattended compressor from continuously cycling on a mechanical defect (which would degrade the relay contacts and cause extreme motor thermal stress):
- Each overcurrent trip (LRA or OCP) increments an internal `retryCount`.
- **Lockout Threshold:** If `retryCount >= 3`, the controller transitions to `STATE_LOCKOUT`. The relay is locked permanently de-energized, the display flashes `LOCK`, and reminder chirps sound every 15 minutes.
- **Rolling Window Reset:** If the compressor starts and operates continuously in `STATE_NORMAL_RUN` for **10 minutes** ($600,000\text{ ms}$), the running condition is classified as fully healthy, and `retryCount` is automatically reset to zero.
- **Manual Reset Safety Interlock:** When in `STATE_LOCKOUT`, a technician can press the Menu/OK button to reset the lockout. **Safety Rule:** The reset request is rejected if line voltage is currently outside safe recovery limits ($185\text{ V} - 250\text{ V}$).

