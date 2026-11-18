# 04-HOST-ANALYSIS.md — host/DSP contract

> **Source:** split verbatim from `Docs/SDlogger.md` Rev 3.5 (container format v4).
> **Body content transferred byte-for-byte; this header and the navigation map are the only
> additions.** Section numbers (§…, App. …) keep their original global numbering.
> **Contains original sections:** Appendix F.1-F.7
> **Cross-references:** internal refs (§3.4, App. F.1, A.6 #11, D-3, …) point to the original
> numbering; locate them via the map in `Docs/00-ARCHITECTURE.md`.


## Appendix F — Host analysis rules (modal / DSP contract)

These rules are **normative for the analysis side**. They exist because a logging pipeline can be
bit-perfect and still produce a misleading spectrum.

### F.1 Window usability — gap-aware, not gap-blind

| Condition in the window | Rule (V3.4) |
|-------------------------|-------------|
| Block with `OVERFLOW` (ring drop) or `SENSOR_OVR`, or `lost_or_valid > 0` with a gap flag | **Drop the windows that contain the gap.** Windows are individually gap-free; they are **not** stitched, interpolated or resampled across it — that fabricates data. Nothing else is lost: over a 2 h run at 10 s windows with 50 % overlap, one stall costs 1–2 of ~720 Welch averages (FLAW-5) |
| `PARTIAL` block | **Trim** to its `lost_or_valid` valid samples; the zero padding must never enter an FFT. A PARTIAL block may be used as the tail of a time-domain series, never as a full window |
| `max_abs_raw == 127` (clip screening code) | **V3.5 (FLAW-3) — screen, verify, then measure the clipping *duty cycle*, and never notch a continuously clipped waveform.** V3.4's row (screen → notch each `\|raw\| == 32767` sample with an order-2 hold or a linear fill → reject only if notched samples > 5 %) was written for **isolated transient** excursions. At a held resonance the sensor sits **at the rail for a quarter-period at a time**: the clipped samples are not scattered spikes, they are the **top of the waveform**, and notching them with a hold or a fill *flattens the resonance peak* — which biases the **peak location**, i.e. the campaign's only required output, and invents a broadband skirt where there is a physical maximum. Procedure now: (1) scan the payload, count `\|raw\| == 32767` samples per window; (2) compute the **duty cycle** `clipped / total` for that window; (3) if **≤ 5 %**, notch the isolated samples as before (they are genuine single-sample excursions, typically shock/switching transients) and mark them; (4) if **> 5 %**, **exclude the window from peak-location results** — it is not notched, not order-2 held, not filled, and not "repaired" in any way; it is reported as *clipped, location unusable*, and counted as one of the ≤ 5 % rejected windows of K12. Report the duty cycle with the window id (F.6 §5) |
| `FIFO_FULL` set, no proven loss | **Usable.** Annotate; this is an advisory, not a gap |
| `VDD_LOW` set | Usable; annotate and correlate — it is a power event, not a data gap |
| `CLOCK_INVALID` | Usable for relative-frequency work; absolute wall-clock claims are not supportable |
| `LOSS_SAT` set | Usable for location ("gap at or after *t*, magnitude ≥ saturated bound"), **not** usable for loss arithmetic |

**Clipping duty cycle of a sine at amplitude `A` against a clip rail `C`** (`duty = 1 − (2/π)·asin(C/A)`,
the fraction of the period for which `|x| > C`; V3.5, FLAW-3 — the arithmetic that justifies the ±6 g
default and the 5 % budget):

| Peak `A` | clipping begins at | **±6 g** (`0x50`, rail 5.898 g) | **±4 g** (`0x48`, rail 3.932 g, not used) | **±8 g** (`0x58`, rail 7.864 g) | Verdict under the 5 % rule |
|---------:|----------------------|--------------------------------:|------------------------------------------:|-------------------------------:|-----------------------------|
| 3.0 g | — | 0 % | 0 % | 0 % | usable everywhere |
| **3.944 g** | ±4 g | 0 % | **5.0 %** | 0 % | **±4 g budget point** |
| 4.0 g | ±4 g | 0 % | **11.7 %** | 0 % | ±4 g rejects the window |
| 4.2 g | ±4 g | 0 % | **22.9 %** | 0 % | ±4 g rejects the window |
| 5.0 g | ±4 g | 0 % | **42.4 %** | 0 % | ±4 g rejects, ±6 g clean |
| **5.916 g** | ±6 g | **5.0 %** | — | 0 % | **±6 g budget point** |
| 6.0 g | ±6 g | **11.7 %** | — | 0 % | ±6 g rejects, ±8 g clean |
| **7.888 g** | ±8 g | — | — | **5.0 %** | **±8 g budget point** |

**Consequences, stated plainly.** (a) At ±6 g the 5 % window-rejection rule is exhausted at a
**≈ 5.92 g** peak, which covers the 1–5 g resonance peaks this campaign expects **and** the ±5.90 g
clip point, i.e. the range and the rejection policy are consistent. (b) At ±4 g the rule is exhausted at
**≈ 3.94 g** — a peak 0.3 % above full scale — so every resonance window in the 4–5 g band would have
been rejected (11.7–42.4 %): the audit's arithmetic is right and V3.4's range choice was wrong. (c) A
window rejected at 10–42 % duty cycle is **not a data-quality failure, it is a range-selection failure**,
and the corrective action is the BOOT pre-test of §3.1/§3.3 (`max_abs_raw_max → 0x58` for the next part),
not a host-side repair. (d) 5 % is a **project budget**, not a physical limit: below it a notch is
defensible because the clipped samples are isolated; above it the notch *is* the peak.

**Allowed without qualification:** Welch / segmented PSD over gap-aware windows (each window
individually gap-free), and time-domain gap-compensated concatenation for *locating when* events
happened, using the C-domain rule of §1.3.7.
**Still forbidden — this is the real integrity boundary:** phase, coherence, OMA, mode-shape
processing, transfer functions and any cross-spectral quantity **across a declared gap**. V3.3's
"never concatenate PSD estimates" was the wrong prohibition; it discarded usable magnitude physics
while saying nothing about the quantities that are actually destroyed by a phase discontinuity.

Analysis windows are aligned to the **425 ms block grid**, because that is the granularity at which
discontinuities are declared — but note that window *alignment* is a bookkeeping convenience, not a
signal-processing requirement (F.7).

### F.2 Frequency-band policy — three regions keyed to `f_true`, not two keyed to the plot

**V3.5 (FLAW-5): V3.4's two-band split ("trusted ≤ 400", "400–800 attenuated, suspect", "above 800
unreachable") collapsed two physically different situations into one word — *alias* — and silently dropped
the only case that can actually corrupt a modal result. The taxonomy below is normative.**

Let `fs = 1600 Hz`, `f_n = 800 Hz`, `fc = 400 Hz` (AA knee), `f_true` = the frequency of the physical
component generating the energy.

| Region | `f_true` | What the sampler does | Reportable? |
|--------|----------|----------------------|-------------|
| **(i) Trusted band** | **0 … 400 Hz** | flat passband: `|H| ≈ 1`, no folding (the first image of anything above `f_n` lands below 400 only after ≥ −19 dB attenuation, §F.2 residual-exposure row) | **Yes — location and (relative) amplitude** |
| **(ii) In-Nyquist, attenuated** | **400 … 800 Hz** | **measured at its own frequency** — the DFT bin at `f_true` is the correct bin. The component is **not** an alias: no folding occurred. Only `\|H(f)\|` has tilted it (2nd-order assumption: −3 dB @ 400, −12.3 dB @ 800) | **Location yes, amplitude no.** Report as *attenuated, in-Nyquist*: a legitimate, locatable peak whose height is filter-shaped. **Do not discard it, and do not call it an alias** |
| **(iii) Above Nyquist — folding images** | **> 800 Hz** | unmeasurable at this ODR; the energy appears at `\|k·fs − f_true\|` for integer `k`, **anywhere in 0…800 Hz, including inside region (i)** | **No, unless region (iii) is disproved for that peak by the dual-rate scan (F.3)** |

**(iii) is the dangerous case, and it is the one V3.4 never stated.** Worked example, cantilever
(`f₂/f₁ = 6.27`, `f₃/f₁ = 17.55`, App. F.5), `f₁ = 100 Hz`: mode 3 sits at **1755 Hz**, above Nyquist.
Its first image is `|1600 − 1755| = `**`155 Hz`** — inside the trusted band, 15 Hz from a plausible
`f₁`. How high it arrives depends entirely on an unknown filter order, so both bounds are quoted: for a
**2nd-order** section at `fc = 400 Hz`, `|H| = 1/√(1+(f/fc)⁴)` gives `1/√(1+4.3875⁴) = 0.052` →
**≈ −29.6 dB** (the audit's rounded "≈ −32 dB"); for a **1st-order** section, `1/√(1+4.3875²) = 0.222`
→ **≈ −13.1 dB** (audit: "≈ −15 dB"). `[DS §10.3]` states plainly that "the filter order and roll-off are
**not specified**", so neither number is a datasheet fact and the spread is **17 dB wide** — which is the
whole point: a 155 Hz image can arrive anywhere between −13 dB and −30 dB of a 1755 Hz component. Against
a strong 1755 Hz structural or bracket response (24 dB more energy than a low mode is unremarkable for a
bracket), the image lands at a height comparable to a genuine 100 Hz mode. **In a single-rate spectrum it
is indistinguishable from a mode, and the dual-rate test of F.3 is the *only* discriminator this design
has.**

**(ii) worked example, corrected.** Simply supported beam (`f₂/f₁ = 4.00`, `f₃/f₁ = 9.00`, App. F.5),
`f₁ = 60 Hz`: the modes are at **60 Hz (f₁), 240 Hz (f₂ = 4.00 × 60) and 540 Hz (f₃ = 9.00 × 60)**.
V3.4's prose called **4.00** the third-mode ratio in exactly the paragraph that decides whether 400 Hz is
wide enough, so it placed the third mode at 240 Hz and under-stated the modal spacing by a factor of 2.25.
**540 Hz is region (ii): in-Nyquist, measured at 540 Hz, attenuated ≈ −5…−8 dB** under the 2nd-order
assumption at `fc = 400 Hz`. It is **not** an alias and must not be discarded — it is a legitimate peak
whose **location** is valid input to a mode-order interpretation and whose **height** is filter-shaped.

**Rule behind the band edge (unchanged from V3.4, FLAW-3): `fc = fs/4`.** The AA transition band must lie
strictly below Nyquist; with `fc = 800 Hz` (V3.3) the knee sat *at* Nyquist and `f_true ∈ [800, 1440]`
folded into 160…800 Hz at 0…−12.3 dB, i.e. into the interior of the trusted band with no attenuation at
all.

**Residual exposure, stated honestly.** With `fc = 400 Hz` the first folding image for
`f_true ∈ [1200, 1600]` still lands inside 0…400 Hz at **−19…−36 dB** (2nd-order assumption; −8…−16 dB
if the order is 1). Region (iii) is therefore **mitigated, never eliminated**: the campaign's defence is
(i) the mandatory dual-rate scan (F.3), (ii) the digital decimation stage (F.7) whose stopband *is*
known, and (iii) the modal pre-estimate of F.5, which predicts where a genuine mode can be and therefore
what a suspicious in-band peak probably is.

**Amplitude consequence of the ±6 g default.** AA-band noise stays **3.00 mg rms** (§3.3) whatever the
range; referred to code it is **16.7 LSB rms** at ±6 g. Whether that is a real SNR cost relative to ±2 g
depends on whether [DS]'s 150 µg/√Hz (quoted only at `FS = 00`) scales with full scale — **unmeasured**,
M0 measures it (FLAW-3, §10.2). The location bias of the AA tilt across a narrow resonance is
`s·σ² ≈ 0.2 Hz` at 600 Hz and **≪ 0.02 Hz** below 400 Hz, where the filter is flat: range choice does not
touch it.

### F.3 Alias discrimination — **a mandatory pre-campaign procedure**, not a milestone test

**V3.5 (WARN-8): V3.4 scheduled this as one of M3's tests. Alias discrimination is worthless as a
milestone test and worth a great deal as a campaign step: a campaign that never ran it produces a peak
list that cannot be defended afterwards, and re-running a 2 h tunnel excitation is not available.** The
2-minute dual-rate sweep below now runs at **BOOT**, before `PREPARE` creates the campaign part (§3.1),
and its completion is recorded in the campaign HEADER (`alias_scan_done`, `alias_scan_odr_hz`,
`alias_scan_aa_bw_hz` — A.1 `0x0CA…0x0CF`, decision **D-3**). The validator reports
`alias_scan_done == 0` as a K12 failure (A.6 #11).

**Primary procedure — change the sample rate *and* the AA bandwidth together, holding the ratio constant
(`fc/fs = 1/4`, both configurations legal per [DS §10.3]):**

| Config | ODR (`CTRL_REG4`) | AA bandwidth (`CTRL_REG5` BW bits) | `CTRL_REG5` value | `fc/fs` | Nyquist | Trusted band | Duration |
|--------|-------------------|--------------------------------------|-------------------|---------|---------|--------------|----------|
| 1 (campaign default) | **1600 Hz** → `0x9F` | **400 Hz** (`BW = 01`) | **`0x50`** (±6 g, 0.18 mg/LSB) | **1/4** | 800 Hz | 400 Hz | the campaign |
| 2 (discriminating) | **800 Hz** → `0x8F` (`ODR = 1000`, [DS:520]) | **200 Hz** (`BW = 10`) | **`0x90`** (same FSCALE `010` = ±6 g) | **1/4** | 400 Hz | 200 Hz | **2 min, at BOOT** |

`fc/fs = 1/4` in both, so the filter shape is identical in **normalised** frequency and only `fs` moves.
A **real** peak below 200 Hz is flat in both configurations: it **stays**. An **image moves**
(`f_alias = |k·fs − f_true|`), and a filter-edge artefact changes height rather than position. The
reconfiguration path is normative (§3.1): write `CTRL_REG4` and `CTRL_REG5` together, apply the ≥ 100 ms
settling discard, re-base the chain timing state (B4), then restore config 1 before `PREPARE`.

**Every candidate peak is cross-matched, not only peaks above 400 Hz (WARN-8, the correction that
matters).** V3.4's trigger was "a peak above the trusted band needs an F.3 justification", which cannot
catch the dangerous case at all: the 155 Hz image of a 1755 Hz bracket mode (F.2 region iii) sits **below**
400 Hz and triggers nothing. The rule now is: **the host cross-matches the two peak lists exhaustively** —
for every candidate peak in either list, the second rate must either confirm it at the predicted position
or explain its absence. A peak present at 1600/400 and absent (or moved) at 800/200 is an **image**, and
the report says so. Cost: two 2-minute sweeps and one peak-list diff.

**Secondary — BW change at fixed ODR** (1600 Hz with `BW = 01` vs `BW = 10`): weaker, retained only for
peaks well below the lower knee, because lowering `fc` attenuates real content in the removed band too.

**Noise-floor reference run:** sensor mechanically isolated, excitation source (fan/machinery) running,
**in both configurations** — it calibrates the folded-noise floor of the trusted band and turns the
"−19…−36 dB folding" statement from arithmetic into a measurement. It also produces the per-range idle
rms of FLAW-3 if run once per candidate full scale.

**What Welch averaging does to this decision (V3.5, WARN-8 — state it, do not lean on it).** Averaging
`≥ 1250` periodograms (a 2 h run at 10 s windows, 50 % overlap) lowers the **broadband random floor by
≈ 10·log10(1250) ≈ 31 dB**, which is why a small deterministic peak becomes visible at all. But a
**coherent deterministic tone — including a tone that is a folded image of a bracket mode at 1755 Hz, a
blade-pass frequency or a vortex-shedding frequency anywhere in 800–3000 Hz — is sharpened by the same
31 dB**. Welch averaging therefore *increases confidence in an aliased peak at exactly the same rate it
increases confidence in a real one*, while the stopband attenuation that created the alias is an
**assumption** ([DS] gives no order, no roll-off). **Normative consequence:** no peak may be reported as a
structural mode on the strength of averaging gain alone; the dual-rate cross-match of F.3 is the required
disambiguation, and the report shall quote the averaging gain and the cross-match result side by side.

### F.4 Time-scale fitting

Follow §7.2: fit segments broken by **sensor-side** gaps only (never by `OVERFLOW` or `FIFO_FULL`),
robust fit (Theil–Sen or σ-clipping), per-segment ±5000 ppm guard, and the **two** reconciliation
invariants (time base in C, container in S) — never the single mixed invariant of V3.3.
Report the fitted `odr_ppm` and its residual alongside every spectral result, and state the absolute
correction from `odr_abs_ppm_ref` next to it: a frequency axis without a stated scale-factor
uncertainty **and** a traceability statement is not a measurement (WARN-6, App. §7.4).

### F.5 Pre-test modal estimate

Use the **actual boundary conditions**, not a remembered ratio:

| Boundary condition | **f₂/f₁** | **f₃/f₁** |
|--------------------|-------|-------|
| Free–free | 2.76 | 5.40 |
| Cantilever (clamped–free) | 6.27 | 17.55 |
| Simply supported | 4.00 | 9.00 |

**These are second- and third-mode ratios, and V3.4 mislabelled them in prose (FLAW-5).** The table itself
was always right; the sentence below it ("a simply-supported beam whose `f₃/f₁ = 4.0` puts mode 3 above
400 Hz") used **4.00 as if it were `f₃/f₁`**, when 4.00 is `f₂/f₁` and `f₃/f₁ = 9.00`. In the paragraph
that decides whether 400 Hz is wide enough, that understated the modal spacing by 2.25×, which is how a
document can conclude its band is sufficient and be wrong.

**Restated correctly, with the band taxonomy of F.2.** For a ~3 m beam the first bending modes are
typically well under 400 Hz, but the ratios — therefore the expected spacing — depend entirely on the
mounting:

| Mounting | `f₁` guess | mode 2 | mode 3 | What the instrument sees |
|----------|-----------:|-------:|-------:|--------------------------|
| Simply supported | 60 Hz | 240 Hz | **540 Hz** | mode 1 and 2 in region (i); **mode 3 in region (ii)** — in-Nyquist, measured at 540 Hz, location valid, amplitude attenuated ≈ −5…−8 dB. **Report it; do not discard it as an alias** |
| Cantilever | 100 Hz | 627 Hz | **1755 Hz** | mode 2 in region (ii); **mode 3 in region (iii)** — above Nyquist, invisible, **and its image at 155 Hz lands in the trusted band** (F.2) |
| Free–free | 40 Hz | 110 Hz | 216 Hz | all three modes in region (i) — this is the case where 400 Hz is comfortably wide |

**Decision that must be taken before `fs` is chosen, and recorded in the campaign report:** the mounting
and the expected mode count. For a cantilever the trusted band is *not* wide enough for mode 3 and the
F.3 dual-rate cross-match is not optional but decisive; for a simply-supported beam mode 3 is in band
enough to locate; for free–free 400 Hz is generous. **Decide the mounting and the expected mode count
before choosing `fs`** — the instrument cannot repair a band choice after the excitation has been run.

### F.6 Campaign acceptance report (K12)

Every campaign must produce:

1. **Duration.** Instrumented run ≥ 2× the planned measurement length (a 4 h run for a 2 h campaign),
   verified from the file, not from the operator's notebook.
2. **No undeclared loss.** Every lost sample is attributed to exactly one named counter:
   `samples_lost_sensor` (+ `fifo_ovr_cnt`), `samples_lost_ring` (+ `ring_ovf_cnt`), `spi_err_cnt`,
   **`producer_liveness_miss_cnt` (V3.5, FLAW-6)**. `fifo_full_cnt > 0` is acceptable and reported as
   advisory. V3.3's "zero declared loss over all analysis windows" is **withdrawn** (FLAW-5): it forbade
   the compromise the operational brief explicitly allows. **No block may be counted in two domains
   (FLAW-2 / I6): if the report ever needs one, the file is defective, not lossy.**
3. **Window budget.** ≤ **5 %** of candidate analysis windows rejected (10 s windows, 50 % overlap on
   a 2 h run ⇒ ~720 candidates ⇒ ≤ 36), with **every rejection attributed** to a block flag and a
   counter, in a table. A rejection with no attribution is a finding, not a footnote. **Clipping
   rejections are stated as a duty cycle, not as a count (V3.5, FLAW-3, F.1).**
4. **Gap location.** For every declared discontinuity: wall-clock position ± (425 ms ⊕ anchor error),
   the responsible counter, and the magnitude (A.6 rule 10).
5. **Counters and power.** `anchor_reject_cnt`, `stall_max_ms`, `sd_err_cnt`, `reinit_cnt`,
   `spi_err_cnt`, **`spike_cnt` (§3.4, with the per-block table derived from the payload)**,
   **`producer_liveness_miss_cnt` (§3.2.7)**, `cd_change_cnt` (if CD fitted — **reserved-zero for M0–M2,
   WARN-3**), `vdd_min_mv`; `vdd_x10mv == 127` read as "≥ 3.275 V", never as a measurement (§6.2 cap).
6. **Time base.** Fitted `odr_ppm` with **the achieved residual per segment quoted** (WARN-6), **and** the
   absolute `Fs` with its uncertainty from `odr_abs_ppm_ref` (§7.4, measured against 1PPS / a 1 Hz counter
   gate). No peak in this report may carry a 0.005 Hz location uncertainty without its segment residual.
7. **Band, per the F.2 taxonomy (V3.5, FLAW-5).** Every reported peak is labelled with its region:
   **(i)** ≤ 400 Hz trusted; **(ii)** 400–800 Hz in-Nyquist — location valid, amplitude not;
   **(iii)** peak **not** reported unless the second sample rate of F.3 confirms it at the predicted
   position. **Every candidate peak is cross-matched, not only peaks above 400 Hz** — an image of
   `f_true > 800 Hz` folds below 400 Hz and is invisible to any band-limit test. The report states
   `alias_scan_done`, the second rate used, and the cross-match outcome per peak.
8. **Clip ledger.** Blocks with `max_abs_raw == 127`, the samples verified as `\|raw\| == 32767`, the
   **clipping duty cycle of every affected window**, and what was done (isolated-sample notch ≤ 5 % duty
   cycle; above it the window is excluded from location results, F.1). A resonance that was clipped must
   be reported with its duty cycle — that is the difference between "we lost the resonance" and "we
   measured it, with a caveat". Physical clip level is read from `ng_per_lsb` (**±5.90 g** at the default).
9. **Inter-axis coherence (V3.5, FLAW-4).** The M0 gate result (`\|Δφ\|` X–Y and X–Z over 5–400 Hz, or the
   stated constant offset) is quoted in every campaign report that uses phase, coherence, OMA or mode
   shapes — a mode-shape result without a coherence statement is not usable.

### F.7 Spectral analysis recipe (V3.4, new — WARN-8)

V3.3 said only "windows aligned to block boundaries", which left the dominant term of the error
budget undefined: with 425 ms blocks the raw bin spacing is **2.35 Hz**, while pinpointing a
resonance to ≲ 0.1 Hz needs **≥ 10 s windows (≥ 24 blocks)**. Until the window length, the window
function, the estimator type and the interpolation policy are fixed, every ppm figure in §7 is moot —
spectral resolution, not time-base ppm, sets the achievable peak-location accuracy. The following is
**normative**; where it disagrees with §7, §7 governs the *time axis* and this governs the *frequency axis*.

| Item | Normative value | Arithmetic |
|------|-----------------|-----------|
| Sample rate for analysis | `sample_rate_hz` from the HEADER (1600 Hz). **V3.5 (WARN-7): "never resample **across a declared gap**, never interpolate, never zero-fill"** — resampling a *gap-free* record is permitted by the decimation stage below, and only there | time-domain gaps are handled by dropping windows, not by resampling (§1.3, F.1). V3.4's flat "resampled **never**" also forbade the one legitimate use of the 4× oversampling headroom this sensor offers |
| **Decimation stage (V3.5, WARN-7 — normative, optional in use)** | A **gap-free** 1600 Hz record **may** be decimated **by 2 → 800 Hz** with a **linear-phase FIR low-pass, `fc ≈ 300 Hz`, stopband reached by 450 Hz** (e.g. a windowed-sinc or Parks–McClellan design with ≥ 60 dB stopband, implemented as a two-pass `filtfilt`-style phase-linear decimator, or an equivalent polyphase half-band). **Any band edge a report attributes to the sensor below Nyquist must instead be attributed to this digital filter**, whose order, `fc` and stopband *are* known | This is the only mitigation available for the sensor's unspecified analog AA order/roll-off ([DS §10.3]: "the filter order and roll-off are **not specified**"): it replaces an unmeasured analog band edge with a known, phase-linear one, and it costs a factor of 2 in host data volume. **Forbidden** across a declared gap, and forbidden before a coherence measurement is quoted (phase-linear only) |
| Primary window (single-spectrum / peak location) | **10 s = 16 000 samples**, Hann, **50 % overlap**, ×4 zero-padded FFT | raw bin `fs/N = 0.1 Hz`; ENBW(Hann) = `2·fs/N = 0.2 Hz`; 4× zero padding → interpolated bin 0.025 Hz |
| Averaged spectrum (PSD) | **Welch, 4096-sample (2.56 s) frames**, Hann, 50 % overlap | bin `1600/4096 = 0.390625 Hz`; a 2 h run gives `≈ 0.9 × 7200/2.56 ≈ 2500` frames ⇒ ≥ 1250 averaged periodograms → noise floor `≈ −10·log2(1250) ≈ −31 dB` below a single frame |
| Window function correction | **Hann**, amplitude correction `1/0.5 = 2` (coherent gain) applied as `2/(fs·Σw/2)`-style normalisation; PSD scaled by `1/(fs·Σw²)`; `Σw²/(Σw)²` used for ENBW reporting | applying an un-corrected Hann biases every amplitude by −3.15 dB |
| Estimator declared | **exactly one of:** single-sided amplitude spectrum (linear amplitude, ng or mg) **or** PSD (`ng²/Hz`), stated per plot, never both in one figure | "amplitude or PSD" was V3.3's unanswered question |
| Window placement | free placement, **aligned to the 425 ms block grid** for gap bookkeeping only | a 10 s window spans ~23.5 blocks, so block alignment is a bookkeeping convenience, not a spectral requirement |
| Gap handling | window containing a block with a declared discontinuity ⇒ **drop the window** (F.1); never stitch, never interpolate, never zero-fill across a gap | for a 2 h run: one 400 ms stall costs 1–2 of ~720 windows |
| DC / low-frequency | **0.5 Hz high-pass, 2nd-order zero-phase (`filtfilt`)**, applied per gap-free window, not across a declared gap | kills the ±40 mg typ zero-g offset and the ±0.5 mg/°C drift [DS §4] without a phase artefact; the beam's first mode is ≫ 0.5 Hz by construction (F.5) |
| Clipping | `max_abs_raw == 127` ⇒ scan payload for `\|raw\| == 32767`, compute the **clipping duty cycle** of the window, notch only if it is ≤ 5 % (isolated samples), else **exclude the window from peak-location results** (F.1) | FLAW-4 kept the window; **FLAW-3 removes the notch's licence at a held resonance**, where clipped samples are the top of the waveform and notching them biases the peak location |
| **Inter-axis time coherence (V3.5, FLAW-4 — normative requirement)** | **X/Y/Z triples are time-coherent to < 5° across the trusted band 0…400 Hz — measured, not assumed.** Any phase, coherence, OMA, mode-shape or cross-spectral product on this data is admissible only with the M0 gate result quoted (F.6 §9) | Three per-axis FIFOs popped by one read ([DS:804-830]) and one `FSS` count for the whole triple mean a 1-sample inter-axis skew is **invisible to every CRC and counter in the container**. 1 sample = **625 µs** → **22.5° @ 100 Hz, 45° @ 200 Hz, 90° @ 400 Hz**. A PSD pair looks clean while the mode shape it produces is garbage — this is the failure mode no counter in this design can report |
| **Re-alignment after a disturbance (V3.5, FLAW-4 + B4)** | after any resync, SPI/DMA abort, ODR change or self-test, the **existing ≥ 100 ms settling discard is the coherence re-alignment window**: no cross-spectral product may be computed from windows overlapping it | the discard already exists (§3.1); using it as the coherence guard costs nothing and is the only defensible boundary, because the pop order of the three FIFOs is undefined across an abort [DS F10] |
| **Link-plausibility screen (V3.5, WARN-9)** | the §3.4 slew gate's `spike_cnt` and its payload-derived per-block table are quoted with the spectrum; a window whose spike count is non-zero may be **annotated**, not silently averaged | an SPI bit flip produces a plausible-looking sample that passes every CRC and injects broadband energy that Welch averaging will report as structure |
| **Averaging and aliases (V3.5, WARN-8)** | when ≥ 1250 periodograms are averaged, the report states that the ≈ 31 dB floor reduction applies to **broadband** noise only and that a **coherent** tone — real or folded — is sharpened by the same factor | averaging raises confidence in an alias exactly as fast as it raises confidence in a mode; the stopband attenuation that produced the alias is an assumption (F.3) |
| Time-scale correction | divide the frequency axis by `(1 + odr_ppm/10⁶)` per **fit segment** before comparing peaks between runs; never correct with an uncertain loss estimate (§7.2) | 20 ppm at 150 Hz = 0.003 Hz |
| Peak location accuracy | report the **interpolated** peak (parabolic or Quinn on the zero-padded spectrum) with uncertainty `≈ 0.2·δf_bin/SNR`; at SNR 40 dB this is **≈ 0.005 Hz**, and the AA flat-band location bias below 400 Hz is **≪ 0.02 Hz** | **spectral resolution 0.1 Hz dominates everything else**: ODR ppm contributes 0.003 Hz, anchor error 0.025 Hz-equivalent, AA bias ≪ 0.02 Hz |
| Resolution requirement | required resolution comes from the **expected mode spacing** (F.5) — resolve the smallest gap you intend to separate, then state it | for a cantilever `f₂/f₁ ≈ 6.27` the resolution requirement is trivial; for closely spaced modes it is the binding constraint |
| What must never be estimated across a gap | phase, coherence, OMA, mode shapes, FRFs, cross spectra | F.1 — the phase is discontinuous even where the magnitude looks fine |
| Deliverable per channel | PSD (Welch, 2.56 s) **and** 10 s peak-location spectrum, both on the corrected axis, with the rejection table of F.6 §3 | makes the report reproducible by a third party without the firmware |

**Error budget, in the order that matters (10 s Hann, 1600 Hz, trusted band ≤ 400 Hz):**

| Contribution | Magnitude | Rank |
|--------------|-----------|------|
| Spectral bin / interpolation (0.1 Hz bins, ×4 padded, SNR-limited) | ≈ 0.005–0.1 Hz | **1 — dominant** |
| AA filter tilt across a narrow resonance, if the peak sits above 400 Hz | up to ≈ 0.2 Hz at 600 Hz; **≪ 0.02 Hz** below 400 Hz | 2 (and eliminated by staying in band) |
| Anchor residual on the fitted scale (≈ 5–8 ms, WARN-7) | ≈ 0.025 Hz-equivalent at 150 Hz, per segment | 3 |
| Absolute `Fs` (LSE parabola + aging, order 10–30 ppm) | 0.0015–0.0045 Hz at 150 Hz | 4 — real, but belongs in the traceability statement, not in the noise |
| Sensor ODR nonlinearity/thermal drift after per-segment fitting | ≪ 0.005 Hz | 5 |

---
