# LIS3DSH — Digital 3-Axis Accelerometer
## Register / Interface Reference for Driver Development

**Document version:** 2.0 (EN) · **Date:** 2026-11-17
**Upstream source:** STMicroelectronics datasheet **DocID022405 Rev 3 (September 2017)**
**Device:** MEMS digital 3-axis "nano" accelerometer, 16-bit output, two embedded state machines
**Package:** LGA-16, 3 × 3 × 1.0 mm · **Order code:** LIS3DSHTR (tape and reel)
**Operating temperature:** −40 … +85 °C
**Scope:** register-level and interface-level reference. Timing diagrams, package drawings and
soldering profile are intentionally omitted. **All register content is preserved.**

---

## 0. Provenance, verification status, and how to read this document

### 0.1 Verification statement (read this first)

> **This document has NOT been line-by-line verified against the physical ST PDF.**
> It is a *patched plain-text conversion*, cross-checked for internal consistency, arithmetic
> correctness of every worked example, and against ST's published STM32F4Discovery LIS3DSH BSP
> constant definitions. Nothing here is a certified transcription.
>
> ```
> Verified against ST DocID022405 Rev 3 (physical PDF) on: ____-__-__   by: ______________
> Verified on hardware (bench checklist §12) on:          ____-__-__   by: ______________
> Current status: NOT VERIFIED — do not remove this line until both sign-offs are filled.
> ```

### 0.2 Status tags used throughout

| Tag | Meaning | Trust level |
|-----|---------|-------------|
| `[DS]` | Content converted from the datasheet text | Transcription assumed correct, transcription itself unverified |
| `[DERIVED]` | Computed, inferred, or written for driver authors — **not literal datasheet text** | Correct reasoning, but not an ST specification |
| `[PDF-CHECK]` | Suspected transcription error or internal inconsistency | **Must be read from the PDF before use** |
| `[BENCH]` | Behaviour the datasheet does not specify | **Must be measured on hardware** |

**Rule for driver code:** every constant that depends on a `[PDF-CHECK]` or `[BENCH]` item must be a
named compile-time constant carrying a comment with the item ID (e.g. `/* F4: burst wrap unproven */`).
Never inline such a value as a magic number.

### 0.3 Sign-off register (fill as items are closed)

| Item | Subject | Method | Result | By | Date |
|------|---------|--------|--------|----|------|
| E8 / F5 | `IEA` polarity | PDF table + scope on INT1 | *open* | | |
| F1 | ±16 g sensitivity 0.73 mg/LSB | PDF table + tilt/shaker | *open* | | |
| F4 | Address wrap `0x2D → 0x28` in a multi-sample burst | logic analyzer | *open* | | |
| F5 | `FSS` / `OVRN_FIFO` at the 32-sample boundary | forced overrun | *open* | | |
| F6 | Reset / ODR / self-test settling times | scope + data inspection | *open* | | |
| F7 | Normal vs low-power mode per ODR code | PDF table + current measurement | *open* | | |
| F8 | Minimum CS-high time between SPI frames | logic analyzer sweep | *open* | | |
| F10 | Recovery after an aborted 6-byte FIFO read | fault injection | *open* | | |

### 0.4 Patch log — what changed in v2.0

**Closed from the v1.1 patch (verified in this revision):**

| ID | Defect in the original conversion | Correction now in place |
|----|-----------------------------------|-------------------------|
| E1 | I2C wire addresses swapped, plus a leftover editorial artifact (`"0x3A? no:"`) | §5.1: `0x1D → 0x3A/0x3B`, `0x1E → 0x3C/0x3D`, artifact removed |
| E2 | "BDU = 1" examples `0x67 / 0x57 / 0x97` all had bit3 = 0 | §10: **`0x6F / 0x5F / 0x9F`**, with the wrong values shown explicitly as counter-examples |
| E3 | "DRDY active high, **pulsed** = `0xC8`" had `IEL = 0` (latched) | §10: **`0xE8`**; `0xC8` documented as latched |
| E4 | AA bandwidth codes `01`/`10` swapped | §7.24: `00=800 / 01=400 / 10=200 / 11=50 Hz`, corroborated by ST BSP (`AA_BW_400=0x40`, `AA_BW_200=0x80`) |
| E5 | SPI command byte described as "bit 0 … i.e. MSB" | §5.2: **bit7 = RW, bits6:0 = AD6..AD0** |
| E6 | "In **Bypass** … returns the oldest FIFO sample" | §7.29 / §8: Bypass returns the *current output-register* sample; only FIFO/Stream return stored samples |
| E7 | `STRT` and `BOOT` presented as equivalent resets | §10: separated — `BOOT` = trimming reload, self-clearing; `STRT` = POR, registers → defaults |

**New in v2.0 (this revision):**

| ID | Finding | Where |
|----|---------|-------|
| F1 | ±16 g sensitivity `0.73 mg/LSB` is arithmetically inconsistent with a 16-bit ±16 g range (implies ±23.9 g); all other ranges agree within −1.7 % | §4.3, §11 |
| F2 | `FIFO_CTRL` was **missing from the init sequence** → `FIFO_EN` set but `FMODE` left in Bypass → no watermark interrupt, silent stall | §10.2 (fixed) |
| F3 | Writing `CTRL_REG6 = 0x80` for `BOOT` also clears `ADD_INC` and `FIFO_EN`; after self-clear the register reads `0x00` | §7.25, §10.2 (fixed) |
| F4 | **Address auto-increment wrap `0x2D → 0x28` across a multi-sample burst is not specified** — this decides whether one long burst is legal at all | §7.29, §8.5, §11, §12 |
| F5 | `OVRN_FIFO` is a *fullness* flag, not proof of loss; latch/clear semantics and `FSS` wrap-vs-saturate at 32 unspecified | §7.31, §8.4, §11 |
| F6 | The "≥ 4/ODR" settling rule introduced in v1.1 is a lower bound on *sample availability*, not on filter/self-test settling (2.5 ms at 1600 Hz) | §10.5, §11 (replaced with conservative floors) |
| F7 | Per-code normal/low-power ODR annotation still absent; noise-density test condition may conflict with the mode selected at 100 Hz | §3, §7.20, §11 |
| F8 | Minimum CS-high time between SPI frames not specified; SPI timing characterization voltage not stated | §5.2, §11 |
| F9 | Sensitivity, offset and self-test values are **typical-only** — the datasheet gives no pass/fail limits | §4, §11 |
| F10 | BDU + FIFO behaviour after an aborted/partial 6-byte read unspecified | §7.29, §11 |
| F11 | `INT2` as the FIFO trigger source (Stream-to-FIFO): how the trigger is driven is not described | §8.3, §11 |
| F12 | `WTMP = 0` behaviour undefined | §7.30, §11 |
| — | Provenance block, status tags, sign-off register, bench checklist, project quick-reference appendix | §0, §12, App. A |

---

## 1. Key features `[DS]`

- Supply voltage **1.71 … 3.6 V** (factory calibrated at 2.5 V)
- Independent I/O supply (`Vdd_IO`), 1.8 V compatible
- Selectable full scale: **±2 / ±4 / ±6 / ±8 / ±16 g**
- I²C / SPI digital interface (SPI 4-wire or 3-wire)
- **16-bit** two's-complement output, 3 axes
- Output data rates **3.125 Hz … 1.6 kHz**
- Two programmable embedded state machines (16 steps each)
- Embedded 8-bit temperature sensor
- Embedded self-test
- **Embedded FIFO: 32 levels × 3 channels**
- Two interrupt pins: `INT1/DRDY`, `INT2`
- 10 000 g high-shock survivability

---

## 2. Pin description `[DS]`

| Pin | Name | Function |
|-----|------|----------|
| 1 | `Vdd_IO` | Power supply for I/O pins |
| 2 | NC | Not connected |
| 3 | NC | Not connected |
| 4 | `SCL / SPC` | I²C serial clock / SPI serial port clock |
| 5 | GND | 0 V supply |
| 6 | `SDA / SDI / SDO` | I²C data / SPI data input / 3-wire serial data output |
| 7 | `SEL / SDO` | I²C address selection / SPI serial data output |
| 8 | `CS` | SPI enable, I²C/SPI mode select: **1 = SPI idle, I²C enabled; 0 = SPI active** |
| 9 | `INT2` | Interrupt 2 |
| 10 | Reserved | **Connect to GND** |
| 11 | `INT1 / DRDY` | Interrupt 1 / data ready |
| 12 | GND | 0 V supply |
| 13 | GND | 0 V supply |
| 14 | `Vdd` | Power supply |
| 15 | Reserved | Connect to `Vdd`, to GND, or leave unconnected |
| 16 | GND | 0 V supply |

**Hardware notes** `[DS]`
- Decoupling: **100 nF ceramic + 10 µF** as close as possible to pin 14 (`Vdd`); also place a local
  **100 nF** on pin 1 (`Vdd_IO`).
