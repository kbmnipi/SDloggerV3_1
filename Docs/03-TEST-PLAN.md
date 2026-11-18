# 03-TEST-PLAN.md — milestones, risks, faults

> **Source:** split verbatim from `Docs/SDlogger.md` Rev 3.5 (container format v4).
> **Body content transferred byte-for-byte; this header and the navigation map are the only
> additions.** Section numbers (§…, App. …) keep their original global numbering.
> **Contains original sections:** §10.1, §10.2, §10.2.1, §10.3, §10.4, Appendix B
> **Cross-references:** internal refs (§3.4, App. F.1, A.6 #11, D-3, …) point to the original
> numbering; locate them via the map in `Docs/00-ARCHITECTURE.md`.


## 10. Risks, Milestones, Traceability

### 10.1 Risk register

| Risk | Likelihood / impact | Mitigation |
|------|---------------------|------------|
| GC stall longer than the ring (>6.4 s) | medium / data gap | pSLC, ≤80 % fill, 4 MiB alignment, 32 KiB clusters, 16-chunk ring, explicit loss counters, `stall_max_ms` audited |
| INT1 not routed or shared | medium / latency | §2.1 gate; TIM2 200 Hz polling fallback with identical accounting |
| `0xA8` burst does not wrap `0x2D→0x28` | medium / garbage data | **mode B is the default and does not depend on it**; mode A gated on analyzer proof |
| IEA/IEL polarity differs from the converted DS | medium / missed or inverted edges | scope gate in M0; EXTI edge is a compile-time constant |
| `FSS` 5-bit vs 32-deep FIFO ambiguity | medium / stall or over-read | drain-first + **elapsed-time credibility check** (X4) + drain-loop escalation |
| **`OVRN_FIFO` latched-stale semantics** | medium / fabricated loss or duplicates | credibility check; stale flag → advisory + deferred resync, never an over-read |
| **SPI/DMA abort mid-frame** | medium / undefined FIFO state | mandatory resync + declared loss (§3.2.2); mode B bounds it to 1 sample |
| **Torn RTC anchor** | medium / 71.8 ppm time-scale error | double-read + firmware plausibility filter + single RTC owner |
| **Dual-owner RTC/ADC access** | medium / torn protocol | ownership table (§2.4) + debug assertions (F18 → assertion, cut §3) |
| DMA-visible buffer migrates to CCM | medium / silent corruption | D1 link section + linker ASSERT + CI map grep + diskio range check + M2 soak |
| Card removed / **socket micro-motion under vibration** | medium / hang or CRC storm | **PA8 card-detect as L3-vs-L0/L1 discriminator if wired (§2.1, WARN-11)**; else software detect → L3; **mechanical retention requested**; `sd_err_cnt`/`reinit_cnt` audited per run; **F19 shake test promoted to M2** |
| Both MAN slots corrupted by FTL | low / unrecoverable metadata | **V3.4: recovery moved to the host validator**; firmware reports `MAN_INVALID`/LED 3 and never sweeps the card. Residual risk accepted for a 1–2 h campaign (audit §3) |
| Rotation cut leaves an orphan part | low / stale 128 MiB file | **rotation compile-gated off for M2/M3**; when enabled: two-phase intent + crash-point table (§4.5, host-side resolution) |
| No VBAT coin cell | high / clock reset | ids from manifest/directory, `FA_CREATE_NEW`, `clock_valid` flag, RTC fallback chain |
| Card write cache loses ACKed data | medium / K6 violated | industrial/pSLC requirement; K6 excludes it explicitly; M2 power-cut measurements on the real card |
| ~~Near-Nyquist aliasing (unknown AA order)~~ → **AA knee at Nyquist** | ~~closed in V3.4~~ **reopened in V3.5 for `f_true > 800 Hz`** (FLAW-5) | `fc = fs/4` (BW 400 Hz) still moves the knee to a quarter of the sample rate and App. F.2's regions (i)/(ii) still hold; what V3.4 got wrong is region (iii): an image of `f_true > 800 Hz` lands **inside** 0…800 Hz, possibly inside the trusted band, at −15…−32 dB depending on an **unknown** filter order. Mitigations now: **mandatory dual-rate alias scan before every campaign** (App. F.3, WARN-8) + the **linear-phase decimation stage** (App. F.7, WARN-7) which replaces the unmeasured analog band edge with a known digital one. Filter order remains unspecified → App. D open item |
| **±6 g chosen for headroom costs LSB headroom** | low-medium / noise (**[BENCH]-dependent**) | 3.00 mg rms AA-band noise = **16.7 LSB rms** at ±6 g if the [DS] noise density is range-independent, ≈ 50 LSB if it scales with FS (or ≈ 4× at ±2 g in that branch). **[DS quotes 150 µg/√Hz only at FS = 00 → the penalty is unmeasured, not settled (V3.5 FLAW-3, M0 measures idle AA-band rms at ±4/±6/±8 g).** Accepted regardless of the branch: the alternative — a ±4 g clip point (±3.932 g) against a 1–5 g resonance — rejects every window containing the mode, i.e. loses the measurement itself. Amplitude accuracy is out of scope; peak **location** is not. `0x40` (±2 g) remains available only if a pre-test *proves* peaks < 1.9 g; `0x58` (±8 g) if peaks exceed 5.9 g |
| **SDMMC 13-bit DMA byte counter exceeded** | medium / silent short transfer or hang | **`f_write` capped at 4096 B (§3.4)**; M1 exit proves it by inspecting `SDMMC_DTCOUNT`/`DMA_LISR` |
| **Producer/consumer race on ring overflow** | medium / corrupted block on media | **I3 rewritten: an unpublished block is aborted, a published one is never un-published, `head` never moves backwards (§3.4)** |
| **`first_sample_index` domain ambiguity** | ~~high~~ **closed in V3.4** | §1.3.7 T/C/S model; A.6 validator invariants; RESCUE accepts declared C-jumps |
| **Single VDD median ends the campaign early** | medium / lost data (product) | **3 consecutive 1 s medians** before FLUSHING (§6.2); PVD 2.9 V as last line |
| **`f_truncate` unavailable / destructive** | ~~medium~~ **closed in V3.4** | declared extent in MAN/TRAILER instead of truncation (§4.3, §4.5, §5.3) |
| **Anchor sector RMW drags erase groups** | ~~medium~~ **closed in V3.4** | append-once anchor log, 3 sectors (§4.3) |
| **Mode-B CPU exceeds budget because of an unmeasured CS floor** | medium / K8 miss | **4–8 % allowance**, M0 CS-high sweep, fallback SPI BR=32 → 2.625 MHz (§3.2.4) |
| **Analysis recipe undefined → ppm work is moot** | high / unusable result | App. F.7 "Spectral analysis recipe" is now normative: 10 s windows, Hann, 50 % overlap, Welch over 4096-sample frames (0.390625 Hz bins), peak location to ≤ 0.02 Hz |
| **Missed watermark edge / interrupt that never re-arms** (V3.5 **FLAW-6**) | medium-high / **large undeclared** loss — the only failure mode in this design that can hollow a 2 h run invisibly | **Producer-liveness poll §3.2.7** (10 ms flag, `FSS ≥ WTMP ∧ now − t_last_end_us > 12 ms` → same drain entry; `producer_liveness_miss_cnt`; forced resync after 3 misses) **+ 1 kΩ/1 nF RC on INT1** (§2.1, §6.3). PB5 is flying-lead, next to tunnel motors/VFD drives and a 24 MHz 4-bit bus, with `IEA` polarity unresolved |
| **SPI1 TX stream starved by the SDIO stream → re-transmitted byte → duplicated samples that pass every CRC** (V3.5 **B1**) | medium / silent sample duplication | Preferred DMA2 set `S0/S3/S6` (§2.1, §9) so arbitration favours capture; if the legacy set is kept, the **M0 concurrency test** (mode-B stream + continuous 4 KiB SDIO writes vs a deterministic stimulus) is mandatory. Mechanism `UNCERTAIN` until RM0090's SPI under-run wording is read |
| **Polled SPI access while its DMA streams are armed → stale byte → wrong `FSS`/`OVRN_FIFO` → duplicates or fabricated loss** (V3.5 **B2**) | medium / wrong drain decision | Normative **quiesce sequence §3.2.6** + M0 assertion (`WHO_AM_I` reads `0x3F` through a quiesced bus) |
| **Settling discard counted as loss → `samples_lost_sensor += 160` on block 0** (V3.5 **B4**) | medium / fabricated gap (K4 violation) | Chain state **re-based at the end of the discard window**, **deficit test disabled during it**, re-applied after every self-test / ODR change / resync (§3.1, §3.2.1, §3.2.2) |
| **`OVERFLOW` ∧ `SENSOR_OVR` in one block → never-captured samples injected into C; validator sums unsatisfiable** (V3.5 **FLAW-2**) | medium / permanent C-domain error for the rest of the part (≈ 735 ppm for 4 such blocks in one anchor segment) | Producer mutual-exclusion invariant **I6** + carry-forward; A.3 row 5; A.6 rejects the block as **file defect** |
| **Inter-axis skew invisible to every CRC and counter** (V3.5 **FLAW-4**) | medium / mode shapes, coherence, OMA garbage while PSDs look clean | Normative **< 5° coherence** requirement (App. F.7) + **M0 cross-spectral phase gate**; ≥ 100 ms discard reused as re-alignment window; mode B only until [DS F4] closes |
| **Undetected SPI link spike creates spectral content no counter can see** (V3.5 **WARN-9**) | medium / phantom structure | Consumer **slew/spike gate** (§3.4), log-only in M1/M2, `spike_cnt` in MAN/TRAILER, per-block table host-side |
| **Aliasing image of `f_true > 800 Hz` inside the trusted band** (V3.5 **FLAW-5 / WARN-8**) | medium / a peak that looks 400-Hz-clean | Mandatory dual-rate scan (App. F.3), `alias_scan_done` in HEADER, F.6 §7 tests **every** candidate peak, digital decimation (App. F.7) |
| **No linker enforcement: a DMA-visible buffer migrates to CCM in practice, not just in theory** (V3.5 **B3**) | medium / silent corruption | §3.5 (i)-(iii) **as a §10.2.1 repo precondition before M1** — today only the runtime `diskio` guard exists |
| **M1/M2 exit criteria depend on source that is not vendored** (V3.5 **B5 / WARN-11**) | high / a milestone that cannot be signed off | §10.2.1: vendor `ff.c` + HAL, pin `platform = ststm32@x.y.z`, add `env:release` (-O2) **before M1** |

### 10.2 Milestones (right-sized for a 1–2 h campaign, R8 + V3.4 re-scope)

| Milestone | Scope | Exit criterion |
|-----------|-------|----------------|
| **M0** | BSP 84 MHz (PLLQ=48), PB2/PC13, IWDG 8 s, VREFINT (**slow median only** — WARN-2), RTC gate + **double-read + anchor filter**, WHO_AM_I, self-test, **mode B implemented and verified**, optional mode A after analyzer proof, **DMA2 mapping confirm + loopback (TEIF/FEIF/DMEIF)**, **SPI1↔SDIO DMA concurrency test (B1)**, **SPI1 PIO/DMA quiesce assertion (B2)**, **producer-liveness poll exercised with INT1 deliberately disconnected (FLAW-6)**, **discard-window / deficit-test re-basing check (B4)**, **CS-high floor sweep**, **PA8 wiring + active level measured as a fact only (WARN-3)**, **absolute ODR cross-check against 1PPS / 1 Hz counter gate (WARN-6)**, **inter-edge-interval histogram (WARN-10)**, **idle AA-band rms at ±4 / ±6 / ±8 g (FLAW-3)**, **inter-axis cross-spectral phase gate (FLAW-4)**, **dual-rate alias scan run (WARN-8)**, stack watermark | LED codes readable; IWDG reset detected and mirrored; IDLE ≤ 150 µA; **mode-B monotonicity, no duplicates, `ΔFSS ≈ N ± 1`, and byte-count equality against the FSS-estimated volume**; **no duplicated triple while a continuous 4 KiB SDIO write stream runs alongside capture** (B1); **a quiesced polled `WHO_AM_I` reads `0x3F` with the DMA streams configured** (B2); **a deliberately starved watermark edge is caught by the liveness poll within 12 ms and appears in `producer_liveness_miss_cnt`** (FLAW-6); **block 0 carries `samples_lost_sensor == 0` and no `SENSOR_OVR` after the settling discard** (B4); **X–Y and X–Z cross-spectral phase \|Δφ\| < 5° over 5–400 Hz** (FLAW-4, or a stated constant offset); **idle AA-band rms recorded at each candidate range** (FLAW-3); **inter-edge interval histogram: mean 5.000 ms, spread resolved to ≈ 1 ppm relative** (WARN-10); `odr_abs_ppm_ref` written to the HEADER from the **1PPS/counter** measurement; **A/B stream byte-identity is an M3 gate, not M0** (WARN-12); mode + measured CS floor recorded; mode-B CPU within the 4–8 % allowance or BR=32 fallback taken |
| **M1** | SDIO 4-bit + DMA2 (SDIO at **S6/CH4** preferred / S3/CH4 legacy) latency profile: 10 000 × **4096 B** writes on an 80 %-full card with CMD13 polling, **capture running concurrently throughout** (B1) | **Precondition: §10.2.1 (vendored `ff.c` + HAL + pinned `ststm32`) — without it the 13-bit-counter proof has no subject (B5).** p99 < 5 ms, max < 400 ms, zero CRC errors, no re-init; **`SDMMC_DTCOUNT`/`DMA_LISR` inspection proves no request exceeds the 13-bit counter (or a chunking proof of the vendored HAL)**; **one deliberate forced stall driving the ring to overflow**, so the drop path, `samples_lost_ring`, `ring_ovf_cnt` and the C-jump are exercised at M1, not at M3; **captured triples remain unique and non-duplicated under the continuous 4 KiB write load** (B1); `spike_cnt` recorded (log-only) with a spike report attached (§3.4) |
| **M2** | Container + anchors (**append-once**) + dual MAN + **RESCUE with declared extent and C-jump acceptance** + full-volume write/read-back CRC + **30–40 power cuts at 4 real phases (mid-data-write, mid-MAN, mid-anchor-append, mid-header-finalise)** + **F19 slot-shake (M2, WARN-3: judged on `sd_err_cnt`/`reinit_cnt`/`ring_ovf_cnt` growth, not on a CD state)** — **the 2 rotation cuts are dropped (WARN-1: a gate-off build cannot produce the crash)** | every file readable; loss within K6 and **every declared gap located by the validator to ±(425 ms ⊕ anchor error)**; `chkdsk` clean; Python validator 100 % including the C-jump/declared-gap acceptance rules, both invariants I-T/I-S **and the co-asserted-flag rejection (FLAW-2: a block with `OVERFLOW` ∧ `SENSOR_OVR` must be reported as corruption)**; no `f_truncate` anywhere; `max_abs_raw` round-trips; **counter re-derivation across MAN / anchor log / HEADER-ext / TRAILER is a `logcore` regression suite, not an M2 cut-phase (WARN-4)** |
| **M3** | **4 h instrumented** end-to-end run (K12 = ≥ 2× a 2 h plan) + fault injection **F6, F15, F16, F20** (+F19 from M2) + **App. F.7 analysis recipe applied end-to-end** (including the decimation stage, WARN-7) + DWT profiling + optional mode-A A/B byte-identity | zero **undeclared** loss; **zero fabricated gaps**; injected faults always flagged; **`producer_liveness_miss_cnt == 0` in a clean run and every non-zero value attributed** (FLAW-6); CPU measured inside the 4–8 % allowance; analysis windows rejected ≤ 5 % and each rejection attributed to a counter; **the dual-rate alias scan was completed before the campaign and every candidate peak — not only peaks above 400 Hz — is cross-matched against the second rate (WARN-8 / FLAW-5)**; fitted `odr_ppm` reported with **the per-segment residual** and both relative and absolute (WARN-6) values |
| **M4** | **post-campaign backlog:** brownout thresholds (3.22 / 3.12 V with the 3-median rule), one IDLE-current measurement, K9 autonomy discharge | planned stop at 3.12 V reproducible before PVD after debouncing; **K9 ≥ 8 h qualified by a dedicated ≥ 8 h discharge — explicitly not by this campaign**; K12 remains ≥ 2× planned duration |

> **V3.4 schedule cuts (audit §3).** The overnight 12 h unattended run, the K1 8 h and K4 24 h soaks,
> the 200-point power-cut fuzz and the 21 targeted rotation cuts are **out**. They are replaced by a
> 4 h instrumented run, 30–40 cuts at the four phases where a cut can actually corrupt something,
> and 2 rotation cuts behind a compile gate. Every failure mode reachable in a 2 h tunnel run is
> still exercised; ~10 h of wall-clock test time and 160 fuzz points are not.

### 10.2.1 Repo preconditions (V3.5 — WARN-11, B3, B5): gating, not a milestone

These are **build/repository facts that must exist before M1 starts**, because M1's and M2's exit
criteria are written against them. They are not firmware work and they are not optional.

| # | Precondition | Why it gates a milestone | Evidence required |
|---|--------------|--------------------------|-------------------|
| P1 | **Vendor `ff.c` (FatFs R0.12c) into `lib/`** and pin the version in `lib_deps` | M1's 13-bit-counter proof, §4.4's `f_expand` semantics, §4.4's `f_sync` `OBJ_DIRTY` question and §5.2's `SDMMC_*TIMEOUT` audit all say "verify in the vendored source" — with nothing vendored those sentences are un-actionable (**B5**) | files present + a version string quoted from the source |
| P2 | **Vendor / pin the HAL** actually used, and pin `platform = ststm32@x.y.z` | `.clinerules` claims HAL 1.28.3 while `platformio.ini` pins nothing; a milestone cannot be signed off against an unpinned, floating library (**B5, WARN-11**) | `platformio.ini` diff + `HAL_GetVersion()` printed at BOOT in M0 |
| P3 | **Add `env:release` with `-O2`** (and keep a debug env) | every CPU figure in §9 and the K8 acceptance are quoted for `-O2`; without the env there is no build the numbers describe | `pio run -e release` builds; `map` file exists |
| P4 | **`.d1ram` section + linker `ASSERT` + `-fstack-usage` map check** (§3.5) | today the only real D1/CCM protection is a runtime range check in `diskio`; M1 is where DMA buffers multiply (**B3**) | a deliberately CCM-placed test symbol **fails the link** |
| P5 | **`RM0090` PDF (the revision actually used) available at the bench** for the SPI under-run wording | the duplication mechanism in **B1** is currently `UNCERTAIN`; the fix is free either way, but the claim must be closed with the manual, not inferred | a citation of the section/page used, recorded in the M0 bench record |
| P6 | Cosmetic but confusing: `.clinerules/*.md` cite `docs/…` while the folder is `Docs/` | provenance hygiene: a citation that does not resolve is treated as a missing source | path fix |

**Until P1–P3 are done, M1 and M2 are not signable as written** (audit V3.4 verdict: *conditional GO for
M0/M1 after the patch pass; NO-GO for any M2 acceptance claim while the sources are absent*).

### 10.3 KPI traceability

| KPI | Verification | Milestone |
|-----|--------------|-----------|
| K1 | register readback; **absolute check via `odr_abs_ppm_ref` against a 1PPS / 1 Hz counter gate (§7.4, WARN-6)**; **cycle-to-cycle stability via the inter-edge-interval histogram (§7.4, WARN-10)** | M0, M3 |
| K2 | **anchor residual per segment (target ≤ 8 ms median, gate 15 ms + 3σ)**; scope INT1 vs packer timestamps; **inter-edge histogram separates ODR jitter from drain latency (WARN-10)**; **inter-axis phase < 5° is the time-domain form of the same KPI (FLAW-4)** | M0, M3 |
| K3 | file size audit over a 1 h log; declared-extent arithmetic | M2 |
| K4 | injected fullness/overrun, SPI abort, card pull, **INT1 deliberately disconnected → the liveness poll catches it within 12 ms and counts it (FLAW-6)**; **counter reconciliation (I-T / I-S / I-X) + no-fabrication check (F20) + block 0 clean after the settling discard (B4) + no co-asserted `OVERFLOW`∧`SENSOR_OVR` block (FLAW-2)** | M2, M3 |
| K5 | 10 000 writes on an 80 %-full pSLC card + **one forced ring overflow at M1** | M1 |
| K6 | **30–40 power cuts at the 4 real phases** (the 2 rotation cuts are dropped — WARN-1); capacity-vs-granularity distinction (WARN-14) | M2 |
| K7 | deliberate hang → IWDG reset, journal + LED 8 | M0 |
| K8 | DWT cycle counters, both SPI modes, **with the measured CS-high floor**; acceptance band 4–8 % | M0, M3 |
| K9 | dedicated ≥ 8 h discharge on a 1000 mAh cell — **deferred post-campaign** | M4 (backlog) |
| K10 | KEY→first written block stopwatch, 20 runs | M2 |
| K11 | reference Python parser on every produced artefact | M2 |
| **K12** | campaign report: duration ≥ 2× plan; **no undeclared loss**; **≤ 5 % of candidate analysis windows rejected, each attributed** (clipping rejections expressed as a **duty cycle**, FLAW-3); **`alias_scan_done = 1` and every candidate peak cross-matched against the second rate** (WARN-8); peaks admissible only per the App. F.2 band taxonomy | M3 + App. F |

### 10.4 Test strategy beyond the milestones

- **`logcore` on the host:** pure C, no peripherals → compile for x86, unit-test the packer, SPSC
  ring (**including the I3 overflow rule, the C/S index bookkeeping and the new I6 mutual-exclusion /
  carry-forward rule — FLAW-2 is a bookkeeping rule, so it is provable *only* here**), drain decision
  (credibility check, fixed-point deficit band, **and the `unc_q16 = 0 ⇒ band = 2` cold-start invariant,
  WARN-5**), **the §3.2.6 quiesce state machine (idle / armed / deferring — modelled, the host has no
  SPI), the §3.2.7 liveness predicate `FSS ≥ WTMP ∧ Δt > 12 ms` and its miss counter (FLAW-6)**, the
  slew/spike bound arithmetic (§3.4, WARN-9), manifest/anchor logic, container geometry and the
  **relocated RESCUE path-solutions** — directory sweep, both-slots-invalid rebuild, rotation-intent
  resolution — against synthetic and corrupted images, and fuzz them. **V3.4 consequence: the fault
  rows demoted from firmware (App. B) are proven here or not at all.** **V3.5 additions (WARN-1,
  WARN-4):** the **App-G identity ladder** (`MAX(session_id, max_dir_scan_id) + 1`, `FA_CREATE_NEW`
  increment chain, `part_index > 255`, 256× `FR_EXIST` refusal), the **217-entry anchor-extension
  ceiling** and the **cross-artefact counter re-derivation** (MAN vs anchor log vs HEADER extended
  counters vs TRAILER) all become **host regression suites** — they are real code paths that M0–M2
  hardware will never execute, so this is the only place they can be proven. `part_index` is asserted
  constant **0** in every artefact they produce.
- **Golden files:** firmware-produced containers parsed by the reference validator with known
  synthetic acceleration, verifying `ng_per_lsb` (**±6 g → 180000**, and the derived-scale rule
  `fs_g == 2 << ((ctrl_reg5 >> 3) & 7)` — FLAW-1), CRCs, **C-domain indices and declared C-jumps**,
  **the absence of any co-asserted `OVERFLOW`∧`SENSOR_OVR` block (FLAW-2)**, `max_abs_raw`, and anchors
  bit-exactly. A golden file whose HEADER says `fs_g = 4` with `ctrl_reg5 = 0x50` **must fail the
  validator** — that pair is exactly the Rev 3.4 bug, and it is now a test.
- **Fault-injection matrix:** Appendix B (firmware-resident subset: **F6, F15, F16, F19, F20**).

---

## Appendix B — Fault-injection matrix

**V3.4 re-scope (audit §3).** In a 1–2 h campaign only faults that touch the capture path or the
declared-loss bookkeeping can silently corrupt a modal result, so **B.1 is the firmware fault
matrix** and contains exactly those five. The remaining 15 V3.3 rows are **not deleted**: B.2 lists
each with its new home (host `logcore` unit test, M0 bench gate, reduced M2 cut schedule, or
post-campaign backlog). Nothing is un-tested; what changed is *where* it is tested.

### B.1 Firmware fault-injection matrix (the kept five)

| # | Injected fault | Expected behaviour | Milestone |
|---|----------------|--------------------|-----------|
| **F6** | `FSS == 0` with `OVRN_FIFO == 1` (forced full FIFO) | **drain-first**; resync only if the deficit is proven; never a silent stall, never an over-read | M3 |
| **F15** | SPI1-DMA TEIF injected mid-frame | mandatory resync, declared bounded loss, `spi_err_cnt++`, **no duplicates downstream**; **V3.5 (B2): the resync's `FIFO_CTRL` write is a PIO write and must go through the §3.2.6 quiesce first; V3.5 (B4): the ≥ 100 ms discard that follows is not a loss and the deficit test stays disabled through it** | M0/M3 |
| **F16** | `OVRN_FIFO` forced permanently 1 (simulated latch) | credibility check prevents over-read; advisory only; deferred resync clears it | M3 |
| **F19** | **Mechanical shake of the microSD socket during capture** — contact bounce, DAT-line dropout, full disconnect | **V3.5 (WARN-3): judged on counter growth shape, not on a card-detect state** — `sd_err_cnt` and `reinit_cnt` rise within the L0 ×3 / L1 ×2 budget, `ring_ovf_cnt` rises only if the stall outlives the 6.375 s ring, and no loss is unattributed; **no CD discriminator is implemented in M0–M2**, so a bounce and a pull converge on `L0 → L1 → L3 + LED` and the test measures whether the ladder *stays bounded*, which is the property that matters. `cd_change_cnt` stays reserved-zero; if PA8 is later confirmed it is recorded as a **diagnostic annotation of this same test**, never as a rung selector | **M2** (promoted from M3 in V3.4 — the single most relevant environmental fault in a wind tunnel) |
| **F20** | Fullness with zero deficit (late by ~15 ms, FIFO exactly 32) | all 32 samples captured, `fifo_full_cnt++`, `samples_lost_sensor == 0`, **regression slope unchanged** | M3 |

Two **forced conditions** are acceptance items rather than injected faults and live in M1 (§10.2): one
stall long enough to drive the ring to **overflow** (exercising I3, `samples_lost_ring`,
`ring_ovf_cnt` and the C-jump), and one write burst with `SDMMC_DTCOUNT`/`DMA_LISR` inspection proving
that no request exceeds the 13-bit counter (WARN-4).

### B.2 Where the other 15 V3.3 rows went

| V3.3 row | V3.4 home | Reason |
|----------|-----------|--------|
| F1 power cut mid data write (200 random points) | **M2 cut schedule, 30–40 cuts at the 4 real phases** (mid-data-write, mid-MAN, mid-anchor-append, mid-header-finalise) | 160 of the 200 points sample regions where a cut is provably harmless; the 4 phases are where a cut can corrupt something |
| F2 power cut mid MAN commit | **M2 cut schedule** (it *is* one of the 4 phases) | dual-slot semantics unchanged |
| F3 both MAN slots corrupted | **host `logcore`** | the firmware path was deleted (§4.3 step 2) |
| F4 power cut at each of 7 rotation steps × 3 | **dropped in V3.5 (WARN-1)** — rotation stays compile-gated off and a part can never fill (69.4 MB of 134.2 MB), so a gate-off build cannot produce the crash. The crash table remains documented in §4.5 for the gate-ON build only | removes one state-machine path, one MAN/TRAILER field pair and one whole M2 test class |
| F5 card pulled during write / MAN / rotation | **folded into F19** (its full-disconnect case) + one M2 clean-pull check | same code path, one test instead of three |
| F7 INT1 stuck / disconnected | **M0 bench gate** (§2.1 routing) **+ the §3.2.7 producer-liveness injection (V3.5, FLAW-6)**: INT1 deliberately disconnected while capture runs, the poll must re-enter the drain within 12 ms and `producer_liveness_miss_cnt` must account for every lost batch | a wiring fact verified with a scope **and** a loss path now testable in software, not only with a scope |
| F8 mode A wrap disabled (forced mode B) | **M0 = mode-B monotonicity + `ΔFSS ≈ N ± 1` + byte-count equality**; A/B byte-identity → **M3** | WARN-12: mode A does not exist at M0, so the V3.3 gate was unpassable |
| F9 DMA buffer forced into CCM / unaligned | **linker `ASSERT` on the `.d1ram` section + map / `-fstack-usage` grep (§3.5, §10.2.1 P4) + `diskio` guard unit test on host `logcore`** | a build-time property; no run-time injection needed — **but the ASSERT does not exist in the repo yet (V3.5, B3), so today this row is a precondition, not a test** |
| F10 `VREFINT_CAL` blank | **host `logcore`** (pure arithmetic) + one M0 read | arithmetic, not a race |
| F11 GC stall 500 ms / 2 s / 7 s | **M1 forced stall** (ring-to-overflow case) + M3 natural stalls from `stall_max_ms` | one deliberate overflow covers the drop path; 500 ms/2 s are ordinary operation |
| F12 LSI at temperature extremes | **post-campaign backlog (M4)** | matters for 8 h+ autonomy, not for a 4 h instrumented run |
| F13 RTC invalid / backup domain lost | **host `logcore`** + one BOOT bench check | deterministic at boot; no continuous-injection value |
| F14 repeated `FR_EXIST` on create | **host `logcore`** against synthetic directory images (App. G) | filesystem-interaction logic |
| F17 RTC second-boundary torn read | **filter unit-tested on host**; the fault is exercised opportunistically in M3 DWT runs | the double-read + plausibility filter is pure arithmetic; reproducible injection is not |
| F18 second owner attempts RTC/ADC access | **deleted → debug assertion** (cut §3) | with the seqlock gone, no second owner exists by construction |

---
