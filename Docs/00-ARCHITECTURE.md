# 00-ARCHITECTURE.md — constitution & navigation

> **Source:** split verbatim from `Docs/SDlogger.md` Rev 3.5 (container format v4).
> **Body content transferred byte-for-byte; this header and the navigation map are the only
> additions.** Section numbers (§…, App. …) keep their original global numbering.
> **Contains original sections:** header block (1-36), §1 Project Overview, §2.4 Software layering, §9 Resource Analysis
> **Cross-references:** internal refs (§3.4, App. F.1, A.6 #11, D-3, …) point to the original
> numbering; locate them via the map in `Docs/00-ARCHITECTURE.md`.

## Master navigation map — added by the split, not part of the original document

> **Source lines** refers to `Docs/SDlogger.md`, which is left untouched. All internal refs in the six files keep the original global numbering.

| Original section | Source lines | Now in file |
|---|---|---|
| header block: title, metadata, provenance, document rule | 1-36 | 00-ARCHITECTURE.md |
| Rev 3.5 change log (A FLAW-1..6, B WARN-1..11, C blockers B1..B5, D strip table, E decision register D-1..D-5, F carried in unchanged) | 37-113 | 90-DECISIONS-LOG.md |
| Rev 3.4 change log (kept as history, annotated) | 114-180 | 90-DECISIONS-LOG.md |
| Rev 3.3 change log (kept as history) | 181-226 | 90-DECISIONS-LOG.md |
| §1 Project Overview (1.1 Mission, 1.2 KPI, 1.3 Lossless contract incl. Index model T/C/S table + mutual-exclusion row) | 227-318 | 00-ARCHITECTURE.md |
| §2 Hardware Topology & Framework Choice | 319-434 | §2 heading + 2.1, 2.2, 2.3 — 02-FIRMWARE-DESIGN.md; §2.4 — 00-ARCHITECTURE.md |
| §3 Sensor Acquisition, Concurrency & Buffer Management (3.1, 3.2, 3.2.1-3.2.7, 3.3, 3.4, 3.5) | 435-961 | 02-FIRMWARE-DESIGN.md |
| §4 File Format & Data Structure | 962-1208 | §4 heading + 4.1 — 02-FIRMWARE-DESIGN.md; §4.2 — 01-CONTAINER-FORMAT.md; §4.3, 4.4, 4.5 — 02-FIRMWARE-DESIGN.md |
| §5 Reliability & Graceful Recovery (5.1, 5.2, 5.3) | 1209-1285 | 02-FIRMWARE-DESIGN.md |
| §6 Power Management (6.1, 6.2, 6.3) | 1286-1362 | 02-FIRMWARE-DESIGN.md |
| §7 RTC & Time Base (7.1, 7.2, 7.3, 7.4) | 1363-1525 | 02-FIRMWARE-DESIGN.md |
| §8 State Machine & Error Handling | 1526-1571 | 02-FIRMWARE-DESIGN.md |
| §9 Resource Analysis | 1572-1595 | 00-ARCHITECTURE.md |
| §10 Risks, Milestones, Traceability (10.1, 10.2, 10.2.1, 10.3, 10.4) | 1596-1715 | 03-TEST-PLAN.md |
| Appendix A Byte-exact container specification (A.0, A.0.1, A.1-A.6) | 1716-2161 | 01-CONTAINER-FORMAT.md |
| Appendix B Fault-injection matrix (B.1, B.2) | 2162-2206 | 03-TEST-PLAN.md |
| Appendix C Status of DS_lis3dsh.md | 2207-2244 | 90-DECISIONS-LOG.md |
| Appendix D Open points and verification gates | 2245-2295 | 90-DECISIONS-LOG.md |
| Appendix E Revision lineage | 2296-2318 | 90-DECISIONS-LOG.md |
| Appendix F Host analysis rules (F.1-F.7) | 2319-2603 | 04-HOST-ANALYSIS.md |
| Appendix G Capacity, identity and endurance-run rules | 2604-2645 | 01-CONTAINER-FORMAT.md |
| closing line (End of SD_LOGGER_STRATEGY.md Rev 3.5) | 2646-2656 | 90-DECISIONS-LOG.md |

# SD_LOGGER_STRATEGY.md — V3.5 High-Speed Vibration Data Logger

**Project:** standalone, battery-powered 3-axis vibration logger, continuous capture to MicroSD
**Target board:** WeAct Studio STM32F405RGT6 Core Board, LQFP64 (PCB / netlist frozen)
**MCU:** Cortex-M4F @ **84 MHz constant** · 1024 KB flash · 128 KB D1 SRAM (ring + stack + all DMA buffers) · 64 KB CCM (core-private only) · FPU · SDIO · DMA1/DMA2 · IWDG · ADC1 · RTC(LSE) · PVD
**Sensor:** LIS3DSH on dedicated SPI1 — reference in repo: `Docs/DS_lis3dsh.md` **V2.0**, cited as **[DS]**
**Toolchain:** PlatformIO `ststm32` / `framework = stm32cube` / `env:genericSTM32F405RG`
**Rev:** 3.5 (EN) — supersedes 3.4, 3.3, 3.2, 3.1, 3.0, 2.0, 1.0. **Single source of truth for implementation.**
**Container format version: 4 — unchanged by Rev 3.5** (no field moves, no header grows, `4096 = 680 × 6 + 16` is untouched; the FLAW-2 fix is a producer rule, not a byte).
**Primary use case:** 1–2 h measurement campaigns on a ~3 m beam (modal / frequency-domain work).
The architecture is dimensioned for 8 h+; the *test schedule* is right-sized for the campaign (§10.2).
**Source of this revision (Rev 3.5):** `Docs/SDlogger_audit_V3_4.md` — the audit of Rev 3.4 / container
v4: **FLAW-1…6**, **WARN-1…11**, **critical blockers B1…B5** and the **M0/M1 strip table**. That audit is
in the repo and every correction below cites it.
**Source of the previous revision (Rev 3.4):** `Docs/Audit_SDlogger_V3.3.md` — FLAW-1…5, WARN-1…14 and
the §3 over-engineering strip table. **This file has been restored to `Docs/`** (audit V3.4 WARN-11
provenance complaint is closed): the 26 Rev 3.4 corrections and the "cut §3" justification are
auditable there, and the V3.3 strip table is **not** restated inline in this document. Rev 3.4's
"Verified correct — do not touch" list is still reflected here unchanged (DMA2 stream/channel legality,
CCM/DMA rule, memory budget, clock tree, RTC prescaler chain, sensor bring-up order, FIFO drain logic,
ring arithmetic, metadata overhead, container geometry) and is re-confirmed, with one exception:
the DMA2 *arbitration order* (not legality) is reopened by blocker **B1**.

> **Document rule (since 3.2):** a revision may never delete a section. Corrections are made in
> place; the change log lives here and in Appendix E. V3.1 was a valid *patch* but an incomplete
> *specification*; V3.2 restored the full operational contract. V3.3 keeps all of it and closes the
> DSP-oriented review of V3.2. V3.4 keeps all of it, closes the structural-resonance-campaign audit
> of V3.3, and **re-scopes** (does not delete) the parts of the operational contract that are dead
> code for a 1–2 h single-file run. V3.5 keeps all of it, closes `SDlogger_audit_V3_4.md`, and
> **corrects in place** one register decode (±6 g), one producer-semantics hole (`OVERFLOW` ∧
> `SENSOR_OVR`), one band taxonomy, one unmeasured coherence claim and one missing liveness
> watchdog, while **stripping** (not deleting) the M0/M1/M2 coding listed in §10.2 and in the Rev 3.5
> strip table below. Where a new normative rule had to be added, it was added as a new sub-item or a
> new table row — no existing sibling was renumbered, and no `§N.M` was removed.

---

## 1. Project Overview

### 1.1 Mission

Continuously measure 3-axis acceleration at **1600 Hz**, persist it to a MicroSD card on the
**SDIO peripheral (4-bit)** in a self-describing binary container, on a battery, for hours,
without silently dropping a single sample, without corrupting the file when power disappears at an
arbitrary nanosecond, and **without fabricating gaps that did not physically occur** — a declared
gap removes good data from analysis just as effectively as a real one. Equally: a **declared** gap
must always be *locatable* in wall-clock time, which is what the index model of §1.3.7 exists to
guarantee.

### 1.2 KPI / engineering targets

| # | Parameter | Target | How it is guaranteed |
|---|-----------|--------|----------------------|
| K1 | Sample rate | 1600 Hz, sensor-clock domain | LIS3DSH ODR=1600 Hz [DS §7.20]; instants generated by sensor hardware. **Absolute** accuracy bounded by LSE parabola + [DS E17] — traceability chain in §7.4, M0 cross-check |
| K2 | Sample-instant uniformity | **anchor residual ≤ 8 ms (median), ≤ 15 ms + 3σ outlier gate** | FIFO/watermark capture: ISR latency shifts *readout*, never the instant. "No polling jitter" is not a measurable KPI (WARN-12); the residual vs the fitted time scale is. The physical floor is drain latency, **not** the 3.906 ms SSR quantum (§7.1, WARN-7) |
| K3 | Data rate | payload 9600 B/s (34.56 MB/h); container 9638 B/s (34.70 MB/h) | 6 B/sample + 16 B per 4080 B payload (0.392 % overhead) |
| K4 | Loss guarantee | zero **silent** loss; **no fabricated loss**; sensor-side counts carry bounded uncertainty | drain-first + proven-deficit test (§3.2); `lost_or_valid` per block; `fifo_ovr_cnt` / `fifo_full_cnt` / **`producer_liveness_miss_cnt`** / `spike_cnt` published separately (FLAW-6, WARN-9) |
| K5 | SD GC tolerance | ring 6.80 s (15 usable slots = **6.375 s**) vs 50–200 ms GC stalls | 16 × 4096 B ring in D1, §3.4. **This is total absorption capacity, not recovery granularity — see K6/WARN-14** |
| K6 | Recovery granularity | typical ≤ 1 block + 1 partial (**≤ 850 ms**); worst case ≤ 4 blocks + 1 partial (**≤ 2.125 s**) | write-at-≥1 rule + forward CRC scan in `RESCUE`; excludes card-internal volatile cache. **WARN-14:** the ≤ 1.7 s figure is the *in-flight* set (≤ 4 blocks), a different quantity from K5's 6.375 s total buffer — conflating them causes wrong M1 acceptance decisions |
| K7 | IWDG | 8.0 s nominal (5.45–15.06 s over LSI spread), refresh on proven progress only | §5.1 |
| K8 | Runtime load | **4–8 % worst case @ 84 MHz** (mode B ≈ 2.2 % measured-typical, 3.9–5.5 % if the CS-high floor lands at 10–20 µs; mode A ≈ 0.7 %) | §9, DWT profiling in M0/M3. The 4–8 % band is the **design allowance** until the [DS F8] CS-high floor is measured (WARN-5) |
| K9 | Battery autonomy (product) | ≥ 8 h on 1000 mAh 1S (≈ 12 h typical) — **not qualified by the V3.4 schedule** | constant 84 MHz, STOP idle, §6. The 12 h run that qualified it is cut (cut §3); verification moves post-campaign with M4 |
| K10 | Start latency (KEY → recording) | ≤ 3 s | pre-allocation in `PREPARE`; no blocking ODR gate |
| K11 | Host compatibility | FAT32 / 32 KiB clusters / LE / IEEE CRC-32 (`zlib`) / CRC-8-ATM | Appendix A byte-exact + reference Python validator |
| **K12** | **Campaign acceptance** | **no *undeclared* loss in any analysis window; ≤ 5 % of candidate analysis windows rejected, every rejection attributed to a named counter; trusted band ≤ 400 Hz; mandatory dual-rate alias scan completed (`alias_scan_done = 1`)** | §10.2 M3, Appendix F.1/F.3/F.6/F.7. **FLAW-5:** V3.3's "zero declared loss over all analysis windows" forbade the very compromise the brief allows; the integrity boundary is *undeclared* loss, not *declared* loss. **V3.5 (FLAW-3/FLAW-5):** "≤ 5 %" is now a **clipping duty cycle**, and a peak is admissible only if it survives the second sample rate |

> **Range note (V3.5, decision D-1).** The campaign default full scale is **±6 g** (`CTRL_REG5 = 0x50`,
> 0.18 mg/LSB, clip ±5.90 g). **Self-test acceptance (K-table LED 11, §3.3) is range-independent** —
> the 140 mg / 590 mg typicals are electro-mechanical, not code-domain ([DS F9]) — so the ±4 g → ±6 g
> move **changes nothing there and must not be "fixed" together with it**. No KPI in this table depends
> on the range; the only range-dependent figures are `ng_per_lsb`, `fs_g`, the clip point and the
> LSB-referred noise floor, which is **[BENCH]**-pending (§3.3, M0).

### 1.3 Lossless contract (definition of done for reliability)

1. Every sample produced by the sensor either lands in a written block or is counted in
   `samples_lost_*`. The producer never stalls and never stops silently. **On ring overflow the
   producer aborts its *in-progress, unpublished* block and discards its ≤ 680 samples; a published
   block is never un-published and `head` never moves backwards (§3.4 I3, WARN-9).** Loss is stated
   as `samples_lost_ring += 680` per blocked publish cycle. Because those samples *were* captured,
   they remain in the C domain and the S↔C jump they create is what makes the gap locatable
   (§1.3.7).
2. **Duplicates are structurally impossible.** The driver never reads more samples than the FIFO
   can contain: `FSS` is qualified by `EMPTY` and `OVRN_FIFO`, and fullness is additionally
   **credibility-checked against elapsed time** (§3.2, X4) so an ambiguous or latched status flag
   can never cause an over-read.
3. **No fabricated gaps.** A FIFO fullness event is *not* a loss event. Loss is declared only when
   elapsed-time accounting proves a deficit beyond the ODR uncertainty band. Fullness without
   proven loss is recorded as an advisory (`FIFO_FULL`, `fifo_full_cnt`) and does **not** create a
   discontinuity in the data or in the host's time axis.
4. Any power loss leaves a file readable block-by-block up to the last valid CRC block on media.
   The lost tail is bounded by K6. **Card-internal volatile write cache is outside this bound** —
   industrial/pSLC cards are a requirement, not a preference (§4.1).
5. No error-handling path may block in interrupt context, and nothing preempts the SDIO/DMA
   interrupt except a flag-setting PVD (§3.2).
6. `first_sample_index` carries **C**, the **captured** index (§1.3.7). Loss estimates never enter
   the index arithmetic of the *producer*; the two declared-loss domains move C and S **differently**
   (§1.3.7), which is why the index alone is sufficient to locate a ring gap and why the time axis
   is always reconstructed from RTC anchors (§7) over **fit segments bounded by sensor-side gaps
   only**, never by interpolation across a gap.
7. **Index model (normative — FLAW-1).** Three distinct index domains exist and are never mixed:

   | Domain | Name | Meaning | Stored? |
   |--------|------|---------|---------|
   | **T** | true ODR index | samples *produced* by the sensor (`T = C + samples_lost_sensor`) | not stored |
   | **C** | captured index | samples *captured* since part start = `first_sample_index` | **stored**, time-valid |
   | **S** | storage index | `block_seq × 680` = position in the file's data region | never stored (derived from `block_seq`) |

   With `C_k`/`S_k` the first-sample/storage indices of block *k* and `L_k = lost_or_valid` of
   block *k*:

   | Invariant | Statement |
   |-----------|-----------|
   | Container | `S_{k+1} = S_k + 680` (equivalently `block_seq` strictly +1); `S` is a file offset, **not** a time quantity |
   | Ring-domain gap (`OVERFLOW`) | samples were captured and then discarded **after** capture: `C_{k+1} = C_k + 680 + L_{k+1}` ⇒ **C jumps by more than 680** |
   | Sensor-domain gap (`SENSOR_OVR`) | samples were never captured, so `C` does **not** contain them: `C_{k+1} = C_k + 680` and `L_{k+1}` is the declared sensor-side deficit (bounded, `fifo_ovr_cnt`-attributable) |
   | Clean block | `L_{k+1} = 0`, no gap flag ⇒ `C_{k+1} = C_k + 680`; in general `C_k = S_k + Σ L_ring(< k)`, so `C_k = S_k` **only** while no ring gap has been declared in this part |
   | Host reconstruction (normative) | `C_block = S_block + Σ L_ring(≤ block)`, i.e. **sum `lost_or_valid` of OVERFLOW-flagged blocks only**; the anchor fit (§7.2) then converts C → *t* |
   | Gap placement accuracy | a declared gap is placed to **± (425 ms block granularity ⊕ ≈ 5–8 ms anchor error)** |
   | **Mutual exclusion (V3.5, FLAW-2)** | `OVERFLOW` and `SENSOR_OVR` are **never both set in one block** (§3.4 I6). The host-reconstruction rule above is valid **precisely because** the two loss domains cannot co-occur inside one `lost_or_valid`: a co-asserted block would inject never-captured samples into **C** and is a **file defect**, not a gap |

   **Why the 3.3 equality could not stand:** `block_seq` advances only for *written* blocks, so
   `first_sample_index == block_seq × 680` is a tautology in the S domain (it validates nothing and
   leaves the host unable to locate a gap) and is **false** in the C domain, where it made `RESCUE`
   truncate every good block after the first legitimate GC stall. Both readings are now replaced by
   the invariants above.

---
### 2.4 Software layering and single-owner rules

| Layer | Contents | Rule |
|-------|----------|------|
| `bsp_*` | clocks, pins, TIM2, IWDG, PVD, ADC, RTC, LED/KEY, STOP wake restore | HAL/LL only, no policy |
| `drv_lis3dsh` | register setup, FIFO drain, modes A/B, self-test, resync, error recovery, **PIO/DMA quiesce (§3.2.6)**, **producer-liveness poll entry (§3.2.7)** | knows nothing about files or SD; **sole owner of SPI1 and of RTC reads while the producer runs** |
| `drv_sdio_sd` | `HAL_SD` wrapper, own busy-wait, **software card detect only in M1/M2 (PA8 diagnostic only if M0 confirms — WARN-3)**, recovery ladder | knows nothing about FAT |
| `ff` / `diskio` | FatFs R0.12c + D1 range/alignment guard + bounce | called from the consumer only |
| `logcore` | SPSC ring, packer, container, manifest, anchors, trailer, CRC | **pure C, no peripherals → compiles and is fuzz-tested on the host**; **V3.4 also owns the RESCUE path-solutions that left firmware (directory sweep, both-slots-invalid fallback, rotation-intent resolution)** |
| `app` | state machine, KEY/LED, VDD policy, error codes | only owner of state transitions |

**Peripheral ownership table (X1/X2 — enforced by assertion in debug builds):**

| Peripheral | Owner while recording | Everyone else |
|------------|----------------------|---------------|
| SPI1 | producer chain (EXTI + DMA TC ISR) | config / self-test / resync writes only in BOOT-IDLE with the producer stopped, **or in-chain through the normative PIO/DMA quiesce sequence of §3.2.6 (V3.5, B2)** — a polled SPI access while a DMA stream is armed returns the last DMA-received byte |
| RTC | producer (block-completion latch) | read the **published anchor snapshot** + TIM2 elapsed; direct reads only when the producer is stopped |
| ADC1 | consumer (**1 s median batch only in M1/M2** — the fast droop burst is out of this cycle, V3.5 WARN-2) | TIM2 only sets a 1 Hz flag; PVD ISR touches no peripheral |
| SDIO / DMA2 S3 | consumer | producer never touches SD |
| TIM2 counter | free-running, read by anyone | configuration only in BOOT |

**Single-owner rule is normative; the seqlock is not (V3.4, cut §3).** V3.3 published the anchor
through a seqlock with odd/even sequence and a retry loop, and defined F18 ("a second owner attempts
access") as a fault-injection test. Since a second owner is **excluded by construction** and asserted
against in debug builds, the seqlock and F18 are removed: the producer writes the published snapshot
`{rtc_ms, part_captured_index, part_index}` with plain word stores, and a reader (always thread
context, P7+) takes the 16 B snapshot inside **one `__disable_irq()` / `__enable_irq()` window
(≤ 60 ns at 84 MHz)**. That is strictly cheaper than a retry loop and cannot livelock. A second
writer is a **debug assertion failure**, not a tested runtime path. X1 and X2 themselves are **kept
unchanged** (audit: correct and cheap).

---

## 9. Resource Analysis

| Resource | Allocated | Headroom |
|----------|-----------|----------|
| **D1 SRAM 128 KB** | ring 65536 + SPI staging 2×256 + `FATFS.win` 512 + **2 × `FIL.buf` 1024** + `man_buf` 512 + `anchor_buf` **512** (31-anchor sector buffer, §4.3) + diskio scratch 4096 + MSP stack 2048 + `.data/.bss` ~8192 = **~81 KB** | **~47 KB free** (audit-verified ~54 KB by its own tally; either way ≥ 40 KB headroom, no trap) |
| **CCM 64 KB** | CRC-32 slicing-by-8 table 8 KB (non-const, generated at boot) + snapshot/calibration/reset journal ~2 KB = **~10 KB** | 54 KB free; DMA-visible objects forbidden |
| **Flash 1024 KB** | vectors + HAL + FatFs + app ≈ 120–180 KB | rest for bootloader / calibration |
| **DMA2** | **preferred (V3.5, B1):** SPI1_RX = **S0/CH3**, SPI1_TX = **S3/CH3**, SDIO = **S6/CH4**; SDIO `PFCTRL=1`, `INC4` bursts. Legacy (V3.0–V3.4, still legal): `TX = S5/CH3`, `SDIO = S3/CH4` | RM0090 Table 43 / the repo DMA2 reference §3: `SPI1_RX ∈ {S0,S2}`, `SPI1_TX ∈ {S3,S5}`, `SDIO ∈ {S3,S6}` — **both assignments are conflict-free and legal; they differ only in AHB arbitration**, where the lower stream number wins. Legacy order is `S0 > S3(SDIO) > S5(SPI1_TX)`: the card outranks the stream that clocks the sensor read, and an SPI master under-fed by its TX DMA re-transmits the previous byte → **duplicated samples that satisfy every CRC** (mechanism `UNCERTAIN` pending the RM0090 SPI under-run text). Preferred order is `S0 > S3(SPI1_TX) > S6(SDIO)`. **Never** `SPI1_TX` at S5/**CH4** (= SDIO), never Stream 3 shared between SPI1_TX and SDIO. M0 confirms on silicon **and under concurrent 4 KiB SDIO load** |
| **NVIC** | PVD 4 → SDIO/DMA2_S3 5 → SPI1/DMA2_S0,S5/INT1 6 → TIM2 7 → KEY 7 → SysTick 15 | nothing preempts SDIO except a flag-setting PVD |
| **CRC-32** | IEEE 802.3 reflected (`0xEDB88320`, init/xorout `0xFFFFFFFF`), slicing-by-8, table in CCM | ≈80 µs per 4096 B block → **0.019 %**; matches `zlib.crc32()` |
| **CPU @84 MHz** | producer **2.2 % measured-typical, 3.9–5.5 % if the CS-high floor lands at 10–20 µs** (mode B, default) / 0.7 % (mode A); consumer CRC 0.02 %, `f_write` ≈ 60 µs CPU per block, **producer-liveness poll ≈ 0.05 % (§3.2.7)**, slew/spike gate ≈ 0.1 % (one subtraction per triple, §3.4), **VREFINT 1 s median ≈ 0.045 %**, MAN/anchors negligible. **The VREFINT fast burst (~0.24 %) is gone (V3.5 WARN-2)** — that is the budget it returns | **design allowance 4–8 % worst case** (V3.4, WARN-5; V3.3's "<4 % typical / <6 % peak" was built on a bus-time-only estimate and an unmeasured CS floor). SD bus **0.08 %** (42.7 µs/sector × 8 / 425 ms), SPI bus 1.8 % (B) / 1.5 % (A) |

Stack: one MSP serves thread and handler modes; 2 KB confirmed by paint-pattern watermark
(`-fstack-usage` + runtime check) in M0/M3.

**SDIO request-size bound (WARN-4).** With the one-block write policy, the largest transfer any
caller can hand to `HAL_SD_WriteBlocks_DMA` from the logger is **4096 B = 8 sectors**, which fits
the 13-bit `SDMMC_DTCTL` counter with a 2× margin and removes the reliance on unverified HAL
chunking. FatFs' own window traffic is ≤ `_MAX_SS = 512` B per sector write, i.e. also far inside
the limit. The only remaining multi-sector path is the `diskio` bounce chunking loop (§3.5), capped
at the same 4096 B.

---