- `Vdd` and `Vdd_IO` must both be present for correct operation.
- `Vdd` may be removed while `Vdd_IO` is kept: the communication bus stays usable, the measurement
  chain is powered off.
- For I²C, `CS` must be tied high (to `Vdd_IO`).
- I²C: `SCL` and `SDA` need external pull-ups to `Vdd_IO`.
- Axis orientation: top view, pin-1 indicator corner — **X to the right, Y up, Z out of the package
  top** (positive acceleration direction per the datasheet figure).

---

## 3. Electrical characteristics `[DS]`

Conditions: `Vdd` = 2.5 V, T = 25 °C unless noted.

| Symbol | Parameter | Min | Typ | Max | Unit |
|--------|-----------|-----|-----|-----|------|
| `Vdd` | Supply voltage | 1.71 | 2.5 | 3.6 | V |
| `Vdd_IO` | I/O supply voltage | 1.71 | — | `Vdd` + 0.1 | V |
| `IddA` | Current, active, **ODR 1.6 kHz** (normal mode) | — | 225 | — | µA |
| `IddA` | Current, active, **ODR 3.125 Hz** (low-power mode) | — | 11 | — | µA |
| `IddPdn` | Current, power-down / standby | — | 2 | — | µA |
| `VIH` | Digital high-level input | 0.8 × `Vdd_IO` | — | — | V |
| `VIL` | Digital low-level input | — | — | 0.2 × `Vdd_IO` | V |
| `VOH` | High-level output | 0.9 × `Vdd_IO` | — | — | V |
| `VOL` | Low-level output | — | — | 0.1 × `Vdd_IO` | V |
| `Top` | Operating temperature | −40 | — | +85 | °C |

> **F7 `[PDF-CHECK]`** — the two current rows correspond to *different power modes*, but the
> converted text carries **no per-ODR-code mode annotation**. Which ODR codes select normal vs
> low-power mode is therefore unknown here. This matters because the noise-density specification
> (§4) is stated for *normal mode at ODR 100 Hz*, and 100 Hz may or may not be a normal-mode code.
> Read the mode column/notes from the PDF before making noise or power claims at any ODR other
> than 1.6 kHz.

**Absolute maximum ratings** `[DS]`
- `Vdd`, `Vdd_IO`: −0.3 … 4.8 V
- Input voltage on `CS`, `SCL/SPC`, `SDA/SDI/SDO`, `SDO/SEL`: −0.3 … `Vdd_IO` + 0.3 V
- Supply on any pin must never exceed 4.8 V
- Shock: 3000 g for 0.5 ms; 10 000 g for 0.1 ms (powered and unpowered)
- Storage: −40 … +125 °C · ESD: 2 kV HBM

---

## 4. Mechanical characteristics and sensitivity

### 4.1 Specification table `[DS]`

| Symbol | Parameter | Condition | Typ | Unit |
|--------|-----------|-----------|-----|------|
| `FS` | Full scale | `FSCALE = 000` | ±2.0 | g |
| `FS` | Full scale | `FSCALE = 001` | ±4.0 | g |
| `FS` | Full scale | `FSCALE = 010` | ±6.0 | g |
| `FS` | Full scale | `FSCALE = 011` | ±8.0 | g |
| `FS` | Full scale | `FSCALE = 100` | ±16.0 | g |
| `So` | Sensitivity | ±2 g | 0.06 | mg/LSB |
| `So` | Sensitivity | ±4 g | 0.12 | mg/LSB |
| `So` | Sensitivity | ±6 g | 0.18 | mg/LSB |
| `So` | Sensitivity | ±8 g | 0.24 | mg/LSB |
| `So` | Sensitivity | ±16 g | **0.73** | mg/LSB |
| `TCSo` | Sensitivity change vs temperature | FS = 00 | 0.01 | %/°C |
| `TyOff` | Zero-g level offset accuracy | FS = 00 | ±40 | mg |
| `TCOff` | Zero-g level change vs temperature | max delta from 25 °C | ±0.5 | mg/°C |
| `An` | Noise density | FS = 00, **normal mode, ODR 100 Hz** | 150 | µg/√Hz |
| `ST` | Self-test positive difference | ±2 g, X and Y, `ST2:ST1 = 01` | 140 | mg |
| `ST` | Self-test positive difference | ±2 g, Z, `ST2:ST1 = 01` | 590 | mg |

**F9 `[DERIVED]`** — every mechanical value above is **typical-only**. The datasheet provides no
min/max for sensitivity, offset or self-test delta. Consequently there is **no datasheet pass/fail
band for self-test**: any acceptance window (e.g. ±40 % of typical) is a project decision and must
be documented as such, not attributed to ST.

### 4.2 Conversion and digital clipping `[DERIVED]`

```
acceleration_mg = raw_int16 * sensitivity_mg_per_LSB
acceleration_g  = acceleration_mg / 1000
```

At ±2 g: 1 g ≈ **16 667 LSB**, and `32767 × 0.06 mg = 1966.02 mg`, so the signed 16-bit code
**saturates slightly below the nominal ±2.0 g**. Zero-g level: sensor flat on a table reads ≈ 0 g on
X and Y, **+1 g on Z**.

Factory calibrated for sensitivity and zero-g; trimming is stored in non-volatile memory and
reloaded at every power-up. No user calibration is required.

### 4.3 Full-scale consistency check — **F1** `[DERIVED]` + `[PDF-CHECK]`

Implied span = `So × 65536`:

| Range | So (mg/LSB) | Implied full scale | Nominal | Deviation |
|-------|-------------|--------------------|---------|-----------|
| ±2 g | 0.06 | ±1.966 g | ±2 g | −1.7 % |
| ±4 g | 0.12 | ±3.932 g | ±4 g | −1.7 % |
| ±6 g | 0.18 | ±5.898 g | ±6 g | −1.7 % |
| ±8 g | 0.24 | ±7.864 g | ±8 g | −1.7 % |
| **±16 g** | **0.73** | **±23.92 g** | **±16 g** | **+49.5 %** |

Four of five ranges are mutually consistent (a uniform −1.7 %, i.e. the code clips just below
nominal). The ±16 g row is not: a consistent value would be **≈ 0.488 mg/LSB** (32 000 mg / 65536).
`0.73` is ≈ 1.5× that.

**Interpretation:** either the printed value is correct and ±16 g uses a different internal scaling
than the simple 16-bit model, or it is a datasheet/transcription error. **Do not use the ±16 g range
for quantitative work without per-unit characterization.** Verify the printed table in the PDF and
confirm on a tilt table or shaker. This does not affect ±2 g operation.

### 4.4 Self-test principle `[DS]`

Self-test bits apply an electrostatic force simulating a known acceleration.
`OUTPUT(ST=01) = real acceleration + test force`, therefore
`self_test_delta = OUTPUT(ST=01) − OUTPUT(ST=00)`, compared against the `ST` rows above.
`ST2:ST1 = 11` is **not allowed**. Negative self-test (`10`) is defined as a mode, but the datasheet
gives **no numeric delta for it** `[BENCH]` — do not assume exact symmetry.

---

## 5. Digital interfaces `[DS]`

Registers are accessible via I²C or SPI; the pins are shared.
**`CS` high = I²C enabled / SPI idle. `CS` low = SPI active.**

### 5.1 I²C

- Slave only. Standard mode (100 kHz) and fast mode (400 kHz).
- 7-bit slave address `SAD = 00111xxb`, `xx` set by the `SEL/SDO` pin:
  - `SEL/SDO` tied to supply (**high**): `SAD = 0011101b = 0x1D`
  - `SEL/SDO` tied to GND (**low**): `SAD = 0011110b = 0x1E`
- **Full 8-bit address bytes on the wire** (`(SAD << 1) | R/W`):

  | `SEL` | 7-bit `SAD` | Write byte | Read byte |
  |-------|-------------|-----------|-----------|
  | high | `0x1D` | **`0x3A`** | **`0x3B`** |
  | low | `0x1E` | **`0x3C`** | **`0x3D`** |

  *(E1: earlier revisions had these two rows swapped.)*
- Two devices can share the bus using the two addresses.
- MSB first; acknowledge mandatory after each byte.

```
Write one byte:
  ST | SAD+W | SAK | SUB | SAK | DATA | SAK | SP

Write multiple bytes (auto-increment if ADD_INC = 1):
  ST | SAD+W | SAK | SUB | SAK | DATA | SAK | DATA | SAK | SP

Read one byte:
  ST | SAD+W | SAK | SUB | SAK | SR | SAD+R | SAK | DATA | NMAK | SP

Read multiple bytes (auto-increment if ADD_INC = 1):
  ST | SAD+W | SAK | SUB | SAK | SR | SAD+R | SAK | DATA | MAK | DATA | MAK | DATA | NMAK | SP
```

`SUB` = sub-address byte: the 7 LSBs are the register address. Address-increment behaviour is
defined by the **`ADD_INC` bit of `CTRL_REG6`**, *not* by a bit inside `SUB`.

### 5.2 SPI

