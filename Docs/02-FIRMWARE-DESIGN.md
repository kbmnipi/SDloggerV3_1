# 02-FIRMWARE-DESIGN.md — the implementation contract

> **Source:** split verbatim from `Docs/SDlogger.md` Rev 3.5 (container format v4).
> **Body content transferred byte-for-byte; this header and the navigation map are the only
> additions.** Section numbers (§…, App. …) keep their original global numbering.
> **Contains original sections:** §2.1, §2.2, §2.3, §3.1-3.5, §4.1, §4.3, §4.4, §4.5, §5, §6, §7, §8
> **Cross-references:** internal refs (§3.4, App. F.1, A.6 #11, D-3, …) point to the original
> numbering; locate them via the map in `Docs/00-ARCHITECTURE.md`.


## 2. Hardware Topology & Framework Choice

### 2.1 Buses, pins, and the bring-up verification gate

| Function | Peripheral | Pins (LQFP64 AF12) | Mode / clocks | Mandatory bring-up checks |
|----------|-----------|--------------------|---------------|---------------------------|
| MicroSD | **SDIO** (never SPI) | **PC8=D0, PC9=D1, PC10=D2, PC11=D3, PC12=CK, PD2=CMD** | AF12, 1-bit → **4-bit**, SDIOCLK 48 MHz → SDIO_CK 24 MHz (CLKDIV=0) | CMD/DAT external pull-ups (10–100 kΩ, 47 kΩ typical); **no pull-up on CK** |
| **MicroSD card detect** | GPIO in, **PA8** (per `.clinerules/02-project-context.md`) | **PA8** | input; **pull-up / active level to be confirmed on the bench — do not assume** | **V3.4 (WARN-11) gate, V3.5 scope (WARN-3): confirm the wiring, do not build on it.** V3.3 recorded "no hardware CD switch → software-only detect"; the project context lists PA8 as the slot's CD pin. **M0 measures the fact only** (idle/active level, whether the slot switch is wired) and records it in `card_cd_fitted`. **For M0/M1 the state machine does not consume PA8**: the ladder is `write error → L0 ×3 → L1 ×2 → L3` (§5.3) regardless of CD state, and `cd_change_cnt` / `card_cd_state` / the 3-sample debounce are **not implemented**. If the M0 bench confirms PA8, it may be added **as a diagnostic that annotates F19 results — never as a state-machine input**. If PA8 is not wired, card presence stays software-only (CMD13 / ACMD41 sequencing + timeouts) and PA8 is recorded as not connected |
| LIS3DSH | **SPI1** | PA5=SCK, PA6=MISO, PA7=MOSI, CS=PA4, INT1=PB5 | **mode 3** (CPOL=1, CPHA=1) [DS §5.2], ≤10 MHz → **5.25 MHz** (APB2 84/16) | CS and INT1 routing; CS external pull-up; **scope INT1 idle/active level and pulse width** ([DS E8]); **minimum CS-high floor sweep** ([DS F8]) — this gates mode B timing and the 4–8 % CPU allowance (§3.2.4); `0xA8` burst-wrap proof is **optional** (gates mode A only). **Routing note (WARN-5):** flying leads to an external sensor board in a vibrating tunnel — keep the CS pull-up, twist CS/GND, prefer short leads over a higher BR. **Hardware request (V3.5, FLAW-6): 1 kΩ + 1 nF low-pass RC on INT1 at the MCU input** (τ ≈ 1 µs ≪ the 5 ms watermark period, ≫ the SPI frame scale) — it does not delay the watermark edge measurably and it attenuates the VFD/motor-coupled spikes that a *missed* edge would turn into an undeclared 1600 samples/s hole |
| User KEY | GPIO+EXTI | **PC13**, active-HIGH | input, rising edge, external pull-down assumed | confirm polarity; PC13–PC15 sit behind the backup-domain power switch — never outputs in STOP |
| Status LED | GPIO out | **PB2**, active-HIGH | push-pull, 2 MHz | PB2 = BOOT1: verify the resistor network still selects boot-from-flash (BOOT0=0 makes BOOT1 don't-care, but confirm) |
| VDD sense | ADC1 | internal **VREFINT = ADC1_IN17** | 12-bit; **slow 480-cycle only in M0/M1/M2** — the 84-cycle fast path leaves the write loop (V3.5, WARN-2; §6.2) | read `VREFINT_CAL` @ `0x1FFF7A2A`, sanity 1350…1650; if blank → fallback 1502 + diagnostic flag |
| Battery sense | — | **not fitted** | — | **Decision: cell voltage is not measured.** No divider on PB1; internal `VBAT` (ADC1_IN18) measures the backup-domain pin/4, not the LiPo. All energy policy uses VDD |
| Debug | UART/SWD | PA9/PA10, SWDIO/SWCLK | 115200 8N1 | UART forbidden in `RECORDING`/`ROTATING`; BOOT/IDLE only |
| RTC | LSE | 32.768 kHz (PC14/PC15) | prescaler 127/255 → SSR 256 Hz (3.906 ms) | confirm HSE is 8 MHz per BOM; without VBAT the calendar and Backup SRAM reset on power cut. **V3.4 (WARN-6) gate, V3.5 sizing: cross-check the LSE-derived `odr_ppm` against 1PPS or a 1 Hz counter gate on RTC-OUT/LSE — a 10 s count gives ≈ 10 ppm, which is inside a 10–30 ppm LSE term — and record the correction in the HEADER `odr_abs_ppm_ref` (§7.4). A synthesised shaker tone is an *alternative*, not the primary: it inherits the shaker's own frequency error. V3.5 (WARN-10): the same TIM2 1 MHz counter also histograms the inter-watermark-edge intervals for ODR **cycle-to-cycle** stability** |
| **DMA2 mapping** | — | — | RM0090 **Table 43** + the repo's `STM32F4_DMA2_…_RM0090.md` §1/§3 | **Legality is verified and unchanged; arbitration is not (V3.5, B1).** `SPI1_RX ∈ {S0,S2}/CH3`, `SPI1_TX ∈ {S3,S5}/CH3`, `SDIO ∈ {S3,S6}/CH4`. **Preferred assignment is the documented fallback** `SPI1_RX = S0/CH3`, `SPI1_TX = **S3/CH3**`, `SDIO = **S6/CH4**`, because inside one DMA controller the **lower stream number wins AHB arbitration**: with `TX = S5` and `SDIO = S3` the card's bursting DMA outranks the stream that clocks the sensor read. `S0 > S3 > S6` is strictly favourable to capture. Never `SPI1_TX` on S5/**CH4** (= SDIO) and never share Stream 3 between SPI1_TX and SDIO; an illegal pair routes nothing → a silent 0-byte hang, never an error. **M0 row: confirm on silicon** with a **DMA loopback with a known pattern**, assert no TEIF/FEIF/DMEIF, **and run the mode-B capture stream concurrently with continuous 4 KiB SDIO writes** against a deterministic stimulus (B1) |

Any change to these rows is a **fact correction**, not a redesign: the PCB is frozen.
The gate is passed on the bench, with a scope/analyzer, before BSP work is declared done (M0).

**Bench toolchain for the `Debug` row (recorded 2026-11-19; facts and open items in App. H, "Bench
equipment EQ-1…EQ-5").** The project's programmer — and the intended BOOT/IDLE telemetry receiver —
is the **WeActStudio MiniDebugger** (`github.com/WeActStudio/WeActStudio.MiniDebugger`), an
**APM32F103CBT6 / STM32F103CBT6** dongle that ships **two** firmwares: an **ST-Link V2.1** emulation
and a **DAPLink** build. What that means for this row:

1. **SWD is the programming path in either firmware** (SWDIO / SWCLK / GND). The build already uses
   the plain **`swd`** upload transport rather than `hla_swd` (commit `2514b45`), which is the correct
   posture for a V2.1 clone.
2. **The serial side belongs to the firmware and to the wiring, not to the connector.** Measured on
   this bench (recorded in `platformio.ini`, `[common]`): the unit enumerates as an **ST-Link** —
   `STLINK V2J46M32 API v2`, **VID:PID `0483:3752`**, target VDD measured **3.230 V** — and flashing
   runs through `interface/stlink.cfg` + `transport select **swd**`. ST-Link V2-style firmware *can*
   present a virtual COM port (the Nucleo pattern), but only if the UART middleware is inside the
   flashed image **and** the TX/RX pins are actually routed on the port; nothing in the vendor repo
   proves either — it ships **no USB-UART-bridge driver** (only `Drivers/STLink/stsw-link007-…`,
   `Drivers/STLink/stsw-link009.zip`, `Drivers/DAPLink/CMSIS_DAP_…`), the port map exists only as a
   picture (`Images/2.png`), and `Doc/WeAct-MiniDebugger_V10_SchDoc.pdf` is a raster image with **no
   text layer**. **`[TODO: verify against bench]`** — look for a second CDC interface ("ST-Link
   Virtual COM Port") while the dongle is in ST-Link mode, and identify which port pin is TX / RX.
   Until that is measured, the PA9/PA10 link is proven with a scope or a known-good second adapter,
   never assumed. **Decision item:** reflashing the dongle to `DAPLink` to obtain a COM port would
   invalidate the pinned flashing path (`upload_protocol = stlink` + `interface/stlink.cfg`) and force
   a CMSIS-DAP configuration instead — see App. H **EQ-6**. The safe default is **ST-Link mode for
   programming, a separate 3.3 V USB-UART adapter for telemetry**: two tools, neither of which can
   break the other.
3. **Wiring contract:** PA9 = **USART1_TX → the adapter's RX**, PA10 = **USART1_RX ← the adapter's
   TX**, and **one common GND**. Levels are **3.3 V** on our side (push-pull); a level mismatch is a
   hardware defect, not something firmware may compensate. **`[TODO: measure]`** the far end's logic
   level — a 5 V driver into PA10 violates the pin contract.
4. **No hardware flow control.** This row specifies **115200 8N1, three wires**. A USB-CDC virtual COM
   has no CTS/RTS and buffers outside our control, so the telemetry budget is bounded by this row's
   state gate — **BOOT/IDLE only, forbidden in `RECORDING`/`ROTATING`** — and not by the adapter's
   FIFO depth.
5. **What this toolchain unlocks:** the §10.2.1 **P2** evidence ("`HAL_GetVersion()` printed at BOOT")
   and the per-attempt diagnostic logs that today can only be read over SWD (App. H, **BF-1**). It
   does **not** replace the scope obligations of this row: the **`cs_high_floor_ns`** sweep, the
   **INT1 idle/active level and pulse width** ([DS E8]) and the DMA2 loopback remain scope work. The
   **47 kΩ CS pull-up** is fitted (BF-1) [corrected 2026-11-20: owner-measured fitted value is 33 kΩ, not 47 kΩ. Electrically equivalent for this purpose: 3.3 V / 33 kΩ ≈ 100 µA with CS low (negligible vs §6.1), and a stronger hold than 47 kΩ on flying leads. No conclusion depends on the value: the BF-1 re-test showed the pull-up does not change first-frame behaviour, and BUG-1's mechanism is the MCU-side SPE 0→1 edge under low CS.]; the **1 kΩ + 1 nF INT1 RC** requested by FLAW-6 is **not
   yet fitted**.

### 2.2 Clock tree (constant)

```
HSE 8 MHz ─► PLL: M=8 → 1 MHz ref, N=336 (VCO 336 MHz), P=4 → SYSCLK 84 MHz, Q=7 → SDIOCLK 48 MHz
             AHB 84 · APB1 42 (TIM2 kernel 84) · APB2 84 (SPI1) · FLASH 2 WS
             SDIO_CK = 48/(CLKDIV+2) = 24 MHz · IWDG LSI 32 kHz nom · RTC LSE 32.768 kHz
TIM2: PSC=83 → 1 MHz free-running 32-bit counter (wrap 4295 s), CH1 compare → 1 kHz supervisor tick.
      All elapsed-time arithmetic uses unsigned 32-bit subtraction (wrap-safe).
```

**No dynamic PLL switching.** Reconfiguring P on the fly stops PLLQ (SDIOCLK), forces Flash WS and
APB divider changes and `HAL_InitTick` re-init while a card is mounted. At 84 MHz the CPU budget is
met with >10× margin (§9), so the 168 MHz island is removed entirely. Clock-tree verification is on
the audit's "verified correct" list (Q=7 → 48 MHz SDIOCLK is required by the F4 SDIO kernel clock;
APB2 84/16 = 5.25 MHz SPI1; TIM2 kernel 84 MHz with PSC=83 → 1 MHz).

**After STOP** (§6.3) SYSCLK falls back to HSI: `bsp_clock_restore()` must re-run the full PLL
configuration and `HAL_InitTick()` on every wake (~1–2 ms, accounted in the IDLE budget).

The LIS3DSH sample clock is internal to the sensor: MCU clock error moves only our readout, never
the sample instant. ODR accuracy is **not specified** in [DS E17] → tracked at runtime (§7), and
*absolutely* bounded per §7.4.

### 2.3 Framework decision

**STM32Cube HAL (LL in the hot path) + FatFs R0.12c over own `diskio` → `HAL_SD`, no RTOS.**
One super-loop consumer plus event-driven producers.

| Criterion | Arduino / mbed | **HAL + LL + FatFs** | Bare CMSIS |
|-----------|----------------|----------------------|------------|
| SDIO 4-bit + DMA out of the box | no | **yes** | hand-written CMD stack + DMA |
| Control of FAT allocation (the real stall source) | none | **direct: own diskio, pre-allocation, aligned writes** | total |
| Fixing "HAL_SD returns early while the card programs" | none | **high (readable, patchable)** | highest |
| Time to first reliable log | high risk | **low** | 3–6 weeks |

Rationale: the logger idles ~96 % of the time and the bottleneck lives inside the card controller;
bare-metal buys ~2–3 % CPU that we do not need. An RTOS would add priority inversion, heap
fragmentation and stack cost for two contexts and one ring.

**HAL limits explicitly lifted:**
1. Default `HAL_SD` timeouts (5000 ms) swallow GC stalls and outlive the IWDG → own timeouts (§5.2),
   **including the constants inside `HAL_SD_GetCardState()` / `SDMMC_CmdSendStatus()`** — audit every
   `SDMMC_*TIMEOUT` in the vendored HAL.
2. Polled `HAL_SD_WriteBlocks*` burns CPU → only `_DMA` variants are legal.
3. `HAL_GetTick`/`HAL_Delay` only outside interrupt context (SysTick is lowest priority).
4. No `malloc`/`new` at runtime; everything static and placed by linker sections (§9).
5. Card-busy polling (`CMD13`, R1 `READY_FOR_DATA` bit 8 / `CURRENT_STATE != PRG`) is explicit.
6. **LL/register-level DMA setup is mandatory in the sensor hot path** (mode B especially, §3.2).
7. **V3.4 (WARN-2/WARN-4):** the FatFs option set used here (`_FS_REENTRANT`, `_FS_TINY`, `_MIN_SS`,
   `_USE_LFN`) is **pre-R0.13 naming**, while `f_truncate` is documented as a later addition — so
   `f_truncate` is **not used anywhere in firmware**. `f_expand` semantics and `f_write`/`f_sync`
   `OBJ_DIRTY` behaviour remain **unverified until `ff.c` is vendored**; the repo currently contains
   **no vendored `ff.c` and no vendored HAL**, so every "verify in the vendored source" instruction in
   this document is an **M2 action item**, not a closed fact. Independently, the 13-bit SDMMC byte
   counter is worked around by the 4096 B write cap (§3.4) instead of trusting HAL chunking.
8. **V3.5 (WARN-11, B3, B5) — what the repo actually is today.** `platformio.ini` as committed contains
   one bare env (`platform = ststm32`, `board = genericSTM32F405RG`, `framework = stm32cube`) with
   **no platform version pin, no `lib_deps`, no build flags and no `env:release`**; `.clinerules` claims
   HAL 1.28.3 / FatFs R0.12c / `env:release` (-O2), which is **not yet true in the repo** (and it cites
   `docs/…` while the folder is `Docs/`). Until the §10.2.1 repo preconditions are met, every
   exit criterion that depends on reading HAL/FatFs source is **not passable**, and no compiler-flag or
   linker-section rule in this document exists in the build.

## 3. Sensor Acquisition, Concurrency & Buffer Management

### 3.1 LIS3DSH configuration contract (reconciled with [DS V2.0])

Initialization order is mandatory: **reset → wait → configure → start ODR last.**

| Step | Register | Value | Meaning |
|------|----------|-------|---------|
| 1 | `CTRL_REG3 0x23` | `0x01` | `STRT=1` soft reset (POR). **Wait ≥10 ms**, then write `0x00` if STRT does not self-clear. Writing STRT together with config bits erases them |
| 2 | `WHO_AM_I 0x0F` | read `0x3F` | presence gate |
| 3 | `CTRL_REG5 0x24` | **`0x50`** | **BW = 01 → AA 400 Hz** (the V3.3 setting `BW = 00` = 800 Hz put the knee **at** Nyquist — withdrawn by V3.4 FLAW-3) **+ FSCALE = 010 → ±6 g**, **0.18 mg/LSB**, clip **±5.90 g**, ST = 00, SIM = 0 (4-wire). **V3.5 (FLAW-1) decode, stated so it is never re-mistaken:** `BW = (v >> 6) & 3`, `FSCALE = (v >> 3) & 7`; `0x50 = 0101 0000` → `BW = 01`, `FSCALE = 010` = **±6 g** (`DS_lis3dsh.md:571-573`, `:588-589`). The V3.4 label "±4 g / `FSCALE = 001`" for this byte was a **decode error**; `0x48` is what ±4 g would have been and is **not** used here. Rule: **`fc = fs/4`**, so the AA transition band lies strictly below Nyquist. `0x40` = the same filter at **±2 g** (de-escalate only if peaks are proven < 1.9 g); `0x58` = **±8 g** (escalate if a pre-test shows peaks > 5.9 g) — see the range policy in §3.3. `0x10` / `0x00` (BW 800 Hz) are **forbidden** at ODR 1600. **Cross-reference corrected:** the ST BSP `AA_BW_400 = 0x40` [DS §7.24 / E4] corroborates the **BW** bits only; FSCALE is a separate field, and that is exactly why "0x50 = 0x40 + something = ±4 g" was wrong |
| 4 | `FIFO_CTRL 0x2E` | `0x48` | FMODE=010 **Stream** + WTMP=**8** (5.0 ms nominal batch). `0x50` would be WTMP=16. **This step is mandatory** — [DS F2] showed that omitting it leaves FMODE=Bypass and produces *no* watermark interrupts |
| 5 | `CTRL_REG6 0x25` | `0x74` | FIFO_EN(0x40) + WTM_EN(0x20) + ADD_INC(0x10) + P1_WTM(0x04) [DS §7.25] |
| 6 | `CTRL_REG3 0x23` | `0x68` | IEA=1, IEL=1 **pulsed**, INT1_EN=1, STRT=0. **Polarity is a scope gate** ([DS E8]) |
| 7 | `CTRL_REG4 0x20` | `0x9F` | **written last**: ODR=1001 → 1600 Hz, **BDU=1** (0x08), Zen/Yen/Xen=0x07. **BDU protects the output registers only, not the FIFO pop path** ([DS F10]) — kept because it is free, not because it covers the drain |

**Normative note (Rev 3.5, M0 bench finding BF-1, App. H) — the step 2 presence gate is a retry
loop, and the FIRST SPI frame after power-up is a throwaway.** The table row above states *what*
step 2 reads, not *how the read is repeated*. The full contract for step 2:

1. **Read `WHO_AM_I 0x0F` until it returns `0x3F`, up to 3 attempts.** The gate passes if **any**
   attempt returns `0x3F` and fails only if all three fail. One `0x3F` is a positive
   identification; retrying cannot invent `0x3F` on a genuinely dead bus, which reads the same
   pulled-up `0xFF` (or the same `0x00`) on every attempt.
2. **The FIRST SPI frame after power-up is NOT guaranteed and is treated as a throwaway.**
   Measured on the bench: attempt 1 → `0xFF` (part silent), attempt 2 → `0x3F` (part present),
   attempt 3 not executed (the loop exits on the first match). `s_spi_err_cnt = 0` for **both**
   frames — no transfer timeout, no `SPI_SR.OVR`, no `SPI_SR.MODF`, all four signals present on the
   scope (CS asserted, 16 clocks on SCK, MOSI, MISO). The first frame is therefore **electrically
   clean**: the part did not answer it, which is not a bus fault.
3. **Mechanism (most likely).** [DS §5, `Docs/DS_lis3dsh.md:249`]: **`CS` high = I²C enabled / SPI
   idle; `CS` low = SPI active.** With `CS` high the part sits in I²C mode, so the first falling
   `CS` edge after power-up is the moment it first registers SPI mode — and that frame can be lost.
   After it the part answers. Recorded as a **bench finding**, not as a datasheet guarantee.
4. **Keep a per-attempt log of the returned byte** (diagnostics). The log must distinguish "the
   transfer produced no byte at all" (sentinel, e.g. `0xEE`) from "a byte that was not `0x3F`", so
   silence is never later read as a wrong answer. That log is what separates *"sensor absent /
   wiring / supply"* from *"first frame eaten"* and makes the gate a measurement, not a hope.
5. **Failure path.** If `0x3F` is not obtained within 3 attempts, step 2 **fails**: report an
   **initialization error** and **do not proceed** to steps 3–7. A part that never answers is not a
   first-frame casualty, and configuring against a part that is not there yields a file full of
   zeros, not a measurement.

**Hardware note (closes the "CS external pull-up" M0 bring-up item of §2.1).** A **47 kΩ pull-up is
now fitted on CS (PA4)** [corrected 2026-11-20: owner-measured fitted value is 33 kΩ, not 47 kΩ. Electrically equivalent for this purpose: 3.3 V / 33 kΩ ≈ 100 µA with CS low (negligible vs §6.1), and a stronger hold than 47 kΩ on flying leads. No conclusion depends on the value: the BF-1 re-test showed the pull-up does not change first-frame behaviour, and BUG-1's mechanism is the MCU-side SPE 0→1 edge under low CS.]. It removes the floating-CS failure mode — an undefined CS level at
power-up can otherwise leave the part in either interface, especially on the flying leads of §2.1
WARN-5 — but it is **independent of** rule 2: a defined CS level does not make the part answer the
first frame. The 3-attempt budget and the per-attempt log are kept regardless.

**Scope of this note.** It governs the **power-up presence gate only**. It does not weaken the
§3.2.6 M0 assertion (once initialization has succeeded, a polled `WHO_AM_I` read with the DMA streams
armed must still return exactly `0x3F`) nor the §3.2.7 liveness poll: both run against an
initialized, answering part, where a miss is a real fault.


**BOOT-time sensor reconfiguration only (V3.5: FLAW-3 range policy + WARN-8 alias scan).** Two settings
are chosen **before** the campaign and never changed inside a part:

1. **Full scale** — from the **60 s BOOT pre-test** (sensor configured at the default ±6 g, samples read
   but not logged) which measures the field `max_abs_raw_max` (A.1 `0x0C8`). Escalate to **±8 g
   (`0x58`)** if that pre-test shows peaks > 5.9 g; de-escalate to **±2 g (`0x40`)** only if peaks are
   *proven* < 1.9 g; otherwise keep **±6 g (`0x50`)**. The decision is recorded in the HEADER
   (`ctrl_reg5`, `fs_g`, `ng_per_lsb`) before `PREPARE` creates the file. **There is no runtime range
   switch** — changing FSCALE mid-part would silently change the meaning of every raw code already in
   the ring and of every `max_abs_raw` in the file.
2. **The dual-rate alias scan (WARN-8, App. F.3 — mandatory, not optional).** A **2-minute** sweep at
   `ODR 1600 Hz / BW 400 Hz` (`CTRL_REG4 = 0x9F`, `CTRL_REG5 = 0x50`) followed by **2 minutes** at
   `ODR 800 Hz / BW 200 Hz` (`CTRL_REG4 = 0x8F` — `ODR = 1000` per [DS:520], `CTRL_REG5 = 0x90` with the
   **same** `FSCALE = 010`), i.e. **`fc/fs = 1/4` in both** and both legal per [DS §10.3]. It runs at BOOT
   as a **separate container part** whose own HEADER carries its own `odr_reg_hz` / `aa_bw_hz` /
   `sample_rate_hz` — a second rate must never appear inside one part, because `sample_rate_hz` is a
   single per-part constant (decision **D-3**). The campaign part records only the verdict:
   `alias_scan_done` / `alias_scan_odr_hz` / `alias_scan_aa_bw_hz` (A.1 `0x0CA…0x0CF`). Peak-list
   cross-matching is host-side (App. F.3).

After **each** such reconfiguration the ≥ 100 ms discard applies and the chain timing state is re-based
(B4, §3.2.1). ODR and BW are written together so that the ratio never moves: a lone `BW` change at a
fixed ODR is not legal under this project's `fc = fs/4` rule at 1600 Hz (`BW ∈ {400, 200, 50}` — the
200 Hz setting at ODR 1600 would be `fc = fs/8` and is permitted by [DS §10.3] but is **not** used).

If `BOOT` (0x80 in `CTRL_REG6`) is ever used instead of `STRT`: it **also clears `ADD_INC` and
`FIFO_EN`**, and after self-clear the register reads `0x00` — **`CTRL_REG6` must be restored**
([DS F3]). The default path uses `STRT`, which restores POR defaults (`CTRL_REG6 = 0x10`).

Read-only status used by the driver: `STATUS 0x27` (`ZYXDA`, `ZYXOR`), `FIFO_SRC 0x2F`
(`WTM`, `OVRN_FIFO`, `EMPTY`, `FSS[4:0]`), `OUT_T 0x0C` (signed int8, **relative** temperature).

**`FSS` is 5 bits and the FIFO is 32 deep → 32 is not representable.** `OVRN_FIFO` is a *fullness*
flag whose latch/clear semantics are unspecified ([DS F5]). Neither field may be used alone (§3.2).

**Driver prohibitions [DS §6, §11]:**
- Never write addresses absent from the register map — permanent damage is possible.
- Never modify factory-loaded calibration registers.
- Never use ODR codes `1010…1111` or FSCALE codes `101…111`. **Never use ±16 g without per-unit
  characterization** — the published 0.73 mg/LSB implies ±23.9 g on a 16-bit code ([DS F1]).
- SPI mode 3, MSB-first, ≤10 MHz. Auto-increment comes only from `CTRL_REG6.ADD_INC=1`;
  there is **no MS bit** in the command byte (`0x80|addr` for read).
- LSB weights of `OFF_x`, `CS_x`, `THRSx`, `HYSTx` are unspecified → characterize and record, never assume.
- After ODR start and after any self-test / ODR change / resync, **discard ≥ 100 ms** of samples before
  counting. [DS F6] confirms no settling constants exist; the "≥ 4/ODR" figure in older DS revisions is
  a lower bound on *sample availability*, not on filter settling (2.5 ms at 1600 Hz — unsafe).
  **V3.5 (B4), normative:** at the **end** of that discard window `t_last_end_us` and
  `captured_at_last_end` are **re-based to `now` / current `captured`**, and the **deficit test is
  disabled until then** (§3.2.1). Discarded samples are **not** loss. Without this re-base the first
  chain would publish `samples_lost_sensor += 160` + `SENSOR_OVR` on block 0 — fabricated loss, which
  K4 forbids.
- `WTMP = 0` is undefined ([DS F12]) — always use 8.
- **V3.4:** `BW` must satisfy **both** `BW ≤ ODR/2` ([DS §10.3]) and `BW ≤ ODR/4` (this project's
  anti-alias rule, §3.3). At ODR 1600 that means `BW ∈ {400, 200, 50}`.

### 3.2 Capture path

```
LIS3DSH ODR 1600 Hz → FIFO 32×3 → watermark 8 (5 ms) → INT1 → EXTI [P6]
    │ read FIFO_SRC (2-byte PIO SPI, ~3.2 µs); decide n (drain-first + credibility check)
    │ mode B (default): n × 7-byte frames, CS per sample   |  mode A (optional): 1 + 6n bytes
    ▼
 SPI1_RX DMA2 S0/CH3 ─┐
 SPI1_TX DMA2 S3/CH3* ─┴→ DMA TC ISR [P6]: pack → ring[head] payload
                         on 680 samples: latch RTC (§7), read OUT_T, stamp identity+flags+max_abs_raw,
                                            record C, DMB, head++
                         chain end: deficit test → re-read FIFO_SRC → chain / clear pending / escalate
    ▼
 RING 16 × 4096 B = 64 KB D1, aligned(4096), SPSC, no mutexes
     ▲
 [SUPER-LOOP] CONSUMER: 1 s VDD median → max_abs_raw check → **slew/spike gate (log-only, §3.4)** → crc32_payload → hdr_crc8
                        → f_write(**4096 B, one block per call**, ≤4 calls per pass)
                        → diskio → SDIO + DMA2 S6/CH4* → CMD13 busy poll (**no ADC in the write loop — V3.5 WARN-2**)
                        → tail++ only after acknowledgement
                        MAN commit every 64 blocks (27.2 s); anchor every 4 commits (108.8 s),
                        anchor sectors **appended once** (31 anchors buffered → one 512 B write)
                        IWDG feed on proven progress (§5.1); WFI() when nothing to write
                        **producer-liveness check (§3.2.7): if FSS ≥ WTMP and now − t_last_end_us
                         > 12 ms → call the same drain entry the EXTI uses;
                         producer_liveness_miss_cnt++  (cost ≈ 0.05 % CPU)**
 TIM2 1 kHz: supervisor, KEY debounce, **10 ms producer-liveness flag (§3.2.7)**, 1 Hz flags
             (**no 1 Hz PA8 card-detect sample in M1/M2 — V3.5 WARN-3, §2.1**).
             **Not the sample clock, not an RTC/ADC owner, never touches the FIFO itself.**
```

**Stream note (V3.5, B1)** — the preferred DMA2 assignment is `SPI1_RX = S0/CH3`, `SPI1_TX = S3/CH3`,
`SDIO = S6/CH4`, which makes the intra-controller arbitration order `S0 > S3 > S6` — capture outranks
the card. The V3.0–V3.4 assignment (`TX = S5/CH3`, `SDIO = S3/CH4`) is **equally legal** but puts the
stream that clocks the sensor read **below** the card's bursting DMA; if it is kept, the M0 concurrency
test of §10.2 M0 is **mandatory, not optional** (duplicated-byte mechanism `UNCERTAIN` until the RM0090
SPI under-run wording is checked against the PDF).

#### 3.2.1 Drain decision (R1 + X4) — never over-read, never fabricate loss

**Numeric contract (V3.4, WARN-13) — this code runs inside the P6 EXTI/DMA ISR.** All arithmetic is
**u32 fixed point**: **no `float`, no `double`, no lazy stacking, no division, no `long long`** in
the ISR. The two constants are recomputed in **app/consumer context** whenever an anchor updates
`odr_est_hz`/`odr_unc_ppm` (once per 425 ms at worst, never in the ISR):

```c
/* recomputed in app context (NOT in the ISR): */
odr_q20 = (uint32_t)(((uint64_t)odr_est_hz   << 20) / 1000000u);  /* 1600 Hz → 1678 */
unc_q16 = (uint32_t)(((uint64_t)odr_unc_ppm  << 16) / 1000000u);  /*  500 ppm → 32, 100000 ppm → 6554 */

/* per-chain state: t_last_end_us, captured_at_last_end, iter, drain_active, pending */

src = read(FIFO_SRC);

if (src.EMPTY) { end_chain(); return; }              /* nothing to read */

/* elapsed_us_clamped: unsigned 32-bit wrap-safe subtraction, then clamped to 1e6 µs (1 s).
   Bound: expected ≤ 1600, product ≤ 1e6 × 1678 = 1.678e9 < 2^31 → u32 is sufficient. */
elapsed_us = MIN(elapsed_us_since(t_last_end_us), 1000000u);
expected   = (elapsed_us * odr_q20) >> 20;           /* = elapsed_us × odr_est_hz / 1e6 */
band       = MAX(2u, ((expected * unc_q16) >> 16) + 1u);

/* Fullness is only credible if enough time has passed to actually produce 32 samples.
   This makes a latched/stale OVRN_FIFO harmless: it can never trigger an over-read. */
credible_full = src.OVRN_FIFO && (expected + band >= 32);

if (credible_full) {
    n = 31;                       /* FSS is untrustworthy at the 32 boundary — do not use it */
    fullness_seen = 1;
} else {
    if (src.OVRN_FIFO) {          /* set but not credible → stale latch */
        stale_ovr_seen = 1;       /* advisory + resync request at chain end */
    }
    if (src.FSS == 0) {           /* !EMPTY && FSS==0 && !credible_full → inconsistent status */
        request_resync(); end_chain(); return;
    }
    n = min(src.FSS, 31);         /* never more than reported */
}

launch_DMA(n);                    /* mode B: n frames; mode A: 1 + 6n bytes */
```

**Every `FIFO_SRC` read and every `FIFO_CTRL` write above goes through the PIO/DMA quiesce sequence of
§3.2.6 (V3.5, B2).** A polled SPI access taken while DMA2's SPI1 streams are armed returns the **last
DMA-received byte**, not the register: that produces a wrong `FSS` / `OVRN_FIFO`, therefore a wrong
`n`, therefore duplicates or fabricated loss. The quiesce is a driver rule, not an option.

**V3.5 (B4), normative — settling / resync window.** Immediately after ODR start, after a self-test
transition, after a resync and after an SPI/DMA abort: `discard_active = 1`, the **deficit test below is
disabled**, and at the **end** of the ≥ 100 ms window the driver sets `t_last_end_us = now`,
`captured_at_last_end = captured`, `discard_active = 0`. Discarded samples are **not** loss and are
**never** added to `samples_lost_sensor`. Consequence if omitted: the first chain end would compute
`deficit ≈ 160` and publish `samples_lost_sensor += 160` + `SENSOR_OVR` on **block 0** — a fabricated
gap that removes good data from analysis and pollutes the first anchor-plausibility statistics.

**V3.5 (WARN-5, decision D-4) — cold-start invariant, normative:** when `unc_q16 == 0` (no ODR
uncertainty estimate yet) the band term evaluates to `(expected × 0) >> 16 + 1 = 1` and `band` is
clamped to **`MAX(2, 1) = 2` samples = 1.25 ms @ 1600 Hz**. That floor is **intended cold-start
behaviour**, not an accident: it is the smallest decision granularity the watermark batch can support.
It is asserted as a `logcore` unit test so a mis-initialised `odr_unc_ppm` cannot masquerade as a legal
cold start (the shipping value at boot is `odr_unc_ppm = 100 000 → unc_q16 = 6554`, which yields
`band = 4` at `expected = 32`).

In the DMA TC ISR:

```c
pack(n);  captured += n;                    /* C domain advances — §1.3.7 */

if (fullness_seen) {                       /* remainder pass: 32 - 31 = 1, plus arrivals */
    s2 = read(FIFO_SRC);
    if (!s2.EMPTY && s2.FSS) { read_and_pack(min(s2.FSS, 3)); }
    fifo_full_cnt++;                       /* ADVISORY, not a loss */
    pending_FIFO_FULL = 1;
}

s3 = read(FIFO_SRC);
if ((!s3.EMPTY && s3.FSS >= WTMP) || pending) {
    if (++iter < 4) { pending = 0; chain_another_drain(); }
    else            { force_resync(); pending_SENSOR_OVR = 1; }   /* never a silent stall */
} else {
    deficit_test();                        /* below */
    end_chain();                           /* t_last_end_us = now; captured_at_last_end = captured */
}
```

**Proven-deficit test (R1, X5).** Runs at chain end; the *decision* to declare loss is taken only
when a fullness or stale-flag event was observed in this chain:

```c
expected = (MIN(elapsed_us_since(t_last_end_us), 1000000u) * odr_q20) >> 20;
residual = (s3.OVRN_FIFO ? 0 : s3.FSS);            /* bounded < WTMP after a successful drain */
deficit  = expected - (captured - captured_at_last_end) - residual;
band     = MAX(2u, ((expected * unc_q16) >> 16) + 1u);

if ((fullness_seen || stale_ovr_seen) && deficit > band) {
    samples_lost_sensor += deficit;   /* INCLUDES any samples the resync will discard — X5:
                                         do NOT add the discard again */
    fifo_ovr_cnt++;  pending_SENSOR_OVR = 1;
    force_resync();
} else if (fullness_seen || stale_ovr_seen) {
    /* Loss NOT proven → no declared gap. FIFO_FULL advisory only. */
    if (stale_ovr_seen) request_resync();   /* clear the latch at a safe point */
}
```

`odr_est_hz` / `odr_unc_ppm` are maintained by the producer from its own anchors (§7):
`odr_unc_ppm = 100 000` (±10 %) until anchors span ≥60 s, then `max(500, 3 × residual_ppm)`.
At the 20 ms fullness scale the band is **±4 samples** at startup and **±2 samples** once locked —
so "full but intact" is correctly reported as **no loss**, while a genuine 32-sample overwrite
is still caught. `deficit` is added to `samples_lost_sensor` only; it **does not** advance `C`
(§1.3.7: sensor-side samples were never captured).

**Resync recipe** [DS §8]: `FMODE=000` (Bypass) → `FMODE=010` (Stream); discard ≥ 1 ODR period of
pipeline refill **and, per V3.5 B4, the full ≥ 100 ms settling discard with the deficit test disabled
and the chain state re-based at its end**; reset `t_last_end_us` / `captured_at_last_end`; clear chain
state. Every `FIFO_CTRL` write in this recipe is a PIO write and therefore runs through §3.2.6.

**Why this matters for the spectrum.** A fabricated gap does not create sidebands — the host
*excludes* the window. The damage is that **good data is thrown away and loss counters lie**.
Drain-first preserves both the data and the honesty of the accounting.

#### 3.2.2 SPI/DMA error handler (R2) — mandatory resync

```
on DMA2 SPI1 stream TEIF | FEIF | DMEIF (S0/S3 in the preferred set, S0/S5 in the legacy set),
or SPI1 OVR / MODF:
    abort both DMA streams; clear all DMA + SPI flags; SPE=0; flush RX path
    **quiesce per §3.2.6 before any polled SPI write (B2) — the resync's `FMODE` write is a PIO write**
    discard both staging buffers entirely
    loss = (FSS_at_error if readable else 0) + n_in_flight     /* bounded estimate */
    samples_lost_sensor += loss;  spi_err_cnt++;  pending_SENSOR_OVR = 1
    force_resync()                                            /* [DS F10]: state is undefined */
    **discard_active = 1 for ≥ 100 ms; deficit test disabled; chain state re-based at window end (B4)**
    drain_active = 0; iter = 0; pending = 0; clear EXTI_PR
    resume on next INT1 (or the §3.2.7 liveness poll)
```

Mode B limits the in-flight blast radius to **one sample** instead of N — an independent reason
to prefer it (§3.2.4).

#### 3.2.3 Pending, EXTI clearing, and chain ownership (R7)

- A new EXTI while `drain_active` only sets `pending`; it never re-enters the chain.
- `pending` is consumed **inside the producer chain** at chain end (never from the consumer —
  SPI1 has a single owner, §2.4).
- `EXTI->PR` is cleared by writing 1 at EXTI entry **and at chain end after the final `FSS` check**.
  The **level check, not the edge, is the authority**: if `FSS ≥ WTMP` we drain again regardless of
  whether an edge was seen. This closes the pulsed-watermark re-arm ambiguity ([DS §11]).
- `iter < 4` bounds the chain; exhaustion escalates to a resync, never to a silent stop.

#### 3.2.4 SPI read modes — **mode B is the default** (R9)

| Mode | Framing | Status | CPU @ 84 MHz |
|------|---------|--------|--------------|
| **B (default)** | N separate 7-byte transfers, CS asserted per sample | **always correct per documented framing**; implemented and verified first | **typical ≈ 2.2 %** (7 B @ 5.25 MHz = 10.7 µs + ~3 µs setup ≈ 13.5 µs/sample); **design allowance 4–8 %** (WARN-5) |
| A (optional) | one CS-low burst of `1 + 6N` bytes (`0xA8` then N×6) | enable only after the analyzer proves `0x2D → 0x28` wrap ([DS F4]) | ≈30–45 µs/drain → **0.7 %** |

**The mode-B number is not finished until the CS-high floor is measured (WARN-5).** 2.2 % is
derived from **bus time only**; [DS F8] leaves the **minimum CS-high time between frames
unspecified**. If the M0 sweep lands at 10–20 µs, mode B costs **23–33 µs/sample → 3.9–5.5 %
continuous**, and the worst-case drain chain grows to `4 × (31 × 33 µs) = 4.1 ms` (against a 5 ms
batch period and 15 ms of FIFO slack). Therefore:

- **K8 budgets the K7/K8 headroom as 4–8 %**, not 2.2 %, until the floor is known; DWT measures it
  in M0 and the measured value is recorded in the HEADER run report.
- **Stated fallback:** SPI1 **BR = 32 → 2.625 MHz** halves per-frame bus time (5.35 µs) and buys
  back ~1.5–2.5 % if the CS floor is long. Physical preference in a vibrating tunnel is the
  opposite direction: keep the CS pull-up, twist CS/GND, keep leads **short** — a shorter lead is
  worth more than a higher BR.
- Mode B **must** use LL/register-level DMA setup: HAL's `..._DMA` costs ~15 µs of overhead per call
  and would push the load past K8.
- Worst-case drain (measured-typical floor): 31 × 13.5 µs ≈ **419 µs**; floor-bound worst case
  31 × 33 µs ≈ **1.02 ms**.
- The choice is a compile-time constant plus the HEADER `readout_mode` field — not a rewrite.
- Consequences: [DS F4] is **no longer a blocker**; capture can start before the wrap question is settled.
- **M0/M3 gate wording (WARN-12):** mode B must first prove **monotonicity, no duplicates and
  `ΔFSS ≈ N ± 1`**; **A/B byte-identity** is an M3 check, because mode A does not exist at M0.

#### 3.2.5 Packing, header ownership, priorities

**Sample-driven packing.** No "85 batches = 1 block" assumption. The packer appends 6-byte samples
into `ring[head]` payload (4080 B = 680 samples); at 680 it finalizes the producer fields, executes
`__DMB()`, advances `head`, and rolls the remainder into the next block. The producer computes
`max_abs_raw` in the same pass (a running maximum over the block's 2040 int16 magnitudes, shifted
right by 8 at completion) — ≈ 2–3 µs per block ≈ **0.0006 % CPU**, which is why replacing the binary
CLIP flag with a real per-block magnitude costs nothing (FLAW-4).

| Field | Owner | When |
|-------|-------|------|
| `block_seq`, `first_sample_index` (**C domain + packed flags**, A.3), `lost_or_valid`, `temp_rel_i8`, `max_abs_raw` | **producer** (TC ISR) | at block completion |
| `vdd_x10mv` | **consumer** | just before the write, from the **latest 1 s slow median** (§6.2). **V3.5 WARN-2: the one-block attribution lag rule is deleted** — the field carries a 1 s-window statistic, not an event, so no lag is claimed and none must be corrected by the host |
| `crc32_payload`, `hdr_crc8` | **consumer** | just before the write |

Legal under SPSC ownership: after `head++` the slot belongs to the consumer until `tail++`. It also
fixes causality — a producer-finalized header could never contain the droop caused by its own later
write. **V3.4 consequences of the index/flag repack (A.3):** the producer writes the *whole* index
word, because the flags now live inside it (bits 31:25) — so a pending `OVERFLOW`/`FIFO_FULL` from
the abort path of §3.4 I3 is stamped by the producer at completion, and the consumer must **mask,
never write**, those bits (it may only OR in `VDD_LOW`, which it is allowed to know about). Because
the consumer touches neither `C` nor `block_seq`, the C↔S invariant of §1.3.7 cannot be corrupted by
the write path, which is what §3.4 I5 requires.

**Fallback if INT1 is not routed:** TIM2 at 200 Hz polls `FIFO_SRC` and runs the *identical* drain
decision, deficit test and packing. Sample instants are unaffected. Raw per-sample DRDY mode
(`DR_EN=1`, 1600 IRQ/s) is diagnostics-only.

**Priority plan (NVIC 4-bit preempt, 0 sub-priority):**
`PVD 4` → `SDIO + its DMA stream 5` → `SPI1 / its two DMA streams / INT1 EXTI 6` → `TIM2 7` → `KEY EXTI 7` → `SysTick 15`.
The sensor chain is made latency-tolerant by the **sensor FIFO** (24 samples = 15 ms of slack above
the watermark), not by priority. PVD stays tiny: set a flag, touch no peripheral.
**V3.5 (B1), a distinction that matters:** NVIC priority orders *ISRs*, not *AHB bus masters*. Stream
arbitration inside DMA2 is fixed by **stream number**, so no `NVIC_SetPriority` call can protect the SPI1
TX stream from the SDIO stream's bursts — that is exactly why the `S0/S3/S6` assignment (§2.1) is the
preferred one and why the M0 concurrency test exists.

**Sampling supervisor.** TIM2 compares the captured-sample counter (the **C** domain, §1.3.7) against
elapsed ticks; a deviation beyond **±25 % sustained > 50 ms** raises `SENSOR/TIMER` (LED 5) and forces
a resync. Active in `PREPARE`/`RECORDING`/`ROTATING`; disabled in BOOT/IDLE and after the producer is
intentionally stopped in `FLUSHING`. Two V3.4 notes:
1. The supervisor is the safety net for the new overflow rule (§3.4 I3): a blocked publish cycle must
   still advance `captured` — if it does not, the deviation trips here rather than turning into a
   silent zero-loss-looking stall.
2. **V3.5 (WARN-3):** TIM2 no longer runs a 1 Hz PA8 card-detect sample in M1/M2 — the state machine
   does not consume card presence (§2.1, §5.3). TIM2 runs the 1 Hz VDD-window tick and the **10 ms
   producer-liveness flag (§3.2.7)**. It remains neither the sample clock nor an RTC/ADC owner (§2.4).

#### 3.2.6 SPI1 PIO/DMA quiesce sequence (V3.5 — blocker B2, normative driver rule)

`drv_lis3dsh` owns SPI1, and the sensor chain owns it **while DMA2's SPI1 streams are configured**.
Two accesses in the chain are polled (not DMA): the 2-byte `FIFO_SRC` read at EXTI entry and at chain
end, and the `FIFO_CTRL` `FMODE` write of the resync recipe. An F4 SPI peripheral driven by DMA while a
polled transfer is issued on the same peripheral does not "pause" the DMA: the first polled byte
returns the **last byte the DMA put in `DR`**. So the driver shall, **before every polled SPI access**:

1. confirm **both** SPI1 streams idle (`DMA_LISR` TCIF/HTIF/TIF settled, no EN bit on the stream, and
   the driver's own `transfer_in_progress == 0`);
2. clear `SPI_CR2.RXDMAEN` / `SPI_CR2.TXDMAEN` (and disable the stream requests);
3. flush the peripheral: wait `BSY == 0`, then read `DR` to drain, clear `CRCERR`/`OVR` if set;
4. perform the polled access (mode 3, CS asserted, `0x80|addr` / `0x00|addr` auto-increment per [DS]);
5. re-arm the DMA requests and the streams, then continue the chain.

If step 1 cannot be satisfied (a transfer is in flight), the driver **defers** the polled read to the
next safe point and lets the **level check** (§3.2.3) or the §3.2.7 liveness poll run it. A deferred
read is never a loss and never an over-read.

**M0 assertion:** with the DMA streams configured and armed, a polled read of `WHO_AM_I` returns
exactly `0x3F` — proving the quiesce works and that no stale DMA byte leaks into status reads. This is
part of M0's exit, not a nice-to-have.

#### 3.2.7 Producer-liveness poll (V3.5 — FLAW-6, normative)

The chain ends when `FSS < WTMP`, and everything after that depends on **another interrupt edge**. §3.2.3
makes "the level, not the edge" the authority — but that only helps **once an edge has been taken**. If
the watermark interrupt generator fails to re-arm (open question: [DS] does not state whether `P1_WTM`
pulses on the crossing or on every sample while `FSS ≥ WTMP`, and `IEL = 1` is *pulsed*), or if an edge
is lost to a spike on PB5, then nothing drains, the FIFO overruns every 20 ms, and **no counter moves**
— an undeclared 1600 samples/s hole in exactly the environment this logger runs in (flying leads beside
tunnel motors / VFD drives, a 24 MHz 4-bit SDIO bus, `IEA` polarity unresolved).

**Normative rule:** TIM2's 1 kHz supervisor raises a **10 ms flag**; in **app (thread) context** the
logger reads `FIFO_SRC` (through §3.2.6) and, if

```
src.FSS >= WTMP  and  (now − t_last_end_us) > 12 ms   →  invoke the same drain entry the EXTI uses
```

then `producer_liveness_miss_cnt++` and, if the condition recurs for **3 consecutive polls**, a resync
is forced (§3.2.1) and the supervisor raises LED 5. `12 ms` = the 5.0 ms nominal watermark period plus
the `iter < 4` chain bound plus margin: it cannot fire on a healthy chain (whose `t_last_end_us`
advances every ≈ 5 ms), and it fires within 12 ms of a dead interrupt path. Cost: one `FIFO_SRC`
read + one TIM2 read per 10 ms ≈ **0.05 % CPU** — free against the 4–8 % allowance (§9).

Consequences: (a) loss becomes **bounded and declared** instead of silent, which is the whole point of
K4 and F.6 §2; (b) the poll doubles as the fallback path if INT1 is not routed (§3.2.5) and as the
resume path after an SPI abort (§3.2.2); (c) `producer_liveness_miss_cnt` is a named counter in MAN and
TRAILER (A.4 / A.5), so F.6 §2 can always attribute a loss it produces; (d) hardware half — the
**1 kΩ + 1 nF RC on INT1** requested in §2.1 (§6.3) attacks the cause, the poll bounds the consequence.
Neither half may be dropped: the poll without the RC still loses edges (it just reports them), the RC
without the poll still leaves a silent failure mode.

### 3.3 Measurement quality: bandwidth, noise, range, self-test

| Item | Value at ODR 1600 Hz | Consequence |
|------|----------------------|-------------|
| Nyquist | 800 Hz | **V3.5 (FLAW-5), wording corrected:** content above 800 Hz is not "unreachable" — it is **unmeasurable at this ODR**, and its folding images at `\|k·fs − f_true\|` can land anywhere in 0…800 Hz, **including the trusted band**. The three-region taxonomy is normative in App. F.2 |
| **Trusted analysis band (V3.4)** | **≤ 400 Hz = fs/4** (`trusted_band_hz`) | **Rule: `fc = fs/4`.** The AA transition band must lie **strictly below** Nyquist; with the knee at 400 Hz the first folding image cannot land inside the trusted band with meaningful amplitude. 400–800 Hz is **outside** the trusted band — but per V3.5 it is **in-Nyquist and locatable**, not "an attenuated alias" (App. F.2 region ii) |
| AA bandwidth [DS §7.24] | 800 / **400 (selected)** / 200 / 50 Hz, rule BW ≤ ODR/2 | Filter **order and roll-off are unspecified** → the numbers below are a documented **2nd-order assumption**, and the alias A/B test (App. F.3) is what turns them into evidence |
| Aliasing exposure at **fc = 400 Hz** (2nd-order assumption) | −3 dB @400, −12.3 dB @800, **−19.1 dB @1200**, **−24.1 dB @1600**, −28 dB @2000 Hz | First folding image `f_true ∈ [1200, 1600]` lands at 0…400 Hz at **−19…−24 dB** (audit target **−19…−36 dB**); the image `f_true ∈ [800, 1200]` lands at 400…800 Hz, i.e. **outside** the trusted band, at −12…−19 dB |
| **Withdrawn (FLAW-3)** | fc = 800 Hz: −5.4 dB @1000, −7.8 dB @1200 | With the knee at Nyquist, `f_true ∈ [800, 1440]` folds into **160…800 Hz at 0…−12.3 dB**, i.e. real rig/bracket/tip-vortex structure contaminates **the interior** of the analysis band; a phantom peak at 600 Hz is indistinguishable from a mode at 600 Hz. This is why the 640 Hz "trusted band" of V3.3 was not a protection |
| Peak-location bias of the AA tilt | `s·σ²` with `σ ≈ f/2Q`; Q = 50 @600 Hz → σ ≈ 6 Hz → **≈ 0.2 Hz**; **≪ 0.02 Hz** below 400 Hz where the filter is flat | Amplitude consequences of the steeper filter are **irrelevant per the stated priorities** (frequency-domain work); the location bias at the trusted-band edge is negligible, and it is the *location* that the campaign measures |
| Noise density [DS §4] | **150 µg/√Hz typ — quoted only at `FS = 00` (±2 g)**, normal mode, ODR 100 Hz. **[DS] carries no per-range figure** (FLAW-3) | Over the 400 Hz AA band: `150 µg/√Hz × √400 Hz = **3.00 mg rms**` (unchanged from V3.4 — this is a *physical* rms and does not depend on the selected range). Referred to code at the new default ±6 g: `3.00 mg / 0.18 mg·LSB⁻¹ = **16.7 LSB rms**` (V3.4's "25 LSB at ±4 g" was computed from the mis-decoded scale). **If** noise density scales with full scale (an assumption, not a [DS] fact) the LSB-referred floor would stay ≈ 50 LSB. Which branch is true is **[BENCH]**: M0 measures the idle AA-band rms at ±4 / ±6 / ±8 g (§10.2, FLAW-3). Until then no LSB-noise figure in this document is a fact |
| Quantization | **0.18 mg/LSB (±6 g default, `0x50`)**; ±4 g would be 0.12, ±2 g 0.06, ±8 g 0.24 | 16.7 LSB rms ⇒ noise ≫ quantisation, **no dither needed** under either branch of the [BENCH] question above. Amplitude accuracy is out of scope (App. F), so the LSB floor matters only as SNR headroom |
| **Range (V3.5, FLAW-1 + FLAW-3, decision D-1)** | **campaign default ±6 g** (`FSCALE = 010`, `CTRL_REG5 = 0x50`) → clip threshold **±5.90 g** (0.18 mg × 32767). History: V3.3 ±2 g (±1.966 g), V3.4 *labelled* ±4 g while writing the ±6 g code | A ~3 m beam driven to a sharp resonance produces **1–5 g peaks at a mode** in ordinary tunnel testing. At ±4 g a held 4.2–5.0 g resonance clips **22.9–42.4 %** of the waveform (App. F.1 duty-cycle table) and every window containing the mode would be rejected — i.e. the resonance is lost. At ±6 g the 5 % budget survives to a ≈ **5.92 g** peak. Amplitude accuracy is declared out of scope, peak **location** is not; `ng_per_lsb` / `fs_g` in the HEADER carry the scale |
| **Range-selection policy (V3.5, FLAW-3)** | **60 s BOOT pre-test** (sensor at the default range, samples read, not logged) measures `max_abs_raw_max` (A.1 `0x0C8`) → **±8 g (`0x58`)** if peaks > 5.9 g; **±2 g (`0x40`)** only if peaks are *proven* < 1.9 g; otherwise **±6 g (`0x50`)** | A **BOOT-time decision**, recorded in the HEADER before `PREPARE` creates the file; there is **no runtime range switch** (§3.1). Escalation trades LSB headroom for headroom-against-clipping; because amplitude is out of scope and the AA-band rms is range-independent as a *physical* quantity, the move is essentially free for frequency-domain work **provided** the per-range idle rms measurement (below) confirms it |
| **Per-range idle noise — [BENCH] (V3.5, FLAW-3)** | **idle AA-band rms measured at ±4 / ±6 / ±8 g** — 3 lines of M0 time, no extra hardware | Decides whether LSB-referred noise is 16.7 LSB flat or ≈ 50 LSB range-scaled (§3.3 noise row). It is the *only* evidence for the claim "the range move is free": without it the SNR cost of ±6 g is an assumption in both directions |
| **Inter-axis time coherence (V3.5, FLAW-4)** | **X/Y/Z triples time-coherent to < 5° across 0…400 Hz — measured, not assumed** | Three per-axis FIFOs popped by one read ([DS:804-830]), `BDU` does not cover the pop path, `FSS` is one count per triple: a 1-sample skew is invisible to every CRC and counter in the container. 625 µs = **22.5° @ 100 Hz, 45° @ 200 Hz, 90° @ 400 Hz** — PSDs stay clean while mode shapes, coherence, OMA and cross-spectra become garbage. Gate: M0 cross-spectral phase (App. F.7); re-alignment window = the ≥ 100 ms discard (§3.2.1) |
| Zero-g offset / drift | ±40 mg typ, ±0.5 mg/°C | offset removal / high-pass is host-side; `OUT_T` logged per block (relative only) |
| Full scale | ±2 / ±4 / ±6 / ±8 g → 0.06 / 0.12 / 0.18 / 0.24 mg/LSB; **±16 g excluded** ([DS F1]: 0.73 mg/LSB would imply ±23.9 g on a 16-bit code) | Code clips at `32767 × LSB`: **±6 g → ±5.90 g** (`0x50`, default), ±8 g → ±7.864 g (`0x58`), ±4 g → ±3.932 g (`0x48`, not used), ±2 g → ±1.966 g (`0x40`). **The `FSCALE` field is `(ctrl_reg5 >> 3) & 7` — `000/001/010/011` = `2/4/6/8` g** ([DS:588-589]); the validator derives it (A.6 #11) |
| **`max_abs_raw` (replaces the binary CLIP flag)** | 1 byte per block = `min(255, max\|raw\| >> 8)`; clip **screening** ⇔ code = 127 (`\|raw\| ∈ 32512…32767`) | Saturation is data, not noise. The code is **range-independent** (it is a code magnitude); its **physical** meaning depends on `ng_per_lsb`: at ±6 g, code 127 ⇔ **5.85…5.90 g**, i.e. the clip point of the default range (at ±4 g it would have been 3.90…3.93 g). **V3.5 (FLAW-3) wording corrected:** at a held resonance this is **continuous waveform clipping**, not "a handful of transient samples" — screening leads to the **clipping duty cycle** test of App. F.1, and only *isolated* single-sample excursions are notched |
| Self-test [DS §4] | Δ ≈ 140 mg (X,Y), 590 mg (Z) typ; ST = 11 forbidden | BOOT: ST = 00 vs ST = 01, 32-sample averages, ≥ 100 ms settling discard; measured deltas stored as i16 mg. **Typicals only — no datasheet limits** ([DS F9]); the ±40 % band is a project decision, a miss is LED 11, not a rejection. **Self-test deltas are range-independent** (electro-mechanical stimulus, not code-domain): the **±4 g → ±6 g move changes nothing here and must not be "recalibrated" with it** — the same property is why the self-test gate could never catch FLAW-1 |
| Active current [DS §3] | 225 µA typ @1.6 kHz; 2 µA power-down | negligible in the power budget |

### 3.4 Ring buffer: latency arithmetic and SPSC invariants

| Event inside the card | Worst-case latency | Note |
|-----------------------|--------------------|------|
| multi-block page programming | 0.5–3 ms per 4 KiB | normal path; **dominates the per-block cost** |
| **wire transfer of one 4096 B block (8 sectors, 24 MHz 4-bit)** | **42.7 µs/sector → 342 µs/block** | **V3.4 correction (WARN-4):** the V3.3 figure of 20 µs/sector was wrong; 12 MB/s is the 4-bit 24 MHz rate |
| GC / wear-leveling stall | 50–200 ms, tail up to 400 ms | **the real enemy**, unschedulable |
| directory-entry update on `f_sync` | 20–200 ms | **not eliminated by pre-allocation** — cluster-allocation RMW is, a dirty directory entry is not (WARN-3, §4.4) |
| file create + pre-allocation | 0.2–2 s | `PREPARE` / `ROTATING` only |
| SD re-init + mount | 0.3–1.5 s | L1 (§5.3) |

At 9638 B/s: 100 ms → 964 B, 200 ms → 1928 B, 1 s → 9638 B. One 4096 B block = **425.0 ms**.

| Ring depth | D1 RAM | Coverage | Verdict |
|------------|--------|----------|---------|
| 2 | 8 KB | 850 ms | dies on a directory-update tail |
| 3 | 12 KB | 1.275 s | only on guaranteed pSLC |
| 8 | 32 KB | 3.40 s | survives GC, not re-init + mount |
| **16 (decision)** | **64 KB** | **6.80 s (15 usable = 6.375 s)** | survives L1 + clean-stop and a rotation |

```c
uint8_t ring[16][4096] __attribute__((section(".d1ram"), aligned(4096)));
```

4096 = 8 × 512 B sectors = 680 samples × 6 B + 16 B header, and divides evenly into 4/16/32 KiB
clusters. Alignment guarantees word-aligned DMA2 `INC4` bursts and bypasses FatFs partial-sector
copying for full-sector writes. **Because `f_write` is now capped at exactly one block, the
4096 B unit is simultaneously the FatFs transfer unit, the SDMMC DMA unit and the ring slot —
the three no longer need to be reconciled at run time.**

**Consumer write policy (this is what makes K6 derivable):**
- **Trigger: as soon as ≥1 complete block is published. Never wait to accumulate.**
- **Size: exactly 4096 B — one block per `f_write` call (V3.4, WARN-4).** The 13-bit
  `SDMMC_DTCTL NDATA[12:0]` counter tops out at 8191 B, so a 4…16 KiB request is only correct if
  the vendored `HAL_SD_WriteBlocks_DMA` happens to split it — an unverifiable dependency (no HAL
  vendored in this repo). Capping at one block removes it **and** removes the ring-wrap split
  entirely. A catch-up pass may issue **up to 4 separate 4096 B writes**, each with its own
  completion, CMD13 acknowledgement and `tail++`.
- `tail` advances **only after** the write is acknowledged *and* CMD13 reports ready. Failed or
  aborted writes leave `tail` untouched → retry re-writes the same slots.
- Consequence: in steady state the backlog is 1, so the typical in-flight set is one block. A
  4-block pass only occurs while catching up after a GC stall — hence the two-tier K6.
- **Capacity vs granularity (WARN-14):** the ≤ 4-block pass defines **recovery granularity**
  (K6, ≤ 1.7 s of published-but-not-durable data); the 15 free slots define **absorption capacity**
  (K5, 6.375 s of stall). They are different numbers for different questions and must not be
  substituted for one another at M1 acceptance.

| # | SPSC invariant | Enforcement |
|---|----------------|-------------|
| I1 | `head` written only by the producer, `tail` only by the consumer; all slot writes complete before publication | single-owner fields, `volatile` indices, `__DMB()` before `head++`, no critical sections |
| I2 | a slot handed to the consumer is not touched by the producer until `tail++` | 16 slots, one reserved; producer catches up only after 15 |
| I3 | **Race-free overflow rule (V3.4, WARN-9).** Overflow test `((head+1)&15)==tail`. The only physically unfree slot is at `tail` — i.e. the block the consumer may already own — so the producer **never touches it**. On overflow, when it attempts to publish, the producer **aborts its in-progress, unpublished block** and discards its ≤ 680 samples: `samples_lost_ring += 680`, `ring_ovf_cnt++`, `head` **does not move**, and a pending `OVERFLOW` + accumulated `lost_or_valid` is stamped into the **next block it does publish**. **A published block is never un-published; `head` never moves backwards.** | never into live data; version-proof: independent of how many blocks the consumer writes per pass |
| I4 | an empty ring is not an error but the trigger for `WFI()`; a discontinuity is legal only with a nonzero `lost_or_valid` or a PARTIAL block. **`FIFO_FULL` alone is not a discontinuity** | super-loop + host validator |
| I5 | **Index-domain rule (V3.4, FLAW-1).** The producer records **C** (captured) into `first_sample_index` at block completion; the consumer never rewrites it. Ring-discarded samples stay in **C** (they were captured) and therefore *must* be declared in `lost_or_valid` of the next published block, or the C↔S relation of §1.3.7 breaks and the gap becomes unlocatable | producer-only field; validated by A.6 #2 |
| I6 | **Mutual-exclusion rule (V3.5, FLAW-2 — normative producer rule).** `OVERFLOW` and `SENSOR_OVR` are **never both set in one block.** When both are pending at block completion: stamp `OVERFLOW` with the **ring-domain** `lost_or_valid` (the 680-per-block loss) into **this** block, and **carry the sensor-side deficit forward** — it is stamped into the **next block the producer publishes with `OVERFLOW` clear** (which, if the card is still stalled, is the next non-overflow block; a deficit carried past `LOSS_SAT` saturates as usual). Neither counter bookkeeping changes: `samples_lost_ring` and `samples_lost_sensor` still receive their own totals; only *which block carries them* is constrained. Why: `4096 = 680 × 6 + 16` leaves no room for a second field, and one `lost_or_valid` cannot mean two domains at once — A.6's two reconciliation sums (`Σ L(OVERFLOW) = samples_lost_ring`, `Σ L(SENSOR_OVR) = samples_lost_sensor`) become mutually satisfiable **only** under this rule | producer packer + `logcore` unit test; rejected by A.6 #3 as **file defect** |

**Data-plausibility screen — the SPI slew / spike gate (V3.5, WARN-9; log-only in M1/M2).** The container
proves what was *stored*; nothing in it proves what the sensor *sent*. A 7-byte mode-B frame carries no
link CRC, `crc32_payload` is computed after the bytes are already in the ring, and the electrical path is
flying leads to an external board in a vibrating tunnel. The only detector available that costs no new
hardware is the data itself, and the consumer already holds it:

| Item | Normative value |
|------|-----------------|
| Where computed | **consumer**, over the block's 680 triples before `crc32_payload`, reading samples already in `ring[tail]` — no new buffer, no new I/O |
| Test | consecutive-sample magnitude step `\|Δ\| = max(\|X_k−X_{k−1}\|, \|Y_k−Y_{k−1}\|, \|Z_k−Z_{k−1}\|)` compared against a bound |
| **Bound (derived, not a constant)** | A signal band-limited to `B = trusted_band_hz` with peak amplitude `A` obeys `\|dx/dt\| ≤ 2πBA`; sampled at `fs` this gives **`Δmax = 2π·B·A/fs = 1.571·A`** at `B = 400 Hz`, `fs = 1600 Hz`. `A` is taken per block from the producer's own `max_abs_raw` upper bound `A = 256·max_abs_raw·ng_per_lsb/10⁶ mg` → **`Δmax_lsb = 2π·400·256·max_abs_raw/1600 = 402·max_abs_raw` LSB**. Recorded values: a full-scale ±6 g block (`max_abs_raw = 127`) tolerates ≈ 51 000 LSB of step (i.e. no realistic step is flagged); a quiet block (`max_abs_raw = 4`, ≈ 185 mg) flags any step above ≈ 1 600 LSB |
| **Correction of the audit's bound** | The audit's "a 3 m beam cannot change 1200 mg in 625 µs at a 400 Hz band-limit" is **not** a band-limit derivation: `1200 mg` is the bound only for a ≈ 764 mg sustained peak. A fixed absolute threshold would flag legitimate near-full-scale samples, so the gate is **amplitude-referenced** as above. `UNCERTAIN` whether a tighter *mechanical* bound (mass × stiffness of the bracket, mode-shape curvature) is worth computing; not needed for M1/M2, which are log-only |
| Excluded from the gate | Blocks with **any** clipped sample (`max_abs_raw = 127` and `\|raw\| = 32767` verified): a clipped waveform is **by construction not band-limited**, so its steps are real and legal — flagging them would invent findings. Also excluded: windows inside a settling / resync discard (§3.2.1) |
| Accounting | `spike_cnt++` per offending triple, aggregated in **MAN `0x1C0`** and **TRAILER `0x06D`** (u16). **No per-block field exists (decision D-5)** — the block header is fully occupied (`4096 = 16 + 680 × 6`), so a spike byte would either break the geometry or force 679-sample blocks, which ripples through every 425.0 ms figure here. The **authoritative per-block spike table is produced host-side from the payload**, which the host already parses; firmware's job is only to make the aggregate visible in a run report |
| Action | **Log-only in M1/M2: no rejection, no notch, no flag in the block header.** Rejection policy is a host analysis decision (App. F.1/F.7) |
| Why this is not covered by the "drops are acceptable" latitude | K4 tolerates a *declared gap* because it removes data. An undetected spike **adds** spectral content that no counter can see: it produces broadband energy that Welch averaging will happily report as structure. That asymmetry is why the gate exists and why its counter must appear in the campaign report (App. F.6 §5) |

### 3.5 Memory map and the hard CCM/DMA rule

| Region | Address | Size | Masters | Residents |
|--------|---------|------|---------|-----------|
| D1 SRAM | `0x20000000` | 128 KB | core, DMA1, DMA2 (SDIO, SPI1) | ring 64 KB, SPI staging, FatFs `win[]`/`FIL.buf[]`, MAN/anchor buffers, diskio scratch, **MSP stack**, `.data/.bss` |
| CCM | `0x10000000` | 64 KB | **core only** | CRC-32 slicing-by-8 table (8 KB), state snapshot, calibration set, reset journal |
| Backup SRAM | `0x40024000` | 4 KB | core (`PWR.DBP`) | best-effort `last_utc`, reset log — **mirrored into the manifest**, never trusted alone |

**Architecture-level prohibition:** any buffer that (a) is handed to `HAL_SD_*_DMA`, (b) participates
in SPI1-DMA, or (c) is reachable from inside FatFs **must live in D1 and be 4-byte aligned**.
DMA2 cannot see CCM: a `0x1000_xxxx` address in `M0AR` ends in `TEIF` or garbage on the card.
*(Audit-verified correct — unchanged.)*

**Enforcement:**
1. **`main` stack (MSP) is in D1.** A CCM stack is a trap: any stack-local buffer passed to DMA
   would silently fail, and an identifier ban cannot catch locals. One MSP serves thread and
   handler modes; 2 KB, verified by paint-pattern watermark in M0/M3.
2. `_Static_assert((uintptr_t)buf …)` **does not compile** (an address is not an integer constant
   expression). Replaced by: a `.d1ram` output section + linker `ASSERT` on its bounds, a CI grep
   of the map file for DMA-visible symbols outside D1, and a runtime check in `diskio`.
   **V3.5 (B3) — this enforcement is currently *only* prose, and prose does not stop a migration.**
   The repo has no custom linker script, no `-Wl,-T`, no CI grep (`platformio.ini` as committed has no
   build flags at all), so **today the only live protection is the runtime `diskio` guard**. Required
   **before M1**, i.e. before DMA-facing buffers start multiplying, as a §10.2.1 repo precondition:
   (i) a `.d1ram` output section covering `ring`, SPI staging, `FATFS.win`, both `FIL.buf`, `man_buf`,
   `anchor_buf`, diskio scratch and the bounce buffer; (ii) `LD_FLAGS` supplying a linker script fragment
   that places `.d1ram` in `>RAM` and asserts its bounds (`ASSERT(... ) >RAM` style — the assertion must
   fail the **link**, not the run); (iii) a map-file / `-fstack-usage` check that no symbol handed to a
   DMA API and no stack frame exceeding the 2 KB MSP budget lands outside D1. Until those three exist,
   "enforced" in this document means "not enforced".
3. **`diskio` guard:**
   ```c
   bool dma_ok = (p >= 0x20000000u) && (p + len <= 0x20020000u) && ((p & 3u) == 0u);
   ```
   If false → bounce through a 4096 B `aligned(4)` D1 scratch **in a chunking loop** (`disk_write`
   may be called with up to 128 sectors; with the one-block write policy of §3.4 the logger itself
   never asks for more than 8 sectors, but FatFs' own window traffic still can).
4. **CRC table in CCM is non-`const` and generated at boot** (~1 ms), avoiding startup copy surgery.
5. MPU (8 regions) guards vectors and inactive regions from rogue **core** writes. It cannot stop a
   DMA master — the CCM rule stays a matter of link layout, CI and the M2 soak test.

---

## 4. File Format & Data Structure

### 4.1 Volume and file system requirements

| Item | Value | Why |
|------|-------|-----|
| File system | **FAT32**, no exFAT | FatFs R0.12c has no exFAT → a >32 GB card as shipped will not mount |
| Cluster / sector | **32 KiB / 512 B** | 4096 B blocks divide evenly; on a 32 GB card the FAT shrinks from 32 MB to ~4 MB, speeding `f_expand` (128 MiB part = 4096 clusters = 32 FAT sectors). **Pre-allocation, not cluster==block equality, is what removes cluster-allocation RMW** |
| Partition | one, start aligned to **4 MiB**, formatted with the SD Memory Card Formatter | misaligned start turns every boundary into a GC event |
| Card | 8–32 GB industrial **pSLC**, U1/A1 or better, **never filled above 80 %** | consumer TLC near full produces second-long stalls no RAM can hide; power-loss behaviour matters for K6 |
| FatFs options | `_FS_REENTRANT=0`, `_FS_TINY=0`, `_MIN_SS=_MAX_SS=512`, `_USE_LFN=0`, **`_USE_EXPAND=1`**, `_FS_READONLY=0`, `_FS_NORTC=0`, `_FS_LOCK=0`, `_CODE_PAGE=437`, **`f_mount(opt=1)`** | single-task FS; 8.3 names only; `_USE_EXPAND=1` is required for `f_expand()` to compile. **V3.4:** `f_truncate`/`f_expand` availability is unproven for this pre-R0.13 option naming and the sources are **not vendored** — `f_truncate` is therefore **unused** (WARN-2), and `f_expand` has a mandatory `f_lseek` fallback (§4.4) |
| `get_fattime()` | implemented from RTC, honouring `clock_valid` (§7) | otherwise every directory entry says 2000-01-01 |
| Deletion | **host-side only**, auto-delete forbidden | deleting while logging fragments the FAT; V3.4 moves all orphan/unclean-file handling to the host tool as well (§4.3) |
| Volume full | refuse a new session at **≥80 % used or <256 MiB free** → LED 9 | consistent with the card rule; capacity tables in **Appendix G** |

### 4.3 Manifest, anchors, trailer, and `RESCUE`

| Artifact | Mutability | Role |
|----------|-----------|------|
| HEADER struct (512 B) | written once in PREPARE (and re-written once at finalise) | immutable creation facts |
| HEADER anchor log (3 × 512 B) | **append-once — each sector written exactly once** (V3.4) | time-scale history that survives MAN overwrites |
| BLOCK headers | written with each block | per-block identity, CRC, loss attribution, `max_abs_raw` |
| `V3LOG.MAN` (2 × 512 B) | rewritten in place (file size never changes) | durable progress checkpoint |
| TRAILER (512 B + pad) | once, at clean stop/rotation | **declared extent** + final totals + "closed properly" marker |

**Manifest.** Slot index = `commit_seq & 1`; each commit writes exactly **one 512 B sector** at
offset `0x000` or `0x200`. The file size never changes → FatFs touches neither the FAT nor the
directory **for this file**; a directory-entry update may still be issued by `f_sync` on the BIN
file (§4.4, WARN-3). On boot: read both slots, validate `man_crc32`, take the valid slot with the
**higher `commit_seq`**.

**Cadence and overhead:** MAN every **64 blocks = 27.2 s** and on every state transition.
512 B / 27.2 s = 18.8 B/s = **0.196 %**; `f_sync()` right after adds another 512 B → **≈0.39 %**;
the anchor sector adds 512 B / 108.8 s = **0.05 %**. Total metadata ≈ **0.44 %**. (Unchanged; the
arithmetic already carries the one directory-sector RMW per commit that WARN-3 makes explicit.)

**Anchor log — linear, append-once (V3.4, WARN-1).** The MAN keeps only the latest 8 anchors and
every commit overwrites the slot, so anchor *history* would be destroyed; anchors are therefore
persisted in the HEADER padding — but **not** by rewriting a sector in place:

- Anchor = `{rtc_ms u64, part_captured_index u32, commit_seq_lo16 u16, part_index u16}` = 16 B (X3).
  `part_captured_index` is a **C-domain** index (§1.3.7). **V3.5 (WARN-1): `part_index` is written as the
  constant `0` for M0–M2** — rotation is compile-gated off and a part can never fill, so the field is
  inert. It stays in the 16 B entry because the v4 layout is frozen; making it a constant is not the
  same as removing it, and a reader must still be able to prove an anchor belongs to this part.
- Cadence: every **4 MAN commits = 256 blocks = 108.8 s**.
- **Mechanism:** 31 anchors are accumulated in a D1 buffer; when the 31st is accepted the whole
  512 B sector is written **once** to the next free anchor slot and the RAM buffer is reset. A
  sector is never rewritten, so there is no read-modify-write of sector 1 of the BIN file (V3.3's
  31 in-place rewrites all fell inside the card's first erase group and dragged the header and
  first block sectors through it on every append).
- **Capacity:** 3 sectors × 31 entries = **93 anchors = 2.81 h** at the 108.8 s cadence — enough
  for the 1–2 h campaign (a 2 h run produces ~66 anchors). If a part ever runs past 93 anchors, the
  log continues into the HEADER's zero pad (`0xE00…0xFFF`) **append-once**, up to the 217 entries
  V3.3 allowed. `count` + `sector_crc32` are kept per sector; the MAN's rolling 8-anchor window
  still covers the tail, so the recovery rule is simply *"last sector with a valid CRC, plus the
  MAN window"*.
- **Rejected alternative (recorded):** moving anchors to a separate append-only `V3LOG.ANC` file —
  cleaner still (the BIN header would then be written exactly twice), but it adds a file, a
  directory entry and a second durability path. Reconsider only if a second erase-group effect shows
  up in M2 wear/latency measurements.
- Anchors that fail the firmware plausibility filter (§7) are **never written**; `anchor_reject_cnt`
  records the rejection.

**`RESCUE` at boot (V3.4: bounded, cooperative, single-file).** RESCUE does exactly one thing: make
the **active file** appendable again without destroying declared gaps. Everything that needs the
directory, another part, or a full-card scan has moved to the **host validator** (cut §3).

1. `f_mount`. If no card → IDLE + LED 2 (not an error loop).
2. Read both MAN slots; pick max `commit_seq` with valid CRC.
   **If both slots are invalid: do not sweep the card.** Report `MAN_INVALID` + LED 3, set
   `rescue_cnt`, and let the host tool rebuild (HEADER scan + forward scan + manifest rewrite).
   Rationale: the MAN + HEADER + forward scan already locate everything a 1–2 h campaign needs;
   the 13 s boot scan and its "both-slots-invalid" full-card fallback existed for multi-day
   endurance runs nobody is running here.
3. If `clean_stop_flag == 1` → nothing to recover; go to IDLE. *(No `intent_valid` check: rotation
   is compile-gated off, §4.5.)*
4. Otherwise open `active_filename`, verify `card_cid` and HEADER (`hdr_crc32`, `session_id`, `part_index`).
5. **Forward block scan from the MAN's `byte_size`** (the checkpoint may be up to 64 blocks behind):
   validate `hdr_crc8`, `crc32_payload`, `block_seq == expected` (i.e. `S_{k+1} = S_k + 680`) and the
   **C-domain rules of §1.3.7**: accept `C_{k+1} = C_k + 680` **and** accept
   `C_{k+1} = C_k + 680 + L_{k+1}` when `OVERFLOW` is set or `lost_or_valid > 0` with the matching
   flag — **a declared gap is not a scan failure** (V3.4, FLAW-1). Stop at the first
   **undeclared** failure (bad CRC, wrong `block_seq`, an unexplained C-jump, or a C-jump with no
   flag). Bound the scan by `max_data_blocks`. **Feed the IWDG on scan progress** (pointer advanced
   ≥8 blocks), at most once per 500 ms — a full 128 MiB scan is ~13 s of reads.
6. **Declare, do not destroy (V3.4, WARN-2).** Record `true_valid_extent` and resume appending from
   it. There is **no `f_lseek → f_truncate`**: the pre-allocated file is never shrunk, the valid
   extent is carried by MAN `byte_size` (and TRAILER `final_file_size` on a clean stop), and the
   host stops at the first invalid block. This removes a destructive operation, a FatFs-version
   dependency and an entire fuzz class, and it makes a re-cut after a half-finished RESCUE idempotent.
7. Commit the manifest with `recovered_unclean=1` and the recovered extent. Only then may a new
   session file be created.

**Counter mirrors: data, not acceptance gates (V3.5, WARN-4).** `samples_lost_*`,
`part_captured_samples`, `ring_ovf_cnt`, `anchor_reject_cnt`, `stall_max_ms` and the `max_abs_raw` /
anchor-extent summaries are deliberately stored in **four** places (block headers, anchor records, the
HEADER extended block `0x0C0…0x1FB`, TRAILER) plus the dual-slot manifest. That redundancy is kept
because each copy is cheap and because a partially written file is read with whatever survived. It is
**not** a set of acceptance criteria: the requirement that "MAN, the anchor log, the HEADER extended
counters and the TRAILER must all re-derive to the same totals" is a **host-side regression test** on
`logcore` / the Python validator, run against synthetic and corrupted images (§10.4). The **minimum
sufficient acceptance chain** for a campaign file is:

1. per-block `hdr_crc8` + `crc32_payload` verify, flags and `C` are internally consistent (§1.3.7, A.6);
2. **one** valid manifest slot (dual-slot kept as cheap insurance — 2 × 512 B every 27.2 s is the cheapest
   durability in the whole design and is not up for stripping);
3. the TRAILER's **declared extent**, or the forward-scan extent when there is no TRAILER.

Anything a file can pass *without* cross-artefact agreement is reported as a diagnostic mismatch, never
as corruption — the M2 cut schedule is 30–40 cuts at 4 phases precisely so that "every mirror agrees"
does not become 40 more cut-phase assertions (the audit counts each mirror as one validator branch and
one cut-phase; that is where the schedule bloat came from).

**Removed from firmware, moved to the host validator (`logcore` / Python):** directory sweep over
all `V3??????.BIN` looking for unclean parts, resolution of a dangling rotation `intent_valid`
record, and the both-slots-invalid full-card rebuild. The host already has to parse the container
byte-exactly (K11), so this is where those paths belong and where they can be unit-tested
(App. B). Firmware consequence, stated honestly: an orphan 128 MiB `V3??????.BIN` from a cut
rotation **survives on the card** until the host cleans it, and PREPARE's id allocation
(Appendix G) will skip past it. Accepted for a 1–2 h campaign with rotation gated off.

**K6 derivation (honest).** Lost at a cut = (a) the partially filled producer block (≤679 samples =
≤425 ms) + (b) published-but-not-durable blocks (**≤ 4 in-flight under the ≤ 4-write pass = ≤ 1.7 s**,
WARN-14) + (c) the card's internal volatile cache (**outside the bound**). Typical steady state:
(b) = 1 → ≤850 ms. Compare K5: **6.375 s of total absorption capacity is a different quantity.**

### 4.4 Pre-allocation

Without pre-allocation FatFs read-modify-writes a FAT sector at every cluster boundary — with
32 KiB clusters that is one RMW every 3.4 s, 20–200 ms each, colliding with GC.

| Criterion | plain growth | `f_lseek(size)` fallback | **`f_expand(&fil, bytes, 1)`** |
|-----------|--------------|--------------------------|-------------------------------|
| FAT **cluster-allocation** updates during recording | hundreds/day | 1 at start + 1 at stop | **0** |
| Fragmentation risk at 80 % full | high | medium | contiguous run, else fallback |
| Creation cost | ~0 | ~0 | 0.2–2 s, in PREPARE/ROTATING |
| Power-loss artefact | growing file, FAT may lag | hole in the chain | full-size file with a stale tail → cured by RESCUE |

```c
f_open(&fil, name, FA_CREATE_NEW | FA_WRITE | FA_READ);
if (f_expand(&fil, prealloc_bytes, 1) != FR_OK)      /* ANY failure, not only FR_DENIED */
    f_lseek(&fil, prealloc_bytes);
f_lseek(&fil, 4096);                                  /* never trust fptr after expand */
```

Verify in the vendored `ff.c`: `f_expand` requires `fptr == 0 && objsize == 0`, the meaning of `opt`,
and whether it moves `fptr`. The explicit `f_lseek` makes the answer irrelevant. **Until `ff.c` is
vendored this is an open M2 item, not a verified fact.**

**Rule: no FatFs cluster allocation may ever occur inside `RECORDING`.**

**Wording correction (V3.4, WARN-3).** V3.3's "0 FAT/RMW updates during recording" is **stronger
than what pre-allocation buys**. Pre-allocation removes *cluster-allocation* RMWs. `f_sync()` after
each MAN commit can still mark a **directory entry** dirty, producing one 512 B directory-sector RMW
per 27.2 s. The overhead arithmetic (§4.3, the "+512 B" of `f_sync`) already accounts for a sector
per commit, so the **numbers stand**; only the claim is narrowed. Closure: read `f_write`/`f_sync`
`OBJ_DIRTY` handling in the vendored `ff.c` at M2 and, if a directory write is issued on every
commit, batch it (e.g. `f_sync` every other commit, ring headroom permitting) — measured, not assumed.

### 4.5 Rotation — kept, **compile-gated off** (`V3_ENABLE_ROTATION = 0` for M2/M3)

**Decision anchor (audit §3):** a part is 134.2 MB = **3.868 h**; a 2 h campaign writes ~69 MB, so
**rotation never triggers**. 128 MiB pre-allocation is kept precisely so that rotation can stay dead
code. With the gate off: the `ROTATING` state, the intent fields (`intent_valid`,
`next_filename`, `next_part_index`), the crash-point table and the RESCUE intent resolution are all
compiled out. **V3.5 (WARN-1): the `part full` exit is *not coded* in `RECORDING` for M0–M2.** A part
can only fill after 3.868 h of continuous recording; the longest campaign in this schedule is 4 h at
69.4 MB written — a **1.93× margin** — so the branch is unreachable and every identity computation
behind it (`part_index++`, per-part regression branching, `part_index > 255`, the 256× `FR_EXIST`
ladder) is inert. `part_index` is a **constant 0** in HEADER, MAN, anchors and TRAILER. What *is* kept
implemented is the **25-bit C-index capacity guard** (A.3 note 2): its trigger — a card dead long enough
that the producer keeps dropping blocks — *is* reachable, and `stop_reason = 7` is its named outcome.
`stop_reason = 6 part_full` remains a defined enum value (the container is frozen) that a v4 file from
M0–M2 will never contain. A campaign is over long before either guard fires.

**`ROTATING` (gate ON) — the producer never stops.** Entered when the last block of a part
(`block_seq == max_data_blocks-1`) has been published:

| # | Action | Manifest state after |
|---|--------|----------------------|
| 1 | commit **intent**: `intent_valid=1`, `next_filename`, `next_part_index`, `state=ROTATING` | intent durable |
| 2 | write current part's TRAILER (final totals + **declared extent**) | — |
| 3 | `f_sync()` → `f_close()` — **no `f_truncate`** (V3.4): the file keeps `prealloc_bytes`; the extent is the declared value | old part logically closed |
| 4 | commit: old part `clean_stop_flag=1`, intent still valid | old part closed |
| 5 | `f_open(FA_CREATE_NEW)` next part → `f_expand` → HEADER + anchor sector 0 → `f_lseek(4096)` | — |
| 6 | commit: `active_filename` = new, `part_index++`, `intent_valid=0`, `clean_stop_flag=0` | new part active |
| 7 | back to `RECORDING` | — |

**Crash-point resolution (host-side once the gate is off; firmware-side only when it is on):**

| Cut between | Recovery |
|-------------|----------|
| 1–2 | old part still active, intent valid → forward-scan old part, declare `true_valid_extent`; ignore `next_filename` if absent |
| 2–4 | old part has a TRAILER but `clean_stop_flag=0` → verify trailer CRC; if valid, adopt the **declared extent** in the trailer and mark clean |
| 4–5 | old part clean; if `next_filename` exists it is an orphan (4096 B header only) → adopt by HEADER and resume from `byte_size = 4096`, or leave it for the host and create the next part |
| 5–6 | new file exists with a valid HEADER but the MAN names the old one → adopt the new file by HEADER, resume from `byte_size = 4096` |

**Budget (gate ON):** trailer + sync + close ≈ 0.05–0.5 s (the 32-FAT-sector truncation pass of V3.3
is gone); `f_expand` ≤ 2 s; 3 sector writes ≈ 5 ms → **≤ 2.6 s worst case against a 6.375 s ring**.
Measured in M2 **only with the gate enabled** (2 synthetic cuts, cut §3). If exceeded, the ring
overflows and the loss is counted — never silent, and now race-free (§3.4 I3). IWDG fed on each
sub-step.

**Capacity, session/part identity allocation, `part_index > 255`, and the 256×`FR_EXIST` refusal
rules are unchanged and now live in Appendix G** (document rule: nothing is deleted, the active-path
text is just no longer padded with endurance-run tables).

---
## 5. Reliability & Graceful Recovery

### 5.1 IWDG — the "proven progress" policy

| Parameter | Value | Remark |
|-----------|-------|--------|
| Clock / prescaler / reload | LSI 32 kHz nom, /256, reload **1000** → **8.00 s** nominal | independent of HSE/PLL |
| Real timeout | **5.45 … 15.06 s** (LSI 17…47 kHz) | budget the worst case 5.45 s |
| Who feeds | **consumer only** (super-loop) | feeding from an ISR turns the watchdog into decoration |
| Feed condition | ≥1 block committed since the last feed, **or** a live 1 Hz state tick in IDLE/ERROR/L3, **or** a completed recovery/rotation/scan sub-step | progress, not "the loop is spinning" |
| Long operations | `f_expand`, SD re-init, RESCUE forward scan: cooperative feed at most once per 500 ms, **conditioned on measurable advance** | a legitimate multi-second operation must not look like a hang |
| Reset cause | `RCC_CSR` → 16-entry journal in Backup SRAM **and mirrored into `V3LOG.MAN`** | a reset is never silent; LED 8 on the next boot |

Budget check: L1 (1.5 s) + clean-stop (1.0 s) = 2.5 s against 5.45 s worst case → **>2× margin**.
*(V3.4: the L2 rung no longer performs a truncation pass, so its budget is tighter, not looser.)*

*(M0 step 5, implemented 2026-11-20: with the IWDG armed, `app_error_halt()` — diagnosed and deliberately **non**-feeding — no longer halts forever but terminates in an IWDG reset; the next boot decodes the cause from `RCC_CSR` (IWDG/BOR) before software writes `RMVF`, stores it, and announces **LED 8**, and bench access to a chip sitting in that halt is connect-under-reset via NRST. **Feed conditions unchanged** — this is a consequence of arming the watchdog, not a new feed rule, and the halt still proves no progress, which is exactly why it is allowed to expire.)*

### 5.2 "SDIO returns early": DAT0 busy and the timeout budget

`HAL_SD_WriteBlocks_DMA` completes its DMA event **while the card is still programming and holding
DAT0 low**. The wrapper explicitly polls `CMD13` (`READY_FOR_DATA` bit 8 = 1 and
`CURRENT_STATE != PRG`) after every write completion. Busy is a **normal state**, metered as
`stall_max_ms`. Poll with backoff (1 ms, then 5 ms).

| Operation | Own timeout | On timeout |
|-----------|-------------|------------|
| CMD with CRC retries | 3 × 10 ms | CMD12 STOP_TRANSMISSION → L0 |
| READY_FOR_DATA (CMD13) wait | **450 ms** | L0, record `stall_ms` |
| `f_write` of **4096 B (one block, one call)** | 500 ms per call | L0; 3× → L1 |
| `f_mount` | 500 ms | clean-stop / L3 |
| SD re-init (400 kHz → 24 MHz, 1-bit → 4-bit) | 1500 ms | clean-stop → L3 |
| `f_expand` (PREPARE/ROTATING only) | 5000 ms + 500 ms heartbeat | fallback of §4.4 |
| RESCUE forward scan | unbounded total, feed every ≤500 ms on progress | bounded by `max_data_blocks` |
| **SPI1 sensor frame (mode B)** | **200 µs** per 7-byte frame | **SPI error handler → resync (§3.2.2)** |

**V3.4 (WARN-4):** the `f_write` timeout is now quoted **per 4096 B block** rather than per
"4…16 KiB" batch, because one `f_write` is one block and the SDMMC transfer counter can never be
asked for more than 8 sectors (8 × 512 B = 4096 B ≪ the 13-bit 8191 B limit). M1 exit additionally
requires inspecting `SDMMC_DTCOUNT` / `DMA_LISR` (or `SDIO_IDATAR`/`SDIO_DCOUNT` per the vendored
HAL naming) during a full-length write burst to prove no request ever exceeds the counter, and that
the HAL, if it splits, does so on sector boundaries.

### 5.3 Recovery ladder and ordered graceful shutdown

**V3.4 collapse (cut §3 + WARN-2).** V3.3 defined a five-level ladder whose middle rung performed
`f_lseek → f_truncate → trailer → …`. **`f_truncate` is gone** (availability unproven for this
FatFs option set and destructive for a pre-allocated file), the rungs that existed to serve
multi-day endurance and rotation recovery are demoted to host tooling, and what remains is
**three active rungs plus two terminal states**:

| Level | Trigger | Actions, strictly in order | Budget | Outcome |
|-------|---------|----------------------------|--------|---------|
| **L0** | single CRC/timeout/GC tail | CMD12 → poll CMD13 ready → retry the same block (≤3) | ≤50 ms | recording continues, zero loss |
| **L1** | repeated write error (3× L0) | abort DMA2 S3 + clear SDIO flags → `HAL_SD_DeInit` → init 400 kHz → 4-bit/24 MHz → **rebuild FatFs context** → resume from the ring | ≤1.5 s | ring absorbs it; `reinit_cnt++` |
| **L2 → "clean-stop"** | L1 failed / FAT error / **VDD < 3.12 V in 3 consecutive 1 s medians** (§6.2) | **declare the extent**: TRAILER (final totals + `final_file_size` = declared extent) → `f_sync` → `f_close` → unmount → ERROR + LED 3. **No truncation, no directory sweep, no re-attempt of a failed step beyond one retry** | ≤1.0 s (best-effort) | session closed properly; otherwise §4.3 RESCUE finishes it |
| **L3** *(terminal state, not a recovery rung)* | card not responding (**2× L1**) — **software detect only in M0/M1 (V3.5 WARN-3)**; "card removed" is inferred from a failed init / failed CMD13, **not** from PA8 | unmount, enter ERROR (**fed by the 1 Hz tick**), producer keeps running, poll card presence 1 Hz **by re-init attempt, not by pin read**; on re-insert → BOOT → RESCUE | — | further data counted as explicit loss, **no L4** |
| **L4** | core genuinely hung | IWDG reset | 5.45…15.06 s | **forbidden as a treatment**; always journaled |

**What the ladder deliberately does *not* discriminate (V3.5, WARN-3).** V3.4 routed CRC/timeout-with-CD
-versus-CD-released between L0/L1 and L3 on the strength of a pin whose wiring is unresolved. That
discriminator is **out of M0/M1**: a card pulled mid-write and a contact-bounced socket converge on the
same ladder anyway (`L0 ×3 → L1 ×2 → L3 + LED`), and the only cost of not discriminating is that a
vibration-induced dropout runs two re-inits it might not have needed — bounded at ≤ 2 × 1.5 s ≈ 3 s of
ring (against 6.375 s of absorption) and fully counted. If the M0 bench confirms PA8, the pin may be
sampled at 1 Hz and recorded (`card_cd_state`, `cd_change_cnt`) **as a diagnostic that annotates F19's
counter signature** — it must not select a rung. `F19` (socket shake, M2) is therefore evaluated on
`sd_err_cnt` / `reinit_cnt` / `ring_ovf_cnt` **growth shape**, not on a CD state. Mechanical retention of
the socket remains the real mitigation (§6.3).

**FatFs context rebuild after any re-init (mandatory).** A `FIL` caches `clust`, `sect` and a sector
window; resuming `f_write` on a stale `FIL` can write to the wrong LBA. Therefore:
`f_mount(NULL,…)` → `f_mount(&fs,…,1)` → `f_open(active_filename)` → `f_lseek(committed byte_size)`
→ re-write the in-flight blocks **from the ring** (`tail` only advances after acknowledgement).

In `RECORDING`/`ROTATING`, `NVIC_SystemReset()` and `f_mkfs()` are strictly prohibited responses to a
write error.

---

## 6. Power Management

### 6.1 Energy budget (1S LiPo → board LDO 3.3 V)

| Consumer | RECORD @84 MHz | IDLE (STOP) | Note |
|----------|----------------|-------------|------|
| STM32F405 (core, DMA2, SDIO, SPI1) | 22–30 mA | 20–45 µA | 2 WS flash; no PLL switching |
| MicroSD 4-bit writing | 25–60 mA (100 mA peak) | 10 µA–1 mA (card-dependent) | measure with the card inserted, after a write burst |
| LIS3DSH @1.6 kHz | 0.225 mA | ~2 µA (power-down) | low-power modes forbidden during capture (noise) |
| LED PB2 | 2–5 mA pulsed, duty ≤0.5 % | 0 | |
| LDO + misc | 0.1–0.5 mA + quiescent | 40–100 µA | ~79 % efficient at 4.2 V |
| STOP wake (PLL restore ~2 ms / 5 s) | — | ≈10 µA average | must be counted |
| **Total** | **~50–90 mA** | **~0.1–0.3 mA** | **≈12 h typical on 1000 mAh (K9 ≥8 h — *unqualified in V3.4*, see K9/§10.2 M4)** |

### 6.2 Supply monitoring — VDD via VREFINT (primary and only rail metric)

**Battery voltage is not measured.** VDD is the rail that actually fails and needs zero external
components. **ADC1 is owned entirely by the consumer (X2)** — TIM2 only raises a 1 Hz flag.

- `vdd_mv = 3300 × VREFINT_CAL / adc_raw`, `VREFINT_CAL` @ `0x1FFF7A2A`, sanity 1350…1650,
  fallback 1502 + `vrefint_cal_valid = 0`.
- **Slow path — the only path implemented in M0/M1/M2 (V3.5, WARN-2):** once per second the consumer runs
  16 conversions with 480-cycle sampling (23.4 µs each) → **median of 16**. This statistic alone drives
  policy, the `vdd_x10mv` block field and the `vdd_min_mv` MAN/TRAILER logging. Cost 16 × 23.4 µs =
  375 µs per 1 s ≈ **0.045 % CPU**.
- **Fast droop path — removed from this cycle (V3.5, WARN-2).** V3.4 placed 84-cycle conversions
  (4 µs each, 4.57 µs/conversion → ≈ 219 kSps, 100–650 samples across a 0.5–3 ms write) *inside*
  `f_write` and the CMD13 busy-wait — i.e. precisely when the card draws its 25–100 mA peak, the most
  pessimistic possible reading of an LDO-fed rail and the least representative of the cell. The whole path
  leaves M1/M2: **the ADC does not run inside the write loop at all**, and ≈ **0.24 % CPU** returns to the
  §9 budget. It is **reinstated only with M4 brownout qualification** (§10.2): droop resolution is a
  product-qualification question, and §6.1 already budgets 50–90 mA on 1000 mAh → **11–20 h** of autonomy,
  which a 2 h campaign cannot exhaust.
- **Attribution — the one-block (425 ms) lag rule is deleted with it (V3.5, WARN-2).** `vdd_x10mv` in a
  block header is **the latest 1 s slow median at the moment that block is written**: a window statistic,
  not an event. There is no "block N−1's droop was recorded in block N" rule for the host to undo, and no
  425 ms attribution error to propagate. Detection latency where it matters is unchanged: a 1 s median with
  N = 3 debounce still catches a genuine droop in ≈ 3 s, and PVD remains the last line either way.
- **Policy rule (V3.4 WARN-10, restated for V3.5): decisions use the 1 s slow-path medians only, and only
  after N = 3 consecutive medians below the threshold** — ≈ 3 s of confirmed droop with hysteresis ±100 mV.
  V3.4's "the fast path never triggers a state transition" is now satisfied trivially, because in this cycle
  the fast path does not exist at all. Rationale unchanged and still correct: a single median below 3.12 V
  triggering a planned `FLUSHING` can end a good 2 h campaign while 11–20 h of autonomy are available,
  which is a pure loss. On an LDO-fed rail a *median* is also the more honest statistic: it is not taken
  only during the card's current peak.
  - **3 consecutive medians < 3.22 V** → LED 6 warning (recording continues).
  - **3 consecutive medians < 3.12 V** → planned `FLUSHING` (clean-stop).
  - **PVD 2.90 V** → flag only (§6.3), the last line of defence, unaffected by the debouncing rule.
- **Encoding cap (V3.5, WARN-2): a property of the frozen field, not a policy to manage in M1/M2.** The
  block header field is `vdd_mv = 2000 + 10 × vdd_x10mv`, `0` = not measured ⇒ it **saturates at
  3275 mV** (`vdd_x10mv = 127`). Firmware clamps above the cap and never wraps; the validator reads `127`
  as "≥ 3.275 V", never as a measurement, and the field may only ever evidence *droop*, never health.
  **V3.4's cliff *handling* (fast-path minima, per-window droop history) leaves with the fast path**;
  qualifying the thresholds against a real cell is M4's job.
- **What VDD cannot tell you (WARN-10):** on an LDO-fed board VDD says nothing about the cell —
  a 3.0 V LiPo under load still regulates to 3.3 V until the LDO drops out. So the brownout
  thresholds are a **product qualification task** (M4, now post-campaign), **not** a prerequisite of
  a 2 h instrumented run.

### 6.3 Sleep, wake-up, and the honest limits of power-loss handling

- Deep sleep forbidden in `RECORDING`/`ROTATING`. `WFI()` is legal only when the ring has nothing to write.
- `IDLE` uses **STOP** + LSE/RTC 5 s alarm + rising-edge EXTI on PC13. On every wake,
  `bsp_clock_restore()` re-runs the PLL configuration and `HAL_InitTick()`. PC13–PC15 are never
  outputs in STOP.
- Clock-gate/power-down everything unused: USB OTG FS + PHY, UART, ADC between triggers, SPI1 in IDLE.
- **PVD is not a guarantee.** It monitors VDD and buys ~1–5 ms depending on bulk capacitance —
  enough to set a flag, kill the LED and abort new writes, not to write a trailer. Integrity rests
  on the manifest + forward scan.
- Hardware requests for a future revision (not blockers): ≥ 470 µF on 3.3 V, slot power switch,
  battery divider or fuel gauge, VBAT coin cell/supercap, confirmed INT1 routing, **mechanical
  retention of the microSD socket** (vibration rig, §10.1), **bench confirmation of the PA8 card-detect
  wiring** (§2.1), **1 kΩ + 1 nF low-pass RC on INT1 at the MCU input (V3.5, FLAW-6 — τ ≈ 1 µs against a
  5 ms watermark period; the schematic half of the producer-liveness defence, §3.2.7)**.

---

## 7. RTC & Time Base

**Two independent domains.** Sample instants live in the sensor ODR domain; the RTC is a wall-clock
anchor. RTC drift never creates jitter or gaps, and the residual ODR error is *measured*. The RTC
prescaler chain (LSE 32768 → `SSEL = 127` → F1 = 256 Hz → `F100 = 255` → 1.000000 Hz, SSR LSB
**3.906 ms**) is arithmetically exact and audit-verified.

### 7.1 Anchor acquisition (R3 + X1)

**Single owner: the producer.** Nobody else reads the RTC while the producer runs — a P6 ISR
preempting a P7 RTC read would tear the F4 shadow-register lock protocol, and double-read alone
cannot fix that. Consumers use the published anchor + TIM2 elapsed.

At block completion, inside the TC ISR:

```
repeat up to 3 times:
    (TR1, SSR1, DR1) = read RTC_TR → RTC_SSR → RTC_DR      /* F4 order: reading time locks
                                                            shadow regs until DR is read */
    (TR2, SSR2, DR2) = read again
    if (TR1==TR2 && SSR1==SSR2 && DR1==DR2) -> accept
else: skip this anchor (anchor_reject_cnt++); the next block will produce one

t_latch_ms = to_ms(DR, TR, SSR)                            /* SSR quantization 3.906 ms */
t_last     = t_latch_ms - (FSS_after + burst_tail_after_boundary) * 625 µs
```

Cost ≈ 1–3 µs per block at 2.35 blocks/s — negligible.

**What dominates the anchor error (V3.4, WARN-7).** The SSR quantum (3.906 ms) is *not* the
dominant term. The anchor is latched at block completion and back-dated by
`FSS_after × 625 µs` (the right compensation for latch latency), but the **burst-tail term and EXTI
latency add a further ±(watermark period) ≈ ±5 ms of jitter** — ~1.3× the quoted quantisation. The
plausibility threshold below (`15 ms + 3σ`) tolerates it, so there is **no functional risk**; the
only change is the expectation: **anchor residuals are ≈ 5–8 ms, not 3.906 ms**, and the validator's
goodness-of-fit threshold must be set accordingly (A.6 #4).

**Firmware plausibility filter (before publishing).** Using the previous accepted anchor:

```
expected_ms = Δcaptured_index * 625 µs (corrected by odr_est)
thr_ms      = 15 + 3 * (expected_ms * odr_unc_ppm / 1e6)
if (|Δt_ms − expected_ms| > thr_ms) -> reject, anchor_reject_cnt++
```

At startup (±10 % ODR) `thr ≈ 142 ms`; once locked, `thr ≈ 15.6 ms`. A **1 s torn read is rejected in
both regimes** — it never reaches the file. Without this, one outlier would shift the part slope by
**1 s / 13 926 s = 71.8 ppm**, ~250× the claimed 0.28 ppm. `Δcaptured_index` is a **C-domain**
difference (§1.3.7), so a ring drop between the two anchors does **not** trip this filter.

**Publication (V3.4, cut §3).** `{rtc_ms, part_captured_index, part_index}` is written by the
producer alone as plain word stores. A thread-context reader copies the 16 B snapshot inside a single
`__disable_irq()` / `__enable_irq()` window (≤ 60 ns at 84 MHz). **The seqlock, its odd/even
sequence and its retry loop are removed**, together with the F18 "second owner" fault test: a second
owner is excluded by construction and caught by a debug assertion (§2.4).

**ODR estimate:** the producer maintains `odr_est_hz` and `odr_unc_ppm` from its own accepted
anchors: `unc = 100 000 ppm` until the anchor span ≥60 s, then `max(500, 3 × residual_ppm)`. These
values feed the drain deficit band (§3.2.1, as the precomputed `odr_q20`/`unc_q16` factors) and are
published in the MAN.

### 7.2 Host time-scale reconstruction and the two invariants (R4 + FLAW-2)

- Anchors: MAN first/latest + rolling 8 (27.2 s cadence, last 217.6 s) + HEADER log (108.8 s cadence,
  whole part). All indices are **part-relative with explicit `part_index`** (X3) and are **C-domain**
  (§1.3.7).
- **Fit segmentation — sensor-side gaps only (V3.4).** A fit segment is broken by `SENSOR_OVR` or by a
  **sensor-side** `lost_or_valid` (samples that were never captured, which is the only thing that can
  change the C-vs-RTC slope). A **ring drop (`OVERFLOW`) does not break a fit segment**: it removes
  425 ms of *stored* data, but the producer kept draining the FIFO at 1600 Hz throughout the card
  stall, so **C remains a faithful proxy for time on both sides of the gap**. V3.3's "cut at any block
  with `OVERFLOW`" threw away good anchors — and good spectrum — for zero accuracy gain.
  `FIFO_FULL` alone never breaks a segment. **FFT/PSD windows do break on `OVERFLOW`** — for
  continuity, not for scale (App. F.1).
- **Never compensate a fit with an uncertain loss estimate.** A sensor-side estimate carries ±32
  samples of uncertainty per event; injecting it into the slope injects exactly the error we are
  trying to remove. Exclude the segment instead.
- **Robust fit:** Theil–Sen or iterative σ-clipping; reject outliers before estimating the slope.
- **Guard ±5000 ppm per segment**, not on the global slope — a global linear fit is itself invalid under
  thermal drift of the sensor's internal oscillator. **V3.5 (WARN-6): this is a *sanity* guard, not an
  accuracy claim, and the two are now separated.** (a) A segment whose fitted slope error exceeds
  5000 ppm is **rejected** — 2.0 Hz at 400 Hz means the segment cannot be used at all. (b) The **achieved
  per-segment residual must be reported** (App. F.4, F.6 §6) before any peak taken from that segment may
  carry F.7's **0.005 Hz** peak-location uncertainty: 5000 ppm admitted silently is 400× the accuracy the
  analysis contract promises, and a report that quotes 0.005 Hz off a segment whose residual it never
  printed is not a measurement, it is a number.
- **Two separate invariants (V3.4 — V3.3's single invariant double-counted ring losses).**

  | # | Domain | Invariant | What breaks it |
  |---|--------|-----------|----------------|
  | **I-T (time base)** | **C** | `Σ Δcaptured ≈ Δt × Fs_fit` over the segment | **sensor-side loss only.** Ring losses must **not** be added: they were captured (`captured` advanced) and only then discarded, so `Δcaptured` already contains them |
  | **I-S (container)** | **S** | `Σ samples_in_blocks = part_captured_samples − samples_lost_ring` | a block lost by a torn write, an undeclared drop, or a counter that lies |
  | **I-X (cross-check)** | both | `Σ lost_or_valid(OVERFLOW blocks) ≈ samples_lost_ring` and `Σ lost_or_valid(SENSOR_OVR blocks) ≈ samples_lost_sensor`, sensor-side within `fifo_ovr_cnt × 32` | mis-attribution between the two loss domains — the failure mode FLAW-1 created by leaving `first_sample_index` ambiguous |

  V3.3's `Σ(Δcaptured + declared_lost) ≈ Δt × Fs_fit` overshoots by exactly `samples_lost_ring` per
  overflow event: a 6 s GC stall (15 dropped blocks = 10 200 samples) breaks a check that A.6 #3
  requires the host to run, and reports **corruption on a perfectly valid file**.

**Why this matters — magnitudes, corrected:**

| Gap inside a 27.2 s anchor window | Slope error if not excluded |
|-----------------------------------|------------------------------|
| 32 samples (20 ms, sensor overrun) | **735 ppm** (the review's 184 ppm was 4× low) — a genuine C-domain discontinuity |
| 680 samples (425 ms, ring drop) | **0 ppm in the C domain.** V3.3's **15 625 ppm figure is withdrawn** (FLAW-2): it only appears if the host uses the **storage** index `S` as the time index, i.e. it is a symptom of FLAW-1, not a physical property of a ring drop |
| 32 samples spread over a 1 h global fit | 5.6 ppm |

**Accuracy floor, restated (V3.4, WARN-7).** One SSR LSB (3.906 ms) over a 3.87 h part = **0.28 ppm**;
with the realistic ≈ 5–8 ms anchor jitter the **per-anchor-pair** figure is **0.36–0.57 ppm**, and a
robust fit over the ~128 anchors of a full part brings part-level slope uncertainty to well under
0.1 ppm. Both statements are true only for clean segments and outlier-free anchors — which is what
§7.1's filter and the segmentation rule above buy.

### 7.3 Validity gate at boot ("year 2000" rock)

1. `RTC_ISR.INITS` / `RSF`.
2. Magic `0xA55A3C3C` in `RTC_BKP0R` — absent means the backup domain lost power.
3. Plausibility window `2026 ≤ year ≤ 2099`.
4. On any failure: fall back to the **manifest's last anchor UTC** (+ elapsed estimate), then to
   Backup SRAM `last_utc` if its CRC passes, else 0; set `clock_valid=0` in the HEADER and
   `CLOCK_INVALID` in every block header; LED 7.

`get_fattime()` returns the RTC value when `clock_valid==1`, otherwise the fixed build timestamp
`2026-11-17 00:00:00`. File names never depend on the clock.

### 7.4 Absolute frequency traceability (V3.4 — WARN-6)

Everything in §7.1–§7.2 is **self-referential**: captured samples versus an LSE-derived RTC. That
makes the *relative* time base excellent and the fit *immune* to RTC drift — but it does not make
the **absolute** `Fs` known. The chain that actually bounds it:

| Link | Bound | Source / status |
|------|-------|-----------------|
| RTC prescaler arithmetic | exact (32768/128/256 = 1.000000 Hz) | audit-verified, §7 |
| LSE crystal frequency-temperature parabola | order **10–30 ppm** around 25 °C, plus aging | crystal datasheet — **not vendored in this repo → `UNCERTAIN`** |
| Sensor ODR tolerance | **unspecified** [DS E17] | measured at run time, this is exactly what `odr_ppm_final` captures |
| Sensor ODR vs. nominal | folded into `odr_ppm_final` | §7.1/§7.2 |

**Consequences and the one required measurement (V3.5, WARN-6 — sized to the error budget).** 20 ppm at
150 Hz is 0.003 Hz — negligible against the 0.1 Hz analysis bin of a 10 s window (0.390625 Hz Welch bins,
App. F.7), so the campaign does not *need* absolute calibration; it **needs it stated**. Therefore:

- **M0 adds one measurement, and it is a counter measurement:** count ODR-derived pulses (TIM2-latched
  watermark edges or a `FSS`-derived pulse train) over a **10 s gate locked to 1PPS or to a 1 Hz counter
  gate** — 10 s resolution ≈ **10 ppm**, comfortably inside the 10–30 ppm LSE term it is meant to bound.
  **Preferred over "shaker + synthesised tone"**: a synthesised tone inherits the shaker/amplifier chain's
  own frequency error and a shaker's amplitude-dependent phase lag, so it can measure the *capture chain*
  but cannot calibrate `Fs`. It remains an acceptable **alternative** when no 1PPS or counter is available.
- **M0 adds a second, free measurement (WARN-10):** histogram the **inter-edge intervals** of the watermark
  EXTI with the same TIM2 1 MHz counter. Nominal 8 samples = **5.000 ms**; 1 µs resolution = 200 ppm per
  edge; averaging ≈ 40 000 edges (3.4 min of capture) resolves **≈ 1 ppm relative cycle-to-cycle
  stability**, which is what §1.3's "`C/Fs` is the sample instant" assumption actually rests on and what
  [DS E17] leaves open. It also separates *ODR jitter* from the *drain-latency* term that §7.1's 5–8 ms
  anchor residual mixes in. Zero extra hardware.
- The correction is stored in the HEADER as **`odr_abs_ppm_ref`** (A.1 @ `0x0B6`) and the campaign report
  quotes both the relative `odr_ppm_final` and the absolute `Fs` with its uncertainty. This converts
  "relative ppm" into "absolute Hz" for the price of one bench afternoon.
- Traceability is a **report line**, not a run-time constraint: no firmware behaviour depends on it. The
  cycle-stability histogram is likewise **not** a container field; it is recorded in the M0 bench record.
  If it ever has to travel with a file, it lands in the HEADER reserved run at `0x0D0` (no geometry
  change).

---

## 8. State Machine & Error Handling

```
        (V3_ENABLE_ROTATION=1 only)
                     ┌───────────────── ROTATING ◄──── part full — unreachable (§4.5, WARN-1)
                     │                     │
                     ▼                     ▼
  IDLE ──KEY long──► PREPARE ──ok──► RECORDING ──KEY long / VDD low(3 medians)──► FLUSHING ──► IDLE
   ▲                   │ fail           │  ▲            / C-index guard A.3 note 2      │ fail
   │                   ▼                ▼  │ L0/L1 (ring keeps filling)             ▼
   └──────────────── ERROR ◄────────────┘  └───────────────────────────────────── ERROR
                       │ KEY / card re-insert / power-cycle
                       └────────────► BOOT → RESCUE → IDLE
```

**V3.5 (WARN-1): the `part full` transition out of `RECORDING` is removed from the diagram and from the
code.** 69.4 MB written of a 134.2 MB part (1.93× margin) makes it unreachable in this schedule, so the
branch and everything behind it (`part_index++`, `part_index > 255`, per-part identity arithmetic) is not
implemented for M0–M2 (§4.5). The one capacity exit that *is* implemented is the **25-bit C-index guard**
(A.3 note 2, `stop_reason = 7`): its trigger — a card dead long enough for the producer to keep discarding
blocks — is reachable. BOOT runs the two sensor decisions of §3.1 (range from the 60 s pre-test; the
mandatory dual-rate alias scan of App. F.3) **before** `PREPARE` creates a file.

The sensor chain runs unchanged in PREPARE / RECORDING / ROTATING / FLUSHING / ERROR.
**It is never stopped by a rotation.** With `V3_ENABLE_ROTATION=0` (M2/M3 default) the `ROTATING`
state does not exist and a full part ends the session by clean-stop instead.

| State | Active | Forbidden / notable | IWDG feed | Exit criterion |
|-------|--------|---------------------|-----------|----------------|
| BOOT | 84 MHz, IWDG start, VREFINT_CAL check, RTC gate, SD init + mount, RESCUE, WHO_AM_I, self-test, FIFO/ODR init, **60 s range pre-test (§3.1/§3.3)**, **mandatory dual-rate alias scan (§App. F.3, WARN-8)**, **quiesce + liveness assertions (§3.2.6/§3.2.7)** | UART only here; `f_mkfs` only on explicit host command; **no directory sweep** (§4.3) | scan/init progress | checks passed or reported by LED |
| IDLE | STOP + EXTI wake, RTC alarm 5 s, VDD poll, LED | sensor powered down (ODR=0000) | 1 Hz tick | KEY held ≥1 s (30 ms debounce) |
| PREPARE | id scan (App. G), `FA_CREATE_NEW`, `f_expand` ≤2 s, HEADER + anchor sector 0, first MAN commit | ring commits to file only after the HEADER is on media | allocation progress | offset = 0x1000, ≤3 s (K10) |
| RECORDING | producer, consumer (write at ≥1 block, **4096 B per call**), MAN every 64 blocks | **no FAT cluster allocation**, no `f_sync` faster than 1/27.2 s, no UART, **no `f_truncate`**, **no ADC in the write loop (WARN-2)**, **no range or ODR change (FLAW-3)** | block committed | KEY long, **VDD < 3.12 V in 3 consecutive 1 s medians**, clean-stop error, **C-index capacity guard (A.3 note 2)**. ~~part full~~ **removed — unreachable (WARN-1)** |
| **ROTATING** | *compiled out by default*; producer keeps running; §4.5 seven-step sequence | producer must **not** stop; no allocation outside step 5; **no truncation** | each completed sub-step | new part active, intent cleared |
| FLUSHING | ring drain (≤6.8 s), PARTIAL block, TRAILER (**declared extent**), sync, final MAN (`clean_stop_flag=1`) | **no `f_truncate`** — the pre-allocated file is never shrunk; producer stops after the last full block is packed; supervisor disabled after that | drain progress | ring empty and trailer committed |
| ERROR | LED codes, KEY + 1 Hz card polling (**software detect only in M0/M1 — WARN-3**), producer keeps filling until overflow → explicit loss (§3.4 I3) | writing before RESCUE completes is forbidden | 1 Hz tick (L3 safe) | KEY / re-insert / restart → BOOT |

**LED codes on PB2** (active-HIGH, pulsed, duty ≤0.5 %): one 20 ms flash every 4 s = recording;
otherwise N short pulses every 4 s:
**2** card missing · **3** write error / clean-stop **or unrecoverable manifest (§4.3 step 2)** ·
**4** ring overflow · **5** sensor/timer supervisor · **6** VDD < 3.22 V (3 medians) ·
**7** clock invalid · **8** previous session ended by IWDG/BOR reset ·
**9** volume ≥80 % or <256 MiB free · **10** session/part id exhausted · **11** self-test out of range.

---