- Slave. **4-wire:** `CS`, `SPC`, `SDI`, `SDO`. **3-wire:** `CS`, `SPC`, `SDI/SDO` (`SIM = 1`).
- **Max clock 10 MHz.**
- `CS` low at start of transfer, high at end. `SPC` is held high while `CS` is high.
- Data is **driven on the falling edge** of `SPC` and **captured on the rising edge**
  ⇒ **SPI mode 3 (`CPOL = 1`, `CPHA = 1`)**. MSB first.
- Single transfer = 16 clock pulses; multiple-byte transfer = `16 + N × 8` pulses.

**Command byte (transmitted MSB first):**

| Bit | Field | Meaning |
|-----|-------|---------|
| **7** | `RW` | **1 = read, 0 = write** |
| **6:0** | `AD6..AD0` | 7-bit register address (`0x00 … 0x7F`) |

```
read  command byte = 0x80 | (addr & 0x7F)
write command byte =         addr & 0x7F
```

*(E5: earlier revisions numbered these bits in transmission order and called bit7 "bit 0".)*

- Second byte: `DI7..DI0` (write) or `DO7..DO0` (read).
- Further bytes: additional data. The address auto-increments **only if `ADD_INC = 1`**
  (`CTRL_REG6` bit 4). With `ADD_INC = 0` the same address is repeated for every byte.
- **There is no MS / auto-increment bit inside the SPI command byte on this device.**
  `[DERIVED]` This is structurally necessary: the register map reaches `0x7F`, so 7 address bits
  plus `RW` fill the byte exactly — there is no room for an MS bit.
- 3-wire mode: `SIM = 1` (`CTRL_REG5` bit 0); `SDI` becomes bidirectional `SDI/SDO`, the data phase
  is driven by the device on the same line.

**SPI timing (characterization, 10 MHz, 3- and 4-wire)** `[DS]`

| Parameter | Value |
|-----------|-------|
| Clock cycle | min 100 ns |
| `CS` setup | 6 ns |
| `CS` hold | 8 ns |
| `SDI` setup | 5 ns |
| `SDI` hold | 15 ns |
| `SDO` valid | max 50 ns |
| `SDO` hold | min 9 ns |
| `SDO` disable | max 50 ns |

> **F8 `[BENCH]`** — two gaps: (a) **no minimum `CS`-high time between consecutive frames** is given,
> which directly limits how fast N separate per-sample transfers can be issued; (b) the
> characterization voltage for this table is not stated, so 10 MHz operation at `Vdd_IO` = 1.8 V is
> unconfirmed. Measure the inter-frame gap floor before relying on per-sample readout.

When no communication is ongoing, `SDO` is driven by an internal pull-up. If the SPI bus is shared
with other slaves, verify bus contention / leakage on `SDO`. `[DS]`

**I²C timing** `[DS]`: `SCL` max 100 kHz / 400 kHz; `SCL` low 4.7 µs / 1.3 µs; `SCL` high 4.0 µs /
0.6 µs; `SDA` setup 250 ns / 100 ns; bus free between STOP and START 4.7 µs / 1.3 µs.

---

## 6. Register map (complete) `[DS]`

Type: `r` = read only, `w` = write only, `r/w` = read/write.

> **Addresses not listed** — e.g. `0x00…0x0B`, `0x26`, `0x30…0x3F`, `0x58` — are **reserved: do not
> write; doing so may permanently damage the device.**
> Registers loaded at boot contain factory calibration: **do not modify.**

| Address | Name | Type | Default | Description |
|---------|------|------|---------|-------------|
| `0x0C` | `OUT_T` | r | — | Temperature output |
| `0x0D` | `INFO1` | r | `0x21` | Information register 1 |
| `0x0E` | `INFO2` | r | `0x00` | Information register 2 |
| `0x0F` | `WHO_AM_I` | r | `0x3F` | Device ID |
| `0x10` | `OFF_X` | r/w | `0x00` | X-axis offset correction |
| `0x11` | `OFF_Y` | r/w | `0x00` | Y-axis offset correction |
| `0x12` | `OFF_Z` | r/w | `0x00` | Z-axis offset correction |
| `0x13` | `CS_X` | r/w | `0x00` | Constant shift X (state machine only) |
| `0x14` | `CS_Y` | r/w | `0x00` | Constant shift Y (state machine only) |
| `0x15` | `CS_Z` | r/w | `0x00` | Constant shift Z (state machine only) |
| `0x16` | `LC_L` | r/w | `0x01` | Long counter low |
| `0x17` | `LC_H` | r/w | `0x00` | Long counter high |
| `0x18` | `STAT` | r | — | Interrupt status / synchronization |
| `0x19` | `PEAK1` | r | — | Peak value SM1 |
| `0x1A` | `PEAK2` | r | — | Peak value SM2 |
| `0x1B` | `VFC_1` | r/w | — | Vector filter coefficient 1 |
| `0x1C` | `VFC_2` | r/w | — | Vector filter coefficient 2 |
| `0x1D` | `VFC_3` | r/w | — | Vector filter coefficient 3 |
| `0x1E` | `VFC_4` | r/w | — | Vector filter coefficient 4 |
| `0x1F` | `THRS3` | r/w | — | Threshold value 3 |
| `0x20` | `CTRL_REG4` | r/w | `0x07` | ODR, BDU, axis enable |
| `0x21` | `CTRL_REG1` | r/w | `0x00` | SM1 control |
| `0x22` | `CTRL_REG2` | r/w | `0x00` | SM2 control |
| `0x23` | `CTRL_REG3` | r/w | `0x00` | Interrupt control, vector filter, soft reset |
| `0x24` | `CTRL_REG5` | r/w | `0x00` | BW, full scale, self-test, SPI mode |
| `0x25` | `CTRL_REG6` | r/w | `0x10` | Boot, FIFO, address increment, interrupt routing |
| `0x27` | `STATUS` | r | — | Data status / overrun |
| `0x28` | `OUT_X_L` | r | `0x00` | X output low |
| `0x29` | `OUT_X_H` | r | `0x00` | X output high |
| `0x2A` | `OUT_Y_L` | r | `0x00` | Y output low |
| `0x2B` | `OUT_Y_H` | r | `0x00` | Y output high |
| `0x2C` | `OUT_Z_L` | r | `0x00` | Z output low |
| `0x2D` | `OUT_Z_H` | r | `0x00` | Z output high |
| `0x2E` | `FIFO_CTRL` | r/w | `0x00` | FIFO mode and watermark |
| `0x2F` | `FIFO_SRC` | r | — | FIFO status |
| `0x40–0x4F` | `ST1_1..ST1_16` | w | — | SM1 code registers (16 steps) |
| `0x50` | `TIM4_1` | w | — | SM1 timer 4 (8-bit) |
| `0x51` | `TIM3_1` | w | — | SM1 timer 3 (8-bit) |
| `0x52–0x53` | `TIM2_1` | w | — | SM1 timer 2 (16-bit) |
| `0x54–0x55` | `TIM1_1` | w | — | SM1 timer 1 (16-bit) |
| `0x56` | `THRS2_1` | w | — | SM1 threshold 2 |
| `0x57` | `THRS1_1` | w | — | SM1 threshold 1 |
| `0x59` | `MASK1_B` | w | — | SM1 axis/sign mask (swap) |
| `0x5A` | `MASK1_A` | w | — | SM1 axis/sign mask (default) |
| `0x5B` | `SETT1` | w | — | SM1 detection settings |
| `0x5C` | `PR1` | r | — | SM1 program / reset pointer |
| `0x5D–0x5E` | `TC1` | r | — | SM1 timer counter output (16-bit) |
| `0x5F` | `OUTS1` | r | — | SM1 output flags |
| `0x60–0x6F` | `ST2_1..ST2_16` | w | — | SM2 code registers (16 steps) |
| `0x70` | `TIM4_2` | w | — | SM2 timer 4 (8-bit) |
| `0x71` | `TIM3_2` | w | — | SM2 timer 3 (8-bit) |
| `0x72–0x73` | `TIM2_2` | w | — | SM2 timer 2 (16-bit) |
| `0x74–0x75` | `TIM1_2` | w | — | SM2 timer 1 (16-bit) |
| `0x76` | `THRS2_2` | w | — | SM2 threshold 2 |
| `0x77` | `THRS1_2` | w | — | SM2 threshold 1 |
| `0x78` | `DES2` | w | — | SM2 decimation factor |
| `0x79` | `MASK2_B` | w | — | SM2 axis/sign mask (swap) |
| `0x7A` | `MASK2_A` | w | — | SM2 axis/sign mask (default) |
| `0x7B` | `SETT2` | w | — | SM2 detection settings |
| `0x7C` | `PR2` | r | — | SM2 program / reset pointer |
| `0x7D–0x7E` | `TC2` | r | — | SM2 timer counter output (16-bit) |
| `0x7F` | `OUTS2` | r | — | SM2 output flags |

---

## 7. Register descriptions

### 7.1 `OUT_T` (0x0C), read only `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | Temp7 | Temp6 | Temp5 | Temp4 | Temp3 | Temp2 | Temp1 | Temp0 |

8-bit temperature, **1 LSB per degree**, two's complement (`int8`).
**No absolute reference / offset is specified** — this is a *relative* temperature reading only.
Use it for drift correlation, never as an absolute thermometer.

### 7.2 `INFO1` (0x0D), read only `[DS]`
Default `0x21` = `0010 0001`. No field description in the datasheet.

### 7.3 `INFO2` (0x0E), read only `[DS]`
Default `0x00`. No field description in the datasheet.

### 7.4 `WHO_AM_I` (0x0F), read only `[DS]`
Fixed `0x3F` = `0011 1111`. Use as the presence / sanity check in the driver.

### 7.5–7.7 `OFF_X` (0x10), `OFF_Y` (0x11), `OFF_Z` (0x12), read/write `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | OFFx_7 | OFFx_6 | OFFx_5 | OFFx_4 | OFFx_3 | OFFx_2 | OFFx_1 | OFFx_0 |

Signed 8-bit per-axis offset correction, default `0x00`.
**The LSB weight of the `OFF_` registers is not specified** `[BENCH]` — characterize experimentally
and record the result in the application's data header; never assume a weight.

### 7.8–7.10 `CS_X` (0x13), `CS_Y` (0x14), `CS_Z` (0x15), read/write `[DS]`

Signed 8-bit constant shift per axis, default `0x00`. **State-machine use only**, selected by the
`D_CS` bit in `SETT2`. LSB weight unspecified.

### 7.11 `LC_L` (0x16), `LC_H` (0x17), read/write `[DS]`

16-bit long counter for state-machine program timing. `LC_L` = low byte (map default `0x01`),
`LC_H` = high byte (default `0x00`).

| Value | Meaning |
|-------|---------|
| `0x0001` | counting stopped |
| `0x0000` | counter expired, interrupt available, counter reloaded |
| `> 0x0001` | counting in progress |

The `LONG` flag in `STAT` (0x18) reports the long-counter interrupt (common to both state machines).
*Default-value discrepancy: map says `0x01`, §7.11 prose says `0x00` — use `0x0001` as the
"counting stopped" default.*

### 7.12 `STAT` (0x18), read only `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | LONG | SYNCW | SYNC1 | SYNC2 | INT_SM1 | INT_SM2 | DOR | DRDY |

- `LONG`: long-counter interrupt flag, common to both SMs
- `SYNCW`: 0 = no host action pending; 1 = host action based on output data pending
- `SYNC1`: 1 = SM1 stopped, awaiting restart request from SM2
- `SYNC2`: 1 = SM2 stopped, awaiting restart request from SM1
- `INT_SM1`, `INT_SM2`: **status flags** (the PDF wording says "enable/disable" — see §11)
- `DOR`: data overrun — unread data in the output registers when the next sample starts.
  *"Bit is reset when the next sample is ready"* — clear/latch semantics are ambiguous `[BENCH]`
- `DRDY`: 1 = data ready in the output registers

> For **FIFO acquisition**, `STAT` is not the authoritative source. Use `STATUS` (0x27) and
> `FIFO_SRC` (0x2F) — see §11.

### 7.13–7.14 `PEAK1` (0x19), `PEAK2` (0x1A), read only `[DS]`
Signed 8-bit peak value detected for the next SM1 / SM2 condition. Default `0x00`.
Enabled by `P_DET` in `SETT1` / `SETT2`.

### 7.15–7.18 `VFC_1..VFC_4` (0x1B–0x1E), read/write `[DS]`
8-bit vector-filter coefficients. `VFC_1/2/4` feed the difference filter, `VFC_3` feeds the SM2
filter. Default `0x00`. The vector filter is enabled by `VFILT` in `CTRL_REG3`.

### 7.19 `THRS3` (0x1F), read/write `[DS]`
8-bit threshold value 3, default `0x00`. Used as the limit value for axis/sign-mask reset —
see `THR3_SA` and `THR3_MA` in `SETT1` / `SETT2`.

### 7.20 `CTRL_REG4` (0x20), read/write, default `0x07` `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | ODR3 | ODR2 | ODR1 | ODR0 | BDU | Zen | Yen | Xen |

- `ODR3..ODR0`: output data rate **and power-mode selection**, default `0000` (power down)
- `BDU`: block data update, default 0. **0 = continuous update; 1 = output registers not updated
  until both MSB and LSB have been read.** Use `BDU = 1` to avoid mixing samples on byte-wise reads
- `Zen`, `Yen`, `Xen`: axis enables, default 1 each

| ODR3..0 | ODR | Power mode |
|---------|-----|------------|
| `0000` | Power down | — |
| `0001` | 3.125 Hz | low-power (per §3 current row) |
| `0010` | 6.25 Hz | *unspecified* — **F7** |
| `0011` | 12.5 Hz | *unspecified* — **F7** |
| `0100` | 25 Hz | *unspecified* — **F7** |
| `0101` | 50 Hz | *unspecified* — **F7** |
| `0110` | 100 Hz | *unspecified* — **F7** (noise spec claims "normal mode" here) |
| `0111` | 400 Hz | *unspecified* — **F7** |
| `1000` | 800 Hz | *unspecified* — **F7** |
| `1001` | **1600 Hz** | normal (225 µA row in §3) |
| `1010…1111` | **not documented — do not use** | — |

**Worked values** `[DERIVED]`: `0x9F` = ODR 1600 Hz + BDU=1 + XYZ. `0x97` is the same **with BDU = 0**
(E2). Power down with BDU=1 and axes enabled = `0x0F`; the reset default `0x07` is power down with
BDU=0.

### 7.21 `CTRL_REG1` (0x21), read/write, default `0x00` — SM1 control `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | HYST2_1 | HYST1_1 | HYST0_1 | — | SM1_PIN | — | — | SM1_EN |

- `HYST2_1..HYST0_1`: unsigned hysteresis added to / subtracted from the SM1 threshold. Default 000.
  **LSB weight unspecified.**
- `SM1_PIN`: 0 = SM1 interrupt routed to **INT1**; 1 = routed to **INT2**
- `SM1_EN`: 0 = disabled, 1 = enabled
- Bits 4, 2, 1: reserved, keep 0

### 7.22 `CTRL_REG2` (0x22), read/write, default `0x00` — SM2 control `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | HYST2_2 | HYST1_2 | HYST0_2 | — | SM2_PIN | — | — | SM2_EN |

Same semantics as SM1. **`SM2_PIN`: 0 = INT1, 1 = INT2** — the PDF prints "INT1" for both values
(known typo); behaviour is symmetric to `SM1_PIN`. Bits 4, 2, 1 reserved.

### 7.23 `CTRL_REG3` (0x23), read/write, default `0x00` `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | DR_EN | IEA | IEL | INT2_EN | INT1_EN | VFILT | — | STRT |

- `DR_EN`: DRDY signal connected to INT1. 0 = not connected, 1 = connected
- **`IEA`: interrupt polarity.** This document states **0 = active LOW, 1 = active HIGH** —
  **`[PDF-CHECK]` / `[BENCH]`, see §11 E8.** ST's original field title is *"IEA — Interrupt active
  low"*, which by ST's usual convention implies the opposite mapping. **Do not lock the EXTI edge
  from this table alone.**
- **`IEL`: interrupt latching. 0 = latched, 1 = pulsed.** *Inverted with respect to LIS3DH /
  LSM6DS3 — a classic porting trap.*
- `INT2_EN`: 0 = INT2 disabled, 1 = enabled
- `INT1_EN`: 0 = INT1/DRDY disabled, 1 = enabled. *The PDF description is mislabelled
  "Interrupt 2 enable/disable"; it actually enables the INT1/DRDY pin.*
- `VFILT`: 0 = vector filter disabled, 1 = enabled
- `STRT`: **soft reset, POR function.** 0 = no reset, 1 = reset. Not documented as self-clearing.
- Bit 1: reserved

### 7.24 `CTRL_REG5` (0x24), read/write, default `0x00` `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | BW2 | BW1 | FSCALE2 | FSCALE1 | FSCALE0 | ST2 | ST1 | SIM |

**Anti-aliasing bandwidth (`BW2..BW1`), default `00`:**

| Code | Bandwidth |
|------|-----------|
| `00` | **800 Hz** |
| `01` | **400 Hz** |
| `10` | **200 Hz** |
| `11` | **50 Hz** |

*(E4: earlier revisions had `01` and `10` swapped. Corroboration: ST's STM32F4Discovery LIS3DSH BSP
defines `AA_BW_400 = 0x40` (`01`) and `AA_BW_200 = 0x80` (`10`). Confirm against the PDF table if
200/400 Hz is ever used.)*

**Full scale (`FSCALE2..0`), default `000`:** `000` = ±2 g, `001` = ±4 g, `010` = ±6 g,
`011` = ±8 g, `100` = ±16 g (**see F1 before using**). Other codes not documented — do not use.

**Self-test (`ST2..ST1`), default `00`:**

| ST2 | ST1 | Mode |
|-----|-----|------|
| 0 | 0 | Normal |
| 0 | 1 | Positive-sign self-test |
| 1 | 0 | Negative-sign self-test |
| 1 | 1 | **Not allowed** |

`SIM`: 0 = 4-wire SPI, 1 = 3-wire.

### 7.25 `CTRL_REG6` (0x25), read/write, default `0x10` `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | BOOT | FIFO_EN | WTM_EN | ADD_INC | P1_EMPTY | P1_WTM | P1_OVERRUN | P2_BOOT |

- `BOOT`: force reboot of internal logic / trimming reload. Active high, **cleared automatically**
  when the reboot finishes.
- `FIFO_EN`: 0 = FIFO disabled, 1 = enabled
- `WTM_EN`: enable use of the `WTMP` watermark level
- `ADD_INC`: register address auto-increment during multi-byte access (I²C **or** SPI).
  **Default 1.**
- `P1_EMPTY`: FIFO-empty indication on INT1
- `P1_WTM`: FIFO-watermark interrupt on INT1
- `P1_OVERRUN`: FIFO-overrun interrupt on INT1
- `P2_BOOT`: BOOT interrupt on INT2

> **F3 `[DERIVED]` — write-whole-register trap.** Setting `BOOT` by writing `0x80` simultaneously
> **clears `ADD_INC` and `FIFO_EN`**. While `BOOT` is pending, multi-byte reads will not
> auto-increment; after `BOOT` self-clears, the register reads `0x00`. **Always restore
> `CTRL_REG6` (`0x10` or `0x74`) after the boot completes.** Polling `BOOT` is a single-byte read
> and is unaffected.

**Worked values** `[DERIVED]`: `0x10` = ADD_INC only (reset default).
`0x74` = `FIFO_EN | WTM_EN | ADD_INC | P1_WTM` — FIFO watermark routed to INT1.

### 7.26 `STATUS` (0x27), read only `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | ZYXOR | ZOR | YOR | XOR | ZYXDA | ZDA | YDA | XDA |

- `ZYXOR`: a new set of data has overwritten the previous set (X/Y/Z overrun)
- `ZOR` / `YOR` / `XOR`: per-axis overwrite
- `ZYXDA`: a new X/Y/Z set is available; `ZDA`/`YDA`/`XDA`: per-axis data available
- All default 0

### 7.27–7.29 `OUT_X_L/H` (0x28/0x29), `OUT_Y_L/H` (0x2A/0x2B), `OUT_Z_L/H` (0x2C/0x2D), read only `[DS]`

Signed 16-bit two's complement, **little endian** (low byte at the lower address), default `0x00`.

**Read semantics:**
- With `BDU = 1` the registers hold their value until **both** bytes are read.
- A 6-byte burst from `0x28` returns `XL, XH, YL, YH, ZL, ZH` (requires `ADD_INC = 1`).
- **Bypass mode** (FIFO off/empty): reads return the **current output-register sample**.
  *(E6: earlier revisions wrongly listed Bypass as returning a stored FIFO sample.)*
- **FIFO / Stream mode:** reads return the **oldest stored FIFO sample**; after each 6-byte sample
  read the next oldest sample is loaded into the output registers.

> **F4 `[BENCH]` — the single riskiest unspecified behaviour.** The text says the *next* sample is
> loaded after each 6-byte group, but it **never states that the address counter wraps
> `0x2D → 0x28`** inside one `CS`-low frame. Therefore a single burst of `1 + 6N` bytes for N > 1
> samples is **not guaranteed**: the address may run past `0x2D` into `0x2E` (`FIFO_CTRL`) and
> return garbage. Two legal driver designs exist and the choice must be made by measurement
> (§12, check 3):
> - **Mode A** — one `CS`-low burst of `1 + 6N` bytes (fast, fewer frames) — *only if wrap is proven*;
> - **Mode B** — N separate 7-byte transfers with `CS` asserted per sample (always correct per the
>   documented framing, but bounded by the unspecified minimum `CS`-high time, **F8**).

> **F10 `[BENCH]`** — behaviour after an **aborted or partial** 6-byte read in FIFO mode (e.g. a DMA
> transfer killed mid-frame) is unspecified: with `BDU = 1` the register/FIFO pop state may be left
> inconsistent. The only documented recovery is a FIFO resync (Bypass → desired mode). Treat any
> aborted read as a resync trigger, and count the discarded samples as loss.

### 7.30 `FIFO_CTRL` (0x2E), read/write, default `0x00` `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | FMODE2 | FMODE1 | FMODE0 | WTMP4 | WTMP3 | WTMP2 | WTMP1 | WTMP0 |

- `FMODE2..0`: mode selection (table below)
- `WTMP4..0`: watermark level, **`0…31`**. **F12:** `WTMP = 0` behaviour is undefined `[BENCH]`.
- FIFO trigger is the **INT2** source (see F11).

| FMODE2..0 | Mode |
|-----------|------|
| `000` | **Bypass** — FIFO turned off |
| `001` | **FIFO** — stops collecting when full |
| `010` | **Stream** — when full, the new sample overwrites the oldest |
| `011` | Stream until trigger de-asserted, then FIFO |
| `100` | Bypass until trigger de-asserted, then Stream |
| `101` | Not used |
| `110` | Not used |
| `111` | Bypass until trigger de-asserted, then FIFO |

**Worked values** `[DERIVED]`: `0x48` = Stream (`010`) + `WTMP = 8`.
`0x50` = Stream + **`WTMP = 16`** (a frequent off-by-one: `0x50 & 0x1F = 16`, not 8).
`0x30` = FIFO (`001`) + `WTMP = 16`.

### 7.31 `FIFO_SRC` (0x2F), read only `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | WTM | OVRN_FIFO | EMPTY | FSS4 | FSS3 | FSS2 | FSS1 | FSS0 |

- `WTM`: **level** status. 0 = filling below watermark; 1 = filling **at or above** watermark
- `OVRN_FIFO`: 0 = FIFO not completely filled; **1 = FIFO completely filled (32 samples)**
- `EMPTY`: 1 = FIFO empty
- `FSS4..0`: number of samples stored, **`0…31`** — qualify with `EMPTY` and `OVRN_FIFO` at the
  32-sample boundary

> **F5 `[BENCH]` — three distinct unknowns, all driver-critical:**
> 1. **Width vs depth.** `FSS` is 5 bits but the FIFO is 32 deep, so **32 is not representable**.
>    At the boundary `FSS` may read `0` or `31` (wrap vs saturate is unspecified). A driver that
>    treats `FSS == 0` as "nothing to read" will **stall forever on a full FIFO**.
> 2. **Fullness ≠ loss.** The PDF wording describes *completely filled*, not *samples lost*. In
>    Stream mode loss begins with the **next** write after fullness. `OVRN_FIFO = 1` alone is
>    therefore **not proof of loss**, and the number of overwritten samples is **not tracked by
>    hardware**. Loss must be inferred from elapsed time vs captured count and reported as a
>    bounded estimate.
> 3. **Latch/clear.** Whether `OVRN_FIFO` is a live level or a latched flag, and what clears it
>    (reading samples, reading `FIFO_SRC`, or a resync) is unspecified.

### 7.32 `ST1_1..ST1_16` (0x40–0x4F), write only `[DS]`
SM1 code registers: 16 × 8-bit, one per program step. **The op-code encoding is not in this
datasheet** — see ST application note **AN3393**.

### 7.33–7.36 SM1 timers, write only `[DS]`
- `TIM4_1` (0x50), `TIM3_1` (0x51): 8-bit unsigned, default `0x00`
- `TIM2_1` (0x52 low / 0x53 high), `TIM1_1` (0x54 low / 0x55 high): 16-bit unsigned, default `0x0000`

### 7.37–7.38 `THRS2_1` (0x56), `THRS1_1` (0x57), write only `[DS]`
8-bit SM1 thresholds, default `0x00`. Signed or unsigned depending on `ABS` in `SETT1`.
*The original PDF swaps the `THRS1_1` / `THRS2_1` numbering in map comments; both are SM1 thresholds.*

### 7.39–7.40 `MASK1_B` (0x59), `MASK1_A` (0x5A), write only `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | P_X | N_X | P_Y | N_Y | P_Z | N_Z | P_V | N_V |

Each bit enables the corresponding positive/negative condition on X, Y, Z or the vector magnitude
V (1 = enabled, 0 = disabled). `MASK1_A` is the default mask, `MASK1_B` the swap mask.

### 7.41 `SETT1` (0x5B), write only `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | P_DET | THR3_SA | ABS | — | — | THR3_MA | R_TAM | SITR |

- `P_DET`: SM1 peak detection enable
- `THR3_SA`: 1 = `THRS3` is the limit value for axis/sign-mask reset of `MASK1_B`
- `ABS`: 0 = unsigned thresholds, 1 = signed
- `THR3_MA`: 1 = `THRS3` is the limit value for mask reset of `MASK1_A`
- `R_TAM`: next-condition validation flag
- `SITR`: 1 = program flow may be modified by STOP / CONT commands
- Bits 4, 3: reserved

### 7.42 `PR1` (0x5C), read only `[DS]`
`RP3..RP0` = reset pointer address (bits 7:4); `PP3..PP0` = program pointer address (bits 3:0).

### 7.43 `TC1` (0x5D low / 0x5E high), read only `[DS]`
16-bit unsigned SM1 timer output value, default `0x0000`.

### 7.44 `OUTS1` (0x5F), read only `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | P_X | N_X | P_Y | N_Y | P_Z | N_Z | P_V | N_V |

Each bit: 1 = condition shown. **Reading this register affects / acknowledges the SM1 interrupt.**

### 7.45 `ST2_1..ST2_16` (0x60–0x6F), write only `[DS]`
SM2 code registers, 16 × 8-bit. Op-codes: see AN3393.

### 7.46–7.49 SM2 timers, write only `[DS]`
`TIM4_2` (0x70), `TIM3_2` (0x71): 8-bit. `TIM2_2` (0x72/0x73), `TIM1_2` (0x74/0x75): 16-bit,
low byte first. Defaults `0x00` / `0x0000`.

### 7.50–7.51 `THRS2_2` (0x76), `THRS1_2` (0x77), write only `[DS]`
8-bit SM2 thresholds; signed/unsigned per `ABS` in `SETT2`.

### 7.52 `DES2` (0x78), write only `[DS]`
8-bit decimation counter for SM2 — lets SM2 run on decimated time instead of the full ODR.

### 7.53–7.54 `MASK2_B` (0x79), `MASK2_A` (0x7A), write only `[DS]`
Identical layout and meaning to `MASK1_B` / `MASK1_A`.

### 7.55 `SETT2` (0x7B), write only `[DS]`

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Name | P_DET | THR3_SA | ABS | RADI | D_CS | THR3_MA | R_TAM | SITR |

As `SETT1`, plus:
- `RADI`: 0 = raw data for SM2; 1 = diff (filtered) data
- `D_CS`: 0 = DIFF2 (current minus previous sample); 1 = constant shift (current minus `CS_X/Y/Z`)

### 7.56 `PR2` (0x7C), read only `[DS]`
Reset pointer (bits 7:4) / program pointer (bits 3:0) for SM2.

### 7.57 `TC2` (0x7D low / 0x7E high), read only `[DS]`
16-bit unsigned SM2 timer output value.

### 7.58 `OUTS2` (0x7F), read only `[DS]`
Same layout as `OUTS1`; reading affects / acknowledges the SM2 interrupt.

---

## 8. FIFO operation

### 8.1 Structure `[DS]`
One FIFO per output channel (X, Y, Z), **32 sample slots** each. Enable with `FIFO_EN`
(`CTRL_REG6`), select mode with `FMODE` (`FIFO_CTRL`).

### 8.2 Modes `[DS]`
- **Bypass** — FIFO off, stays empty; output registers behave as normal single-sample registers.
- **FIFO** — samples stored; watermark interrupt at the programmed level; **stops collecting when
  full**.
- **Stream** — same, but when full the **oldest** data is discarded as new data arrives.
- **Stream-to-FIFO** — runs in Stream, switches to FIFO on a trigger event (keeps data around the
  event). Trigger source is the **INT2** signal.
- Combined modes: Bypass-to-Stream, Bypass-to-FIFO (see the `FMODE` table).

### 8.3 Interrupts and status `[DS]`
`WTM_EN` enables use of the `WTMP` level. Sources routable to **INT1**: FIFO empty (`P1_EMPTY`),
watermark (`P1_WTM`), overrun (`P1_OVERRUN`). `BOOT` can be routed to **INT2** (`P2_BOOT`).
Status is read from `FIFO_SRC`: `WTM`, `OVRN_FIFO`, `EMPTY`, `FSS[4:0]`.

> **F11 `[BENCH]`** — for Stream-to-FIFO / Bypass-to-* modes the trigger is "the INT2 signal", but
> INT2 is normally an **output**. How the trigger is generated or driven (state machine, external
> signal, pin reconfiguration) is not described. Do not design around triggered FIFO modes without
> resolving this from AN3393 or the bench.

### 8.4 Data retrieval and the safe drain rule `[DS]` + `[DERIVED]`

Read `OUT_X/Y/Z` (`0x28…0x2D`). In FIFO/Stream mode each 6-byte group returns the oldest stored
sample; the next oldest is then loaded. Single and burst reads are both supported — **but see F4
for the multi-sample burst caveat.**

**Watermark re-arming** `[BENCH]`: the datasheet does not say whether `P1_WTM` pulses only on
crossing `WTMP` from below, or on every new sample while `FSS ≥ WTMP`. With **pulsed** interrupts
(`IEL = 1`) this is the difference between a working driver and a silent stall.
**Always drain strictly below `WTMP` on every event.**

**Recommended drain algorithm** `[DERIVED]` — safe under every documented and undocumented case:

```
on watermark event (or periodic poll):
    src = read(FIFO_SRC)

    if (src.OVRN_FIFO):                 /* full: FSS may read 0 or 31 — never trust it alone */
        handle_overrun()                /* bounded loss estimate + resync; see below */
        return

    if (src.EMPTY || src.FSS == 0):
        return                          /* never read more than FSS reports */

    N = min(src.FSS, 31)                /* one burst can always empty a 31-sample FIFO */
    read N samples                      /* mode A or mode B, per F4 */
    pack N samples

    re-read FIFO_SRC
    if (!EMPTY && FSS >= WTMP && ++iter < LIMIT):
        repeat                          /* chain, do not busy-loop inside an ISR */
    if (iter == LIMIT && FSS >= WTMP):
        force_resync()                  /* escalate: never leave the FIFO above the watermark */
```

**Overrun handling** `[DERIVED]`:
1. Estimate the deficit from elapsed time since the last successful drain minus samples captured;
   bound it by one FIFO depth (32 samples = 20 ms at 1600 Hz) per event.
2. Count the event and the estimate separately, so the host can compute an uncertainty interval.
3. **Resync:** set `FMODE = 000` (Bypass), then re-select the desired mode. Count the discarded
   FIFO content as loss too.
4. Continuing silently is forbidden — an undeclared gap is worse than a declared one.

### 8.5 What the FIFO does *not* give you `[DERIVED]`
- No hardware count of overwritten samples in Stream mode.
- No timestamp per sample.
- No indication of *which* samples were lost.
- No guarantee that a single `CS`-low frame can span more than one sample (F4).

---

## 9. State machines (functional summary) `[DS]`

Two independent machines (SM1, SM2), each running a user program of up to **16 steps** loaded into
`ST1_x` (0x40–0x4F) / `ST2_x` (0x60–0x6F). From each state the machine branches to *n+1* (NEXT
condition true) or to the reset state (RESET condition true). Reaching an output/stop/continue
state raises an interrupt.

Supported algorithms: gesture recognition, free-fall, wake-up, 4D/6D orientation, pulse and step
counting, click / double-click, shake / double-shake, face-up / face-down, turn / double-turn.

**Resources per SM:** thresholds `THRS1_x`, `THRS2_x` (+ shared `THRS3`); hysteresis `HYST` bits in
`CTRL_REG1/2`; axis/sign masks `MASKx_A` (default) and `MASKx_B` (swap); settings `SETT1/2` (peak
detection, signed/unsigned, mask reset, flow control); timers `TIM1_x`, `TIM2_x` (16-bit),
`TIM3_x`, `TIM4_x` (8-bit) with outputs `TC1/2`; shared long counter `LC`; decimation `DES2`
(SM2 only); SM2 data source `RADI` (raw/diff) and `D_CS` (diff vs constant shift); status `STAT`,
`PR1/2`, `OUTS1/2`, `PEAK1/2`.

Enable with `SM1_EN` / `SM2_EN`; route with `SM1_PIN` / `SM2_PIN` and `INT1_EN` / `INT2_EN`.

> The instruction op-code set is **not** in this datasheet — see **AN3393**.

---

## 10. Driver implementation notes `[DERIVED]`

*Everything in this section is written for driver authors. It is derived from the register
definitions, not literal datasheet text.*

### 10.1 Register access helpers

```c
/* Bytes must be uint8_t. Cast through uint16_t before the signed cast:
   if the high byte were int8_t, sign extension would corrupt the result. */
static inline int16_t le16(uint8_t lo, uint8_t hi) {
    return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
}
```

- **SPI read:** `CS` low → send `(0x80 | addr)` → read N bytes → `CS` high.
- **SPI write:** `CS` low → send `(addr & 0x7F)` → write N bytes → `CS` high.
- Multi-byte transfers require **`ADD_INC = 1`** (`CTRL_REG6` bit 4, default 1).
- **SPI mode 3** (`CPOL=1, CPHA=1`), MSB first, **≤ 10 MHz**.
- **I²C:** `0x1D` (`SEL` high → `0x3A` write / `0x3B` read) or `0x1E` (`SEL` low → `0x3C` / `0x3D`);
  write the register address, then repeated-start for the read; **`CS` must be high**.

### 10.2 Initialization sequence (strict order)

> **Reset first → configure → start ODR last.**
> **F2 fix:** `FIFO_CTRL` is an explicit, mandatory step. The v1.1 sequence omitted it, which left
> `FMODE` in Bypass while `FIFO_EN`/`WTM_EN`/`P1_WTM` were enabled — a configuration that produces
> **no watermark interrupts at all** and looks like a dead sensor.

| # | Action | Notes |
|---|--------|-------|
| 1a | **Trimming reload only:** write `CTRL_REG6 = 0x80` (`BOOT = 1`), poll until `BOOT` self-clears | **F3:** this write also clears `ADD_INC`/`FIFO_EN`; step 4 restores them |
| 1b | **Full POR reset:** write `CTRL_REG3 = 0x01` (`STRT = 1`), wait **≥ 10 ms**, then write `CTRL_REG3 = 0x00` if `STRT` does not self-clear | Never combine `STRT = 1` with configuration bits in the same write — the reset erases them |
| 2 | Read `WHO_AM_I` (0x0F), expect **`0x3F`** | presence gate |
| 3 | `CTRL_REG5` (0x24): full scale + AA bandwidth + `SIM` | e.g. `0x00` = ±2 g, 800 Hz, 4-wire |
| 4 | `CTRL_REG6` (0x25): `0x10` (ADD_INC only) or **`0x74`** (`FIFO_EN \| WTM_EN \| ADD_INC \| P1_WTM`) | restore after BOOT |
| 5 | **`FIFO_CTRL` (0x2E): `(FMODE << 5) \| WTMP`** — e.g. **`0x48`** = Stream + WTMP 8 | **mandatory when FIFO is used** |
| 6 | `CTRL_REG3` (0x23): interrupt routing / polarity / latching | e.g. **`0x68`** = IEA \| IEL \| INT1_EN (pulsed, "active high" per §7.23 — **verify E8**) |
| 7 | **`CTRL_REG4` (0x20) written LAST** to start sampling: ODR + BDU + axes | e.g. **`0x9F`** = 1600 Hz, BDU=1, XYZ |
| 8 | Discard the settling window (§10.5), then wait for the first sample: poll `STATUS.ZYXDA`, or `STAT.DRDY`, or use the interrupt | |

**Interrupt configuration examples** `[DERIVED]`

| Goal | Value | Composition |
|------|-------|-------------|
| DRDY on INT1, "active high", **pulsed** | **`0xE8`** | `DR_EN \| IEA \| IEL \| INT1_EN` |
| DRDY on INT1, "active high", **latched** | `0xC8` | same with `IEL = 0` (**E3**: this was wrongly labelled "pulsed") |
| FIFO watermark on INT1, pulsed | **`0x68`** | `IEA \| IEL \| INT1_EN` |
| Latched "active low" INT1 with SM1 | `0x08` | `INT1_EN`, `IEA = 0`, `IEL = 0` |
| SM1 to INT2 | `CTRL_REG1.SM1_PIN = 1` + `CTRL_REG3 = 0x10` | `INT2_EN` |

**`CTRL_REG4` examples** `[DERIVED]`

| ODR | BDU = 1, XYZ enabled | Counter-example (BDU = 0) |
|-----|----------------------|---------------------------|
| 100 Hz | **`0x6F`** | `0x67` |
| 50 Hz | **`0x5F`** | `0x57` |
| 1600 Hz | **`0x9F`** | `0x97` |
| Power down | `0x0F` | `0x07` (reset default) |

### 10.3 Bandwidth selection
Choose **BW ≤ ODR / 2**: `00` = 800 Hz, `01` = 400 Hz, `10` = 200 Hz, `11` = 50 Hz.
Example: ±2 g with 50 Hz AA filter → `CTRL_REG5 = 0xC0`; ±2 g with 800 Hz → `0x00`.
The filter order and roll-off are **not specified**, so treat the selected bandwidth as nominal and
out-of-band attenuation as unknown.

### 10.4 Self-test procedure

1. Configure a stable ODR (e.g. 100 Hz or 400 Hz), ±2 g, `BDU = 1`; wait for settling (§10.5).
2. Average N samples (e.g. N = 32) with `ST2:ST1 = 00`.
3. Set `ST2:ST1 = 01`, **discard the settling window**, average N samples.
4. `delta = ST − normal`, compare with **140 mg (X, Y)** and **590 mg (Z)** typical.
5. Return `ST2:ST1 = 00`. **`11` is not allowed.**

Headroom check `[DERIVED]`: Z on a flat table = `1000 + 590 = 1590 mg`, below the ±2 g digital clip
limit of **1966 mg** — self-test is usable at ±2 g in that orientation. In other orientations, or
with a large static load, re-check headroom before enabling self-test.
**F9:** the ST values are typicals with no min/max, so any pass/fail band is a project decision.

### 10.5 Settling and timing constants — **F6**

The datasheet specifies **no** turn-on time, reset/boot delay, ODR-change settling, self-test
settling, or AA-filter group delay.

> The v1.1 guidance "≥ 4/ODR" is a lower bound on **when the first sample appears**, *not* on filter
> or self-test settling. At 1600 Hz it evaluates to **2.5 ms**, which is not a safe discard window.

**Conservative floors for driver code** `[DERIVED]` — replace with measured values from §12:

| Event | Conservative wait / discard | Rationale |
|-------|-----------------------------|-----------|
| After `STRT` or `BOOT` | **≥ 10 ms**, and poll `BOOT` self-clear / `WHO_AM_I` | boot + trimming reload |
| After ODR start or ODR change | **discard ≥ 100 ms** of samples (≥ 160 samples at 1600 Hz) | filter + internal restart |
| After self-test enable/disable | **discard ≥ 100 ms**, then average | electrostatic force + filter settling |
| After FIFO resync (Bypass → mode) | discard the FIFO content and **≥ 1 ODR period** | pipeline refill |

Record the chosen constants in the application's data header so the host knows what was discarded.

### 10.6 Temperature
Read `OUT_T` (0x0C) as `int8`, **1 LSB/°C, relative only** — no absolute offset is specified.

---

## 11. Errata, ambiguities and missing specifications

**Consolidated register.** Items E1–E7 are **closed** in v2.0 (see §0.4). Everything below is
**open** and must be closed by PDF reading or bench measurement.

| ID | Subject | What is wrong / missing | Status | Closure method |
|----|---------|-------------------------|--------|----------------|
| **E8** | **`CTRL_REG3.IEA` polarity** | This document says 0 = active LOW, 1 = active HIGH. ST's field title *"IEA — Interrupt active low"* implies the opposite. **The EXTI edge choice depends on it.** | `[PDF-CHECK]` + `[BENCH]` | PDF table for `CTRL_REG3` **and** scope INT1 idle/active levels with `IEA = 0` and `IEA = 1` |
| **F1** | **±16 g sensitivity** | `0.73 mg/LSB` implies ±23.9 g on a 16-bit code; all other ranges agree within −1.7 %. A consistent value would be ≈ 0.488 mg/LSB | `[PDF-CHECK]` + `[BENCH]` | PDF sensitivity table; tilt/shaker characterization if ±16 g is used |
| **F4** | **Burst address wrap** | Whether `ADD_INC` wraps `0x2D → 0x28` inside one `CS`-low frame is **not stated**; decides mode A vs mode B | `[BENCH]` | §12 check 3 |
| **F5** | **`FSS` / `OVRN_FIFO` at 32** | 5-bit `FSS` cannot encode 32; wrap vs saturate unknown; `OVRN_FIFO` is a *fullness* flag, latch/clear semantics unknown; overwritten-sample count not tracked | `[BENCH]` | §12 check 6 |
| **F6** | **Timing constants** | Turn-on, reset/boot, ODR-change, self-test settling, AA group delay all absent; "≥ 4/ODR" is not a settling spec | `[BENCH]` | §12 checks 7, 9 |
| **F7** | **Normal / low-power per ODR code** | `CTRL_REG4` says "power mode selection" but no per-code annotation exists; noise spec claims normal mode at 100 Hz | `[PDF-CHECK]` + `[BENCH]` | PDF ODR table/notes; measure `Idd` at each ODR |
| **F8** | **SPI inter-frame timing** | Minimum `CS`-high time between frames not given; characterization voltage for the 10 MHz table not stated | `[PDF-CHECK]` + `[BENCH]` | PDF SPI timing table; sweep the inter-frame gap |
| **F9** | **Typical-only mechanical specs** | No min/max for `So`, `TyOff`, `ST` ⇒ no datasheet acceptance limits | `[DS]` fact, `[DERIVED]` consequence | choose and document a project band |
| **F10** | **Aborted 6-byte FIFO read** | BDU + FIFO pop state after a partial/aborted frame is unspecified | `[BENCH]` | §12 check 12 |
| **F11** | **INT2 as FIFO trigger** | Trigger source for Stream-to-FIFO / Bypass-to-* modes is "INT2", normally an output; driving mechanism unspecified | `[PDF-CHECK]` + AN3393 | AN3393 + bench |
| **F12** | **`WTMP = 0`** | Behaviour undefined | `[BENCH]` | never use 0; verify if needed |
| E9 | `IEL` / `INT1_EN` wording | `IEL`: 0 = latched, 1 = pulsed (**inverted** vs LIS3DH/LSM6DS3). `INT1_EN` description mislabelled "Interrupt 2" | `[DS]` errata, resolved in text | — |
| E10 | `CTRL_REG2.SM2_PIN` | PDF prints "INT1" for both values; correct is 0 = INT1, 1 = INT2 | `[DS]` errata, resolved in text | — |
| E11 | `STAT` vs `STATUS` | `STAT.INT_SM1/2` are status flags despite "enable/disable" wording. **For FIFO acquisition, `STATUS` (0x27) and `FIFO_SRC` (0x2F) are authoritative** | resolved in text | — |
| E12 | `STAT.DOR` clearing | "reset when the next sample is ready" — read-to-clear vs auto-clear ambiguous | `[BENCH]` | observe during forced overrun |
| E13 | `LC_L` default | Map says `0x01`, prose says `0x00`; use `0x0001` as "counting stopped" | `[DS]` inconsistency, documented | — |
| E14 | `THRS1_1` / `THRS2_1` | PDF map comments swap the numbering; both are SM1 thresholds | `[DS]` errata, documented | — |
| E15 | Unspecified LSB weights | `OFF_X/Y/Z`, `CS_X/Y/Z`, `THRSx`, `HYST` weights not given | `[BENCH]` | characterize; record in the data header |
| E16 | Relative temperature | `OUT_T` has no absolute offset | `[DS]` fact | treat as relative only |
| E17 | ODR accuracy / drift | Not specified anywhere | `[BENCH]` | measure against an external reference (e.g. LSE RTC) at runtime |
| E18 | Negative self-test delta | `ST = 10` defined as a mode but no numeric delta given | `[BENCH]` | measure; do not assume symmetry |

**Hardware protection rules** `[DS]` — restated because they are irreversible:
- Addresses absent from the map are reserved; **writing them may permanently damage the device**.
- Factory calibration registers loaded at boot **must not be changed**.
- ODR codes `1010…1111` and `FSCALE` codes `101…111` are undocumented — **do not use**.

---

## 12. Bench verification checklist

Each check lists what it closes. Record results in the sign-off register (§0.3).

| # | Check | Method | Pass criterion | Closes |
|---|-------|--------|----------------|--------|
| 1 | **Presence / single read** | `CS` low, send `0x8F`, read 1 byte | returns `0x3F` | basic SPI framing, mode 3 |
| 2 | **Auto-increment proof** | 6-byte burst `0xA8` with `ADD_INC = 1`, then with `ADD_INC = 0` | with 1: six distinct axis bytes forming a plausible XYZ triple; with 0: the same register repeated | `ADD_INC` semantics, §5.2 |
| 3 | **Multi-sample burst wrap** | Capture `1 + 6N` bytes (N = 8) in one `CS`-low frame; compare with N separate 6-byte frames drained back-to-back | identical sample sequences ⇒ **mode A legal**; divergence/garbage after the first sample ⇒ **mode B mandatory** | **F4** |
| 4 | **INT1 polarity & pulse** | Scope INT1 with `IEA = 0` and `IEA = 1`, `IEL = 1` and `IEL = 0`; record idle level, active level, pulse width | documented mapping matches silicon; pulse width ≥ host EXTI minimum | **E8**, `IEL` |
| 5 | **Watermark re-arm** | Drain only part of the FIFO so `FSS` stays ≥ `WTMP`; wait; then drain fully | does a new pulse appear while above `WTMP`? after full drain? | watermark re-arm rule (§8.4) |
| 6 | **Forced FIFO overrun** | Stop draining for > 20 ms at 1600 Hz; log `FSS`, `OVRN_FIFO`, `EMPTY` at 31 / 32 / 33+ samples; then read one sample and re-log | determines wrap vs saturate, and whether `OVRN_FIFO` latches or is a live level | **F5**, E12 |
| 7 | **Self-test deltas + settling curve** | Run §10.4 at 100 Hz and at 1600 Hz, logging every sample after `ST` changes (not just the average) | deltas ≈ 140 / 590 mg; the curve shows the actual settling time | **F6**, **F9**, E18 |
| 8 | **ODR accuracy** | Count captured samples against an LSE RTC gate over ≥ 1 h; repeat over temperature if possible | publish ppm; drift bounded | **E17** |
| 9 | **Turn-on / reset timing** | Scope from `CTRL_REG4` write to first `ZYXDA`; from `STRT` to a valid `WHO_AM_I` read | replace the conservative floors with measured values | **F6** |
| 10 | **Minimum `CS`-high gap** | In mode B, shrink the inter-frame gap until data corrupts | establish the safe floor and the mode B timing budget | **F8** |
| 11 | **Power mode per ODR** | Measure `Idd` at each ODR code | map codes to normal / low-power; reconcile with the noise spec condition | **F7** |
| 12 | **Aborted read recovery** | Kill a 6-byte FIFO read mid-frame (DMA abort), then resync | data correct after resync; quantify how many samples must be discarded | **F10** |
| 13 | **±16 g scale** *(only if used)* | Tilt table (±1 g) and shaker at known amplitude | measured mg/LSB matches the published value or the corrected one | **F1** |

---

## Appendix A — Quick reference for the vibration-logger project

Locked configuration for 1600 Hz FIFO capture (see `SD_LOGGER_STRATEGY.md` V3.2). Values here are
`[DERIVED]` from the register tables and are subject to the open items above.

| Register | Value | Meaning |
|----------|-------|---------|
| `CTRL_REG3` (0x23) | `0x01` → wait ≥ 10 ms → `0x00` → later `0x68` | POR first, configure after; `0x68` = IEA \| IEL \| INT1_EN (**edge pending E8**) |
| `CTRL_REG5` (0x24) | `0x00` | ±2 g, AA 800 Hz, ST off, 4-wire |
| `FIFO_CTRL` (0x2E) | **`0x48`** | Stream + **WTMP = 8** (5.0 ms at 1600 Hz) — *not* `0x50`, which is 16 |
| `CTRL_REG6` (0x25) | **`0x74`** | FIFO_EN \| WTM_EN \| ADD_INC \| P1_WTM |
| `CTRL_REG4` (0x20) | **`0x9F`** | 1600 Hz + **BDU = 1** + XYZ — *not* `0x97`, which is BDU = 0 |
| `WHO_AM_I` (0x0F) | read `0x3F` | presence gate |
| Status per drain | `FIFO_SRC` (0x2F), `STATUS` (0x27) | `FSS`/`OVRN_FIFO`/`EMPTY`, `ZYXOR` |
| Data | `0xA8` + N × 6 bytes | **mode A only if F4 is proven**; otherwise N × 7-byte frames |
| Temperature | `OUT_T` (0x0C), `int8` | relative, per block |

**Derived constants:** 6 B/sample · 9600 B/s payload at 1600 Hz · 680 samples per 4080 B payload ·
block duration 425 ms · FIFO slack above watermark = 32 − 8 = **24 samples = 15 ms** ·
±2 g digital clip = **±1966 mg** · sensitivity **60 000 ng/LSB**.

**Driver invariants that follow from this datasheet:**
1. Never read more samples than `FSS` reports — except when `OVRN_FIFO` says the FIFO is full and
   `FSS` is untrustworthy, which is an **overrun path**, not a read path.
2. Always drain below `WTMP`; escalate to a resync if that cannot be achieved.
3. Reset before configuration; start ODR last; discard the settling window.
4. Restore `CTRL_REG6` after any `BOOT`.
5. Treat every aborted SPI frame as a FIFO resync trigger with counted loss.

---

## Appendix B — Patch list status

**Applied in v2.0:** E1, E2, E3, E4, E5, E6, E7 (corrections); F2, F3 (init-sequence and
register-write traps fixed in §10.2 / §7.25); provenance block, status tags and sign-off register
(§0); full-scale consistency analysis (§4.3); safe-drain algorithm (§8.4); conservative settling
floors (§10.5); consolidated errata register (§11); bench checklist (§12); project quick reference
(Appendix A).

**Pending — requires the physical PDF:** E8 (`IEA` wording), F1 (±16 g sensitivity), F7 (per-code
power mode), F8 (SPI characterization voltage / `CS`-high minimum), F11 (INT2 trigger).

**Pending — requires hardware:** F4, F5, F6, F10, F12, E12, E15, E17, E18, and confirmation of E8.

**Do not delete an open item.** When it is closed, move it to §0.3 with a result, a name and a date,
and update every dependent constant in the driver and in the container header.