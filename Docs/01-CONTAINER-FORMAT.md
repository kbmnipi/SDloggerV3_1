═══════════════════════════════════════════════════════════════════════════════
  FROZEN DOCUMENT — CONTAINER FORMAT v4
═══════════════════════════════════════════════════════════════════════════════
  Status:           FROZEN — authoritative specification
  Format version:   4
  Frozen at:        2026-11-18
  Frozen by:        Cline (SD Logger architect agent)
  Source revision:  Docs/SDlogger.md Rev 3.5
  Split from:       Docs/SDlogger.md lines 962–2161 (§4.2 + App A) and 2604–2645 (App G)
  Content hash:     346f632cad95576e96cb201355363ba1d2c7874e0974797982b08fb7226c6e9a
  Line count:       534

  CHANGE POLICY
  ─────────────
  1. This document is FROZEN. It defines container format v4 and is the sole reference for
     firmware writers, host validators, and golden-file generators.
  2. NO field may be moved, renamed, reinterpreted, or removed. NO offset may change.
     NO struct may grow. The geometry 4096 = 680 × 6 + 16 is immutable.
  3. Fields carved from reserved runs (A.0.1) are the ONLY additions permitted without a
     format version bump, and only if they satisfy ALL of the following:
       a) the reserved run has sufficient space;
       b) no existing offset shifts;
       c) format_version / man_version / trailer_version remain 4;
       d) the change is recorded in the CHANGE LOG below before implementation.
  4. Any change that violates rule 2 or 3 requires a new format version (v5+) and a full
     migration path. This is NOT a patch — it is a new format.
  5. Errata (typos in prose, clarification of ambiguous wording that does not change byte
     layout) may be applied with a dated annotation in the CHANGE LOG below. The byte-exact
     tables themselves are never edited for errata — only annotated.
  6. The Python reference validator and every golden file MUST be regenerated from THIS
     document, never the reverse.

  CHANGE LOG (post-freeze)
  ─────────────────────────
  [empty — no changes have been made since the freeze]

═══════════════════════════════════════════════════════════════════════════════
# 01-CONTAINER-FORMAT.md — byte-exact format, frozen

> **Source:** split verbatim from `Docs/SDlogger.md` Rev 3.5 (container format v4).
> **Body content transferred byte-for-byte; this header and the navigation map are the only
> additions.** Section numbers (§…, App. …) keep their original global numbering.
> **Contains original sections:** §4.2 Container V3SSSSPP.BIN, Appendix A, Appendix G
> **Cross-references:** internal refs (§3.4, App. F.1, A.6 #11, D-3, …) point to the original
> numbering; locate them via the map in `Docs/00-ARCHITECTURE.md`.

### 4.2 Container `V3SSSSPP.BIN`

```
0x000000  HEADER 4096 B = 512 B immutable struct + anchor log (3 sectors, append-once) + zero pad
0x001000  BLOCK[0]  4096 B = 16 B header + 4080 B payload (680 samples)
          BLOCK[1] … BLOCK[N-1]   (last one may be PARTIAL, zero-padded)
          TRAILER 4096 B = 512 B struct + 3584 B zero pad (clean stop / rotation only)
```

Companion file **`V3LOG.MAN`** — 1024 B, two alternating 512 B slots (§4.3).

**Naming: `V3SSSSPP.BIN`** — `SSSS` = 4 hex digits of `session_id` (0…65535), `PP` = 2 hex digits of
`part_index` (0…255). 8.3-legal. 65536 sessions × 256 parts × 128 MiB = 32 GiB per session.

**Geometry (exact):**

| Quantity | Value |
|----------|-------|
| Block | 4096 B = 16 + 4080; 680 samples; 425.0 ms |
| `max_data_blocks` | **32768** (`block_seq` 0…32767) |
| Data region | 32768 × 4096 = **134,217,728 B = 128 MiB** |
| `prealloc_bytes` | (1 + 32768 + 1) × 4096 = **134,225,920 B** |
| Part duration | 32768 × 0.425 s = 13,926.4 s = **3.868 h** |
| Blocks/s | 1600/680 = **2.3529** |

The trailer slot is inside the pre-allocation → writing it never allocates. Geometry is
audit-verified overlap-free: HEADER struct `0x000–0x1FC`, anchor log `0x200–0xDFF`
(3 sectors; extendable append-once to `0xFFF`), blocks from `0x1000`, trailer at
`0x1000 + N×4096`; MAN is a **separate** 1024 B file, so its `0x000/0x200` slots do **not**
collide with the BIN header.

**Self-describing:** `sample_rate_hz`, `ng_per_lsb` (integer, no float ambiguity), `sample_format`,
`endianness`, `trusted_band_hz` and the sensor register dump make misinterpretation impossible.
Conversion: `accel_ng = raw_int16 × ng_per_lsb`; `mg = accel_ng / 1e6`.

Full byte-exact tables: **Appendix A**.

## Appendix A — Byte-exact container specification

**Global rules**
- All multi-byte integers **little-endian**. All `reserved` bytes **must be zero** and are covered
  by the enclosing CRC unless stated otherwise.
- **CRC-32/ISO-HDLC (IEEE 802.3):** reflected poly `0xEDB88320`, init `0xFFFFFFFF`, refin/refout true,
  xorout `0xFFFFFFFF`. Check value for `"123456789"` = `0xCBF43926`. Equals Python `zlib.crc32()`.
- **CRC-8/ATM:** poly `0x07`, init `0x00`, refin/refout false, xorout `0x00`. Check = `0xF4`.
- Unknown enum values → the validator must reject the artefact, not guess.
- **Container format version = 4** (doc rev is separate).
- **No field is silently reinterpreted and no field is moved.** Every V3.4 addition lands in a
  reserved run at its natural offset; offsets that V3.3 defined keep their meaning unless this
  appendix says so in one line. That is deliberate: V3.3's damage came from a field whose *meaning*
  was ambiguous (`first_sample_index`), so V3.4 states meanings in the validator, not only in prose.

### A.0 V3 → V4 delta (normative)

| # | Change | Where | Note |
|---|--------|-------|------|
| 1 | Version fields `3 → 4` | HEADER `0x004`, MAN `0x004`, TRAILER `0x004` | Also closes a V3.3 self-contradiction: A.4 said `man_version = 3`, A.5 said `trailer_version = 2`, the global rule said 3 |
| 2 | Block index word: **bits 24:0 = C** (captured index), **bits 31:25 = the 7 block flags** | block `0x02` | FLAW-1. The flags *byte* is deleted; CRC-8 coverage (bytes 0…14) is **unchanged**, so a torn header is still detected |
| 3 | Block `0x0E`: `flags` → **`max_abs_raw`** | block `0x0E` | FLAW-4; CLIP is derived, never stored |
| 4 | `lost_or_valid` **interpretation** by flags (not moved, not widened, still u16) | block `0x0A` | FLAW-1: the field carries ring-domain loss when `OVERFLOW` is set and the sensor-side deficit when only `SENSOR_OVR` is set — they move `C` differently (§1.3.7, A.6 rule 3) |
| 5 | Anchor log 7 → **3** sectors, **append-once** | HEADER `0x200…0xDFF` | per-sector geometry byte-identical; `sector_index` added in a reserved slot; extension into the pad stays append-once |
| 6 | Declared extent replaces truncation everywhere | MAN `0x030`, TRAILER `0x014` | physical file size is **always** `prealloc_bytes`; WARN-2 |
| 7 | Rotation fields exist only with `V3_ENABLE_ROTATION=1`, **offsets unchanged** | MAN `0x094…0x0A7` | compile gate, not a format change; zero-filled otherwise |
| 8 | Sensor / analysis defaults | HEADER `0x038`, `0x03C`, `0x04A`, `0x050`, `0x093`, `0x0B4` | V4 as shipped by V3.4: `ctrl_reg5 = 0x50`, `aa_bw_hz = 400`, `trusted_band_hz = 400`, `vdd_policy_rev = 3`, **but `0x038 = 120000` / `0x03C = 4`** — a ±6 g configuration described with a ±4 g scale, so **any raw V3.4 file is mis-scaled by 1.5× in `accel_ng`** and must be re-scaled by `180000/120000` before its amplitudes are quoted. **Rev 3.5: `0x038 = 180000`, `0x03C = 6` (FLAW-1) and `0x093 = 4` (slow-median-only VDD policy, WARN-2)**. `fs_g = 4` with `ctrl_reg5 = 0x50` is now a **validator rejection** (A.6 #11), not a legal file |
| 9 | New fields, all from reserved runs | HEADER `0x0B6…0x0C9`, MAN `0x1B8…0x1BF`, TRAILER `0x066…0x06C` | WARN-6/-10/-11 + cut §3; **no existing field shifts by one byte** |
| 10 | Two spec-arithmetic typos corrected | MAN reserved run `72 → 68` B, TRAILER pad `400 → 404` B | Both V3.3 reserved runs were listed overlapping the trailing CRC field; sizes fixed, CRC offsets unchanged |
| 11 | Loss counters stay u32 but are **saturated, never wrapped** | MAN `0x048/0x04C`, TRAILER `0x028/0x02C` | Documented correction (not an audit item): `0xFFFFFFFF / 680 = 6 312 160` ring events ≈ **30.3 days** of continuous card outage would otherwise wrap `samples_lost_ring` and make a loss total read as *smaller*. Unreachable in a 1–2 h campaign, but a counter must never lie: firmware clamps, and the validator treats `0xFFFFFFFF` as "saturated → unknown", not as a value |

### A.0.1 V4 field additions made by Rev 3.5 (no geometry change, version stays 4)

Rev 3.5 adds **five** fields (`spike_cnt` and `producer_liveness_miss_cnt` are each stored in both MAN
and TRAILER). All come out of existing reserved runs; **no offset moves, no struct grows,
`format_version` / `man_version` / `trailer_version` remain 4**, and a v4 reader is unchanged. A v3 reader
is unaffected. `hdr_crc32` / `man_crc32` / `trailer_crc32` still cover bytes 0…507 of their own structs,
so new fields are automatically CRC-covered.

| Artefact | Offset | Size | Type | Field | Meaning / who writes it |
|----------|--------|------|------|-------|-------------------------|
| HEADER | `0x0CA` | 1 | u8 | **`alias_scan_done`** | `1` = the mandatory dual-rate alias scan of App. F.3 was completed for this campaign before the campaign part was created; `0` = not done. Written in PREPARE from the BOOT record. **A `0` here makes K12 unprovable — the validator reports it** (WARN-8) |
| HEADER | `0x0CB` | 1 | — | reserved (0) | alignment pad so the u16 below sits on an even address |
| HEADER | `0x0CC` | 2 | u16 | **`alias_scan_odr_hz`** | the ODR of the **second** rate used by the scan (`800` for the `800/200` configuration); `0` = not done |
| HEADER | `0x0CE` | 2 | u16 | **`alias_scan_aa_bw_hz`** | the AA bandwidth paired with it (`200`); the validator checks `alias_scan_aa_bw_hz × 4 == alias_scan_odr_hz` **per record** (decision **D-3**, A.6 #11) |
| HEADER | `0x0D0` | 300 | — | reserved (0) | was 306 B at `0x0CA…0x1FB`; shrinks by 6 B. Still runs to `0x1FB` |
| MAN | `0x1C0` | 2 | u16 | **`spike_cnt`** | total triples that failed the §3.4 slew gate in this part (saturating, log-only, WARN-9) |
| MAN | `0x1C2` | 2 | u16 | **`producer_liveness_miss_cnt`** | times the §3.2.7 poll had to invoke the drain entry itself (FLAW-6) |
| MAN | `0x1C4` | 56 | — | reserved (0) | was 60 B at `0x1C0…0x1FB`; runs to `0x1FB`; `man_crc32` at `0x1FC` unchanged |
| TRAILER | `0x06D` | 2 | u16 | **`spike_cnt`** | part total, mirrored from MAN (data, not an acceptance gate — WARN-4) |
| TRAILER | `0x06F` | 2 | u16 | **`producer_liveness_miss_cnt`** | part total, mirrored from MAN |
| TRAILER | `0x071` | 395 | — | reserved (0) | was 399 B at `0x06D…0x1FB`; runs to `0x1FB`; `trailer_crc32` at `0x1FC` unchanged |

**Why there is no spike byte in a block header (decision D-5):** the 16 B block header is fully occupied
(`4096 = 16 + 680 × 6`, A.3). A per-block spike count would require either a 17 B header (breaks the
geometry every 425.0 ms figure depends on) or 679-sample blocks (ripples through the entire document). The
host already parses every payload, so the per-block table is **derived**, not stored; only the part-level
aggregate is stored.

**Rejected as unnecessary (recorded, not deleted):** a `range_selection_reason` field (the HEADER already
carries `ctrl_reg5`, `fs_g`, `ng_per_lsb`, and `max_abs_raw_max` at `0x0C8` carries the evidence that
produced the choice); a per-record ODR field (decision D-3 keeps one rate per part); an
`odr_cycle_stability_ppm` field (M0 bench record is the right home — §7.4).

### A.1 HEADER — 512 B struct at file offset `0x000`, then the anchor log (`0x200…0xDFF`) and zero pad

| Offset | Size | Type | Field | Value / meaning |
|--------|------|------|-------|-----------------|
| 0x000 | 4 | char[4] | `magic` | `"V3LG"` |
| 0x004 | 2 | u16 | `format_version` | **4** |
| 0x006 | 2 | u16 | `hdr_size` | 512 |
| 0x008 | 4 | u32 | `session_id` | 0…65535 |
| 0x00C | 2 | u16 | `part_index` | 0…255 |
| 0x00E | 1 | u8 | `time_base` | 1 = SENSOR_ODR |
| 0x00F | 1 | u8 | `clock_valid` | 0/1 |
| 0x010 | 4 | u32 | `sample_rate_hz` | 1600 |
| 0x014 | 2 | u16 | `sample_format` | 1 = INT16_LE_XYZ |
| 0x016 | 2 | u16 | `bytes_per_sample` | 6 |
| 0x018 | 4 | u32 | `block_size` | 4096 |
| 0x01C | 4 | u32 | `samples_per_block` | 680 |
| 0x020 | 4 | u32 | `max_data_blocks` | 32768 |
| 0x024 | 4 | u32 | `prealloc_bytes` | 134225920 |
| 0x028 | 8 | u64 | `start_utc_ms` | this part's start (0 if clock invalid) |
| 0x030 | 8 | u64 | `session_start_utc_ms` | same across parts |
| 0x038 | 4 | u32 | `ng_per_lsb` | **180000** — **±6 g** (V3.3: 60000 at ±2 g; **V3.4 shipped `120000` together with `ctrl_reg5 = 0x50`, i.e. a ±6 g part described as ±4 g → every `accel_ng` in a V3.4 file is 1.5× low, FLAW-1**). `accel_ng = raw_i16 × ng_per_lsb`. **Derived from the register, never asserted from a table: `ng_per_lsb = fs_g × 30000` and `fs_g = 2 << ((ctrl_reg5 >> 3) & 7)`** (A.6 #11) |
| 0x03C | 2 | u16 | `fs_g` | **6** (±6 g default). Must satisfy `fs_g == 2 << ((ctrl_reg5 >> 3) & 7)` — `0x50 → 010 → 6` (FLAW-1) |
| 0x03E | 1 | u8 | `axis_order` | 1 = XYZ |
| 0x03F | 1 | u8 | `endianness` | 1 = LE |
| 0x040 | 8 | char[8] | `sensor_name` | `"LIS3DSH\0"` |
| 0x048 | 1 | u8 | `ctrl_reg3` | 0x68 |
| 0x049 | 1 | u8 | `ctrl_reg4` | 0x9F |
| 0x04A | 1 | u8 | `ctrl_reg5` | **0x50** = **BW = 01 → 400 Hz** + **FSCALE = 010 → ±6 g, 0.18 mg/LSB, clip ±5.90 g**. **The V3.4 label "±4 g" for this same byte was a decode error (FLAW-1): `(0x50 >> 3) & 7 = 010`, and `010` is ±6 g** ([DS:571-573, 588-589]). `0x48` would be ±4 g (not used); `0x40` = ±2 g / BW 400 (de-escalation only, §3.1/§3.3); `0x58` = ±8 g (escalation). **Any value with `BW = 00` (800 Hz — 0x00, 0x10, 0x20, 0x30) is illegal at ODR 1600; the validator rejects it** (FLAW-3) |
| 0x04B | 1 | u8 | `ctrl_reg6` | 0x74 |
| 0x04C | 1 | u8 | `fifo_ctrl` | 0x48 |
| 0x04D | 1 | u8 | `who_am_i` | readback, 0x3F |
| 0x04E | 2 | u16 | `odr_reg_hz` | 1600 |
| 0x050 | 2 | u16 | `aa_bw_hz` | **400** (V3.3: 800) |
| 0x052 | 1 | u8 | `fifo_watermark` | 8 |
| 0x053 | 1 | u8 | `spi_mode` | 3 |
| 0x054 | 4 | u32 | `spi_hz` | 5250000 (BR=16); 2625000 if the §3.2.4 fallback is taken |
| 0x058 | 2 | i16 | `selftest_x_mg` | measured delta (range-independent) |
| 0x05A | 2 | i16 | `selftest_y_mg` | measured delta |
| 0x05C | 2 | i16 | `selftest_z_mg` | measured delta |
| 0x05E | 1 | u8 | `selftest_pass` | 0/1 (±40 % of typicals — project band) |
| 0x05F | 1 | u8 | `selftest_samples` | 32 |
| 0x060 | 2 | i16 | `user_off_x_lsb` | 0 unless characterized |
| 0x062 | 2 | i16 | `user_off_y_lsb` | 0 |
| 0x064 | 2 | i16 | `user_off_z_lsb` | 0 |
| 0x066 | 1 | u8 | `off_lsb_characterized` | 0/1 |
| 0x067 | 1 | u8 | `readout_mode` | **1 = per-sample B (default)**, 0 = burst A |
| 0x068 | 8 | — | reserved | zero |
| 0x070 | 4 | u32 | `fw_version` | `0x00MMmmpp` |
| 0x074 | 2 | u16 | `hw_version` | board revision |
| 0x076 | 2 | u16 | `board_id` | 1 = WeAct F405RG |
| 0x078 | 16 | u8[16] | `card_cid` | full 128-bit CID |
| 0x088 | 4 | u32 | `cluster_size_bytes` | 32768 |
| 0x08C | 4 | u32 | `volume_serial` | FAT BPB serial |
| 0x090 | 2 | u16 | `vrefint_cal_raw` | as read (0 if absent) |
| 0x092 | 1 | u8 | `vrefint_cal_valid` | 0/1 |
| 0x093 | 1 | u8 | `vdd_policy_rev` | **4** = 3-median debounce on the **1 s slow median only**, **no fast droop path in the write loop**, documented 3275 mV encoding cap (§6.2, V3.5 WARN-2). `3` = the V3.4 fast+slow policy; `≤ 2` is rejected as file defect |
| 0x094 | 2 | u16 | `vdd_warn_mv` | 3220 |
| 0x096 | 2 | u16 | `vdd_stop_mv` | 3120 |
| 0x098 | 2 | u16 | `pvd_mv` | 2900 |
| 0x09A | 1 | u8 | `reset_cause_last` | RCC_CSR decoded |
| 0x09B | 1 | u8 | `prev_part_present` | 0/1 |
| 0x09C | 2 | u16 | `prev_part_index` | 0xFFFF if none |
| 0x09E | 13 | char[13] | `prev_filename` | `"V3SSSSPP.BIN"` + NUL, or empty |
| 0x0AB | 1 | u8 | `hdr_flags` | bit0 recovered_unclean, bit1 selftest_skipped |
| 0x0AC | 4 | u32 | `prepare_duration_ms` | K10 audit |
| 0x0B0 | 4 | u32 | `prealloc_mode` | 0 = `f_expand` contiguous, 1 = `f_lseek` fallback |
| **0x0B4** | 2 | u16 | **`trusted_band_hz`** | **400** (App. F.2; V3.3: 640). Per V3.5 the band is region **(i)** of a three-region taxonomy: 400–800 Hz remains **in-Nyquist and locatable** (amplitude-invalid), and images from `f_true > 800 Hz` can fall **inside** this band (FLAW-5) |
| **0x0B6** | 2 | i16 | **`odr_abs_ppm_ref`** | **V3.4 / WARN-6, V3.5 sizing:** absolute ODR correction measured at M0 against **1PPS or a 1 Hz counter gate over a 10 s window (≈ 10 ppm)** — a synthesised shaker tone is an alternative, not the primary; `0x7FFF` = not measured |
| **0x0B8** | 1 | u8 | **`rotation_enabled`** | mirror of `V3_ENABLE_ROTATION` (cut §3) |
| **0x0B9** | 1 | u8 | **`card_cd_fitted`** | 0 = PA8 not wired / unknown (software detect only), 1 = PA8 wired (WARN-11) |
| **0x0BA** | 1 | u8 | **`aa_rule`** | **1 = `fc = fs/4`**; 0 = legacy `fs/2` (v3 files only). Validator cross-checks `aa_bw_hz × 4 == sample_rate_hz` when this is 1 |
| **0x0BB** | 1 | u8 | **`vdd_debounce_n`** | **3** (WARN-10); 1 = V3.3 single-median behaviour |
| **0x0BC** | 4 | u32 | **`index_field_capacity`** | **33554431** = 2²⁵−1: the block index word is 25 bits wide (A.3 note 2) |
| **0x0C0** | 4 | u32 | **`cs_high_floor_ns`** | measured [DS F8] minimum CS-high time (0 = not measured) — the number that settles K8 (WARN-5) |
| **0x0C4** | 4 | u32 | **`cpu_measured_pct_x100`** | measured producer load ×100 (e.g. 550 = 5.50 %), 0 = not measured |
| **0x0C8** | 1 | u8 | **`max_abs_raw_max`** | highest block `max_abs_raw` seen in the part (0…127), written at finalise. **V3.5 (FLAW-3): this is also the quantity the 60 s BOOT pre-test measures — before `PREPARE`, on samples that are not logged — to select the full scale (§3.1). In a campaign part it is the confirmation that the chosen range was adequate: `127` here means this part clipped and the next one must escalate to `0x58`** |
| **0x0C9** | 1 | u8 | **`anchor_sectors_written`** | 0…7, written at finalise |
| **0x0CA** | 1 | u8 | **`alias_scan_done`** | **V3.5 (WARN-8, D-3)**: 1 = the mandatory 2-minute dual-rate scan (App. F.3) completed for this campaign **before** this part was created; 0 = not done → K12 unprovable, validator reports it |
| **0x0CB** | 1 | u8 | reserved | 0 — pad so `0x0CC` is even |
| **0x0CC** | 2 | u16 | **`alias_scan_odr_hz`** | the **second** rate used by the scan (`800`); 0 = not done |
| **0x0CE** | 2 | u16 | **`alias_scan_aa_bw_hz`** | the AA bandwidth paired with it (`200`); validator checks `alias_scan_aa_bw_hz × 4 == alias_scan_odr_hz` **per record** (D-3, A.6 #11) |
| 0x0D0 | 300 | — | reserved | zero. Was `0x0CA…0x1FB` (306 B); now `0x0D0…0x1FB` (300 B). **End offset unchanged** |
| **0x1FC** | 4 | u32 | `hdr_crc32` | CRC-32 over bytes 0…507 |

### A.2 ANCHOR LOG — 3 sectors × 512 B at file offsets `0x200…0xDFF`, **append-once**

Each sector uses the layout below and is written **exactly once**, when its 31st anchor is accepted
(plus one partial flush at `FLUSHING`). **A written sector is never rewritten** (V3.4, WARN-1); the
V3.3 mechanism — 31 in-place rewrites of the same sector, all inside the file's first erase group —
is deleted. Per-sector geometry is byte-identical to V3.3; only `sector_index` and `part_index` take
previously reserved slots.

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0x000 | 2 | u16 | `count` (0…31; 31 unless this is the final flush) |
| **0x002** | 2 | u16 | **`sector_index`** (0, 1, 2, …) — replaces V3.3's implicit "which 31-entry window am I reading" inference |
| 0x004 | 4 | u32 | `first_commit_seq` (full u32) |
| 0x008 | 496 | entry[31] | anchors, 16 B each (below) |
| **0x1F8** | 2 | u16 | **`part_index`** this sector belongs to |
| 0x1FA | 6 | — | reserved (0) |
| 0x1FC | 4 | u32 | `sector_crc32` over bytes 0…507 |

**Anchor entry (16 B) — X3, all indices part-relative:**

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0x00 | 8 | u64 | `rtc_ms` (producer-latched, filtered) |
| 0x08 | 4 | u32 | `part_captured_index` — **C domain** (§1.3.7) |
| 0x0C | 2 | u16 | `commit_seq_lo16` |
| 0x0E | 2 | u16 | `part_index` |

**Capacity and extension.** 3 × 31 = **93 anchors = 10 118 s = 2.81 h** at the 108.8 s cadence; a 2 h
campaign produces ~66. If a part ever runs past 93, the log **continues append-once** into the zero
pad (`0xE00…0xFFF`) up to the 217 entries V3.3 allotted — a full 3.868 h part needs 129, so no
cadence change is ever required. Only **accepted** anchors are written (§7.1). Recovery rule: *last
sector with a valid CRC + the MAN rolling-8 window* — no torn-append reconstruction dance, because an
append either lands in a never-written sector or does not happen.

### A.3 BLOCK — 4096 B, first at file offset `0x1000`

Header (16 B — **fully occupied, there is no reserved byte in the block header**), then 4080 B
payload = 680 × `{int16 X, int16 Y, int16 Z}` LE. Payload starts at `0x10`.

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 2 | u16 | `block_seq` | 0…32767, per part. Storage domain: `S_k = block_seq × 680` (§1.3.7) |
| 0x02 | 4 | u32 | `first_sample_index` | **bits 24:0 = C**, the captured index within the part (§1.3.7); **bits 31:25 = the 7 block flags** (table below). **This is *not* `block_seq × 680`** — that V3.3 identity is withdrawn (FLAW-1) |
| 0x06 | 4 | u32 | `crc32_payload` | CRC-32 over the 4080 B payload (consumer-owned) |
| 0x0A | 2 | u16 | `lost_or_valid` | Shared field disambiguated by the flags — **interpretation is normative, table below**. Ring loss saturates at 65535 (`LOSS_SAT`) |
| 0x0C | 1 | u8 | `vdd_x10mv` | `vdd_mv = 2000 + 10 × value`; **clamped, 127 = "≥ 3275 mV"** (encoding cap, §6.2); 0 = not measured. **V3.5 (WARN-2): the value is the latest 1 s slow median at the moment this block is written — the one-block lag rule is deleted together with the fast droop path** |
| 0x0D | 1 | i8 | `temp_rel_i8` | `OUT_T`, relative, 1 LSB/°C |
| 0x0E | 1 | u8 | `max_abs_raw` | **V3.4 — replaces the binary `CLIP` flag** (FLAW-4): `max\|raw\| >> 8` over the block, so code *N* means `max\|raw\| ∈ [256N, 256N+255]`. Legal **0…127**; ≥ 128 is a validator rejection. **Clip *screening* ⇔ code = 127** (32512…32767); the authoritative clip test stays `\|raw\| == 32767` on the payload, which is cheap because the code says which blocks are worth scanning. **V3.5 (FLAW-1): the code is range-independent, its physical meaning is not — at the ±6 g default, code 127 ⇔ 5.85…5.90 g and code 85 ⇔ ≈ 3.9 g, the point V3.4 wrongly believed was full scale (at ±4 g code 127 would be 3.90…3.93 g).** **V3.5 (FLAW-3): screening feeds the clipping **duty-cycle** test of App. F.1 — `> 5 %` of a window's samples clipped ⇒ that window is excluded from **peak-location results**, never notch-filtered — because at a held resonance clipping is continuous waveform flattening, not "a handful of transient samples"** |
| 0x0F | 1 | u8 | `hdr_crc8` | CRC-8/ATM over bytes **0…14** — unchanged, so it still covers the packed index word and therefore the flags |

**Block flags — now bits 31:25 of `first_sample_index` instead of a byte:**

| Bit | Name | Was (V3.3) | Meaning |
|-----|------|-----------|---------|
| 25 | OVERFLOW | BIT0 | ring drop immediately before this block (§3.4 I3) — **C jumps by more than 680** |
| 26 | CLOCK_INVALID | BIT1 | wall clock not trustworthy; relative-frequency work still valid |
| 27 | VDD_LOW | BIT2 | rail below the warning threshold in this block's window |
| 28 | SENSOR_OVR | BIT3 | **proven** sensor-side loss — `C` does *not* jump |
| — | ~~CLIP~~ | ~~BIT4~~ | **deleted**: derived from `max_abs_raw` (this is what frees the byte at `0x0E`) |
| 29 | PARTIAL | BIT5 | last block of a part, zero-padded; `lost_or_valid` = valid sample count |
| 30 | LOSS_SAT | BIT6 | `lost_or_valid` saturated (65535) — exact magnitude unknown, location still bounded |
| 31 | FIFO_FULL | BIT7 | advisory: FIFO reached fullness before this block; **loss not proven, no discontinuity implied** |

**Normative interpretation of `lost_or_valid` (v4):**

| Flags | `lost_or_valid` means | C-domain consequence |
|-------|----------------------|----------------------|
| PARTIAL set | valid samples in this block, 0…680 | this block's `C` is the captured index of its first valid sample; contributes **no** C step |
| `OVERFLOW` set, PARTIAL clear | **ring-domain** samples dropped immediately before this block (680 per blocked publish cycle; saturating) | `C_{k+1} = C_k + 680 + L_{k+1}` — a declared ring jump is **legal**, and it *is* the gap's location |
| `SENSOR_OVR` set, `OVERFLOW`/PARTIAL clear | **sensor-side** proven deficit before this block | `C_{k+1} = C_k + 680`; the loss is declared, **not** reflected in C (those samples were never captured) |
| none set | **must be 0** | `C_{k+1} = C_k + 680` |
| **`OVERFLOW` ∧ `SENSOR_OVR` both set** (**V3.5, FLAW-2 — the fifth row**) | **no legal interpretation exists.** The producer is forbidden to create this state (§3.4 **I6**): ring-domain loss is stamped in this block and the sensor deficit is **carried to the next block published with `OVERFLOW` clear** | **file defect, not a gap** — the validator rejects the block (§A.6 #3). Two loss domains cannot share one `lost_or_valid`: one u16 cannot mean "captured then discarded" and "never captured" at once, and the two reconciliation sums of A.6 (#3 and #4) become mutually unsatisfiable if it tries |

**Note 1 — why the index is the C domain (FLAW-1).** `block_seq` advances only for blocks that were
*written*. A ring overflow discards a block whose samples had already been captured, so the next
published block's captured index is `block_seq × 680 + Σ dropped`. Storing `block_seq × 680` is a
tautology: it validates nothing, leaves the host able to locate a gap only by summing
`lost_or_valid` — a capability V3.3 never made normative — and makes "we know when it happened" true
by accident. Storing C is what §1.3.1 / A.3 of V3.3 literally said, and what RESCUE (§4.3 step 5) and
A.6 rule 3 are now written against, so a declared 400 ms GC stall near the end of a run no longer
costs the last 40 minutes of the file.

**Note 2 — the 25-bit field and its guard.** A part holds at most 32768 × 680 = 22 282 240 captured
samples; the field holds 33 554 431 (2²⁵ − 1) → **1.5× headroom**. C can only exceed that if the card
stays dead while the producer keeps dropping blocks (≈ 11.3 M discarded samples ≈ 2 h of continuous
outage). Guard: **when `C + 680 > 33 554 431` the app closes the part** (`FLUSHING`,
`stop_reason = 7 index_overflow`) instead of publishing a wrapped index, and sets
`index_guard_tripped` in MAN and TRAILER. The validator rejects any block whose bits 24:0 exceed
`index_field_capacity`. Rejected alternatives: a 17 B header (breaks `4096 = 16 + 680 × 6`) and a
22 B / 679-sample block (ripples through every 425.0 ms figure in this document).

**Note 3 — no spike byte in this header (decision D-5, WARN-9).** V3.5's slew/spike gate (§3.4) is
counted in MAN `0x1C0` and TRAILER `0x06D`, **not** per block: `4096 = 16 + 680 × 6` leaves the 16 B
header fully occupied, and the alternatives (17 B header, or 679-sample blocks) are the same two
rejected in Note 2. The host already parses every payload, so the per-block spike table is **derived
from the payload** rather than stored — strictly more information than a single byte would have carried.
The audit's "one byte/block alongside `max_abs_raw`" is therefore implemented as "one aggregate per part,
full table on the host", and the deviation is recorded here rather than silently absorbed.

### A.4 MANIFEST `V3LOG.MAN` — 1024 B, two 512 B slots at `0x000` / `0x200`

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0x000 | 4 | char[4] | `magic` = `"V3MN"` |
| 0x004 | 2 | u16 | `man_version` = **4** |
| 0x006 | 2 | u16 | `slot_size` = 512 |
| 0x008 | 4 | u32 | `commit_seq` (strictly increasing) |
| 0x00C | 4 | u32 | `session_id` |
| 0x010 | 2 | u16 | `part_index` |
| 0x012 | 13 | char[13] | `active_filename` + NUL |
| 0x01F | 1 | u8 | `state_snapshot` (state enum) |
| 0x020 | 16 | u8[16] | `card_cid` |
| 0x030 | 4 | u32 | `byte_size` — **declared durable extent** (multiple of 4096, includes header). **Never a truncation target**: the physical file size stays `prealloc_bytes` (WARN-2) |
| 0x034 | 2 | u16 | `last_block_seq` |
| 0x036 | 2 | u16 | `anchor_reject_cnt` (torn reads + plausibility rejects) |
| 0x038 | 8 | u64 | `session_captured_samples` |
| 0x040 | 8 | u64 | `part_captured_samples` = final **C** of the part |
| 0x048 | 4 | u32 | `samples_lost_sensor` (proven deficits only; **saturated at `0xFFFFFFFF`**, A.0 rule 11) |
| 0x04C | 4 | u32 | `samples_lost_ring` (**saturated**; + 680 per blocked publish cycle; host cross-check `= 680 × ring_ovf_cnt ± 680`) |
| 0x050 | 8 | u64 | `start_utc_ms` (part) |
| 0x058 | 8 | u64 | `session_start_utc_ms` |
| 0x060 | 8 | u64 | `anchor_rtc_ms` (latest accepted) |
| 0x068 | 4 | u32 | `anchor_part_captured_index` (**C domain**) |
| 0x06C | 2 | u16 | `anchor_part_index` |
| 0x06E | 2 | u16 | reserved (0) |
| 0x070 | 8 | u64 | `first_anchor_rtc_ms` |
| 0x078 | 4 | u32 | `first_anchor_part_captured_index` (**C domain**) |
| 0x07C | 2 | u16 | `first_anchor_part_index` |
| 0x07E | 2 | u16 | reserved (0) |
| 0x080 | 2 | u16 | `vdd_min_mv` (unlagged session minimum) |
| 0x082 | 2 | u16 | `vdd_last_mv` |
| 0x084 | 2 | u16 | `sd_err_cnt` |
| 0x086 | 2 | u16 | `fifo_ovr_cnt` (**proven** overrun events) |
| 0x088 | 2 | u16 | `reinit_cnt` |
| 0x08A | 2 | u16 | `stall_max_ms` |
| 0x08C | 2 | u16 | `ring_ovf_cnt` |
| 0x08E | 2 | u16 | `rescue_cnt` |
| 0x090 | 1 | u8 | `clean_stop_flag` |
| 0x091 | 1 | u8 | `recovered_unclean` |
| 0x092 | 1 | u8 | `clock_valid` |
| 0x093 | 1 | u8 | `prealloc_mode` |
| 0x094 | 4 | u32 | `next_session_id` — **`V3_ENABLE_ROTATION=1` only**, else 0 (offset unchanged from V3.3) |
| 0x098 | 2 | u16 | `next_part_index` (same condition) |
| 0x09A | 13 | char[13] | `next_filename` + NUL (same condition) |
| 0x0A7 | 1 | u8 | `intent_valid` (same condition; the RESCUE firmware resolution is **deleted** — §4.3) |
| 0x0A8 | 4 | i32 | `odr_ppm_est` (running estimate) |
| 0x0AC | 2 | u16 | `spi_err_cnt` |
| 0x0AE | 2 | u16 | `fifo_full_cnt` (advisory fullness events) |
| 0x0B0 | 128 | journal[16] | reset journal, 8 B each: `{cause u8, state u8, err u16, counter u32}` |
| 0x130 | 128 | anchor[8] | rolling window, 16 B each (A.2 entry format, part-relative, **C domain**) |
| 0x1B0 | 4 | u32 | `odr_unc_ppm` (current uncertainty feeding the deficit band) |
| 0x1B4 | 4 | u32 | `anchor_span_ms` (first→latest accepted anchor) |
| **0x1B8** | 1 | u8 | **`anchor_sectors_written`** (0…7) — how much append-once log is valid |
| **0x1B9** | 1 | u8 | **`card_cd_state`** — 0 = not fitted/unknown, 1 = present, 2 = absent (WARN-11). **V3.5 (WARN-3): written as `0` in M0/M1/M2 — no CD input is implemented; the field stays so the layout is frozen** |
| **0x1BA** | 2 | u16 | **`cd_change_cnt`** — 1 Hz card-detect transitions; the F19 shake signature. **V3.5 (WARN-3): not incremented in M0/M2 (no 1 Hz PA8 sample); reserved-zero** |
| **0x1BC** | 1 | u8 | **`rotation_enabled`** (mirror of the compile gate) |
| **0x1BD** | 1 | u8 | **`index_guard_tripped`** (A.3 note 2 guard fired; part closed on capacity) |
| **0x1BE** | 2 | u16 | **`declared_gap_cnt`** — blocks carrying a declared discontinuity (cheap K12 evidence) |
| 0x1C0 | 2 | u16 | **`spike_cnt`** (V3.5, WARN-9) | triples failing the §3.4 slew gate in this part (saturating, **log-only**, per-block table derived host-side — D-5) |
| 0x1C2 | 2 | u16 | **`producer_liveness_miss_cnt`** (V3.5, FLAW-6) | times the §3.2.7 poll had to invoke the drain entry itself; `> 0` must always be attributable via F.6 §2 |
| 0x1C4 | 56 | — | reserved (0) | runs `0x1C4…0x1FB`. **V3.5: was `0x1C0…0x1FB` = 60 B, now 56 B; `man_crc32` offset unchanged** (V3.3's "72 B at `0x1B8`" overlapped `man_crc32` — already corrected in V3.4) |
| **0x1FC** | 4 | u32 | `man_crc32` over bytes 0…507 |

### A.5 TRAILER — 512 B struct + pad, at `0x1000 + N × 4096`

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0x000 | 4 | char[4] | `magic` = `"V3TL"` |
| 0x004 | 2 | u16 | `trailer_version` = **4** (V3.3 said 2 while the global rule said 3 — closed) |
| 0x006 | 2 | u16 | `trailer_size` = 512 |
| 0x008 | 4 | u32 | `session_id` |
| 0x00C | 2 | u16 | `part_index` |
| 0x00E | 2 | u16 | `stop_reason` — 1 KEY, 2 VDD, 3 rotation, 4 clean-stop error, 5 volume, **6 part full with rotation gated off, 7 C-index capacity guard (A.3 note 2)** |
| 0x010 | 4 | u32 | `final_block_count` |
| 0x014 | 4 | u32 | `final_file_size` — **declared extent**; physical size is always `prealloc_bytes` (WARN-2) |
| 0x018 | 8 | u64 | `part_captured_samples` (final C) |
| 0x020 | 8 | u64 | `session_captured_samples` |
| 0x028 | 4 | u32 | `samples_lost_sensor` (saturating) |
| 0x02C | 4 | u32 | `samples_lost_ring` (saturating) |
| 0x030 | 4 | u32 | `running_crc32` (all payloads of this part) |
| 0x034 | 4 | i32 | `odr_ppm_final` (fitted slope over §7.2 segments — sensor-side gaps only) |
| 0x038 | 8 | u64 | `stop_utc_ms` |
| 0x040 | 8 | u64 | `first_anchor_rtc_ms` |
| 0x048 | 8 | u64 | `last_anchor_rtc_ms` |
| 0x050 | 2 | u16 | `vdd_min_mv` |
| 0x052 | 2 | u16 | `stall_max_ms` |
| 0x054 | 2 | u16 | `fifo_ovr_cnt` |
| 0x056 | 2 | u16 | `ring_ovf_cnt` |
| 0x058 | 13 | char[13] | `next_filename` + NUL (if rotated) |
| 0x065 | 1 | u8 | `fifo_full_cnt_lo` (advisory, low byte; full value in MAN) |
| **0x066** | 1 | u8 | **`max_abs_raw_max`** — highest block code in the part (0…127) |
| **0x067** | 1 | u8 | **`anchor_sectors_written`** |
| **0x068** | 1 | u8 | **`rotation_enabled`** |
| **0x069** | 1 | u8 | **`final_block_flags`** — the 7 flag bits of the last published block (fast RESCUE/host sanity check) |
| **0x06A** | 2 | u16 | **`cd_change_cnt`** (WARN-11) |
| **0x06C** | 1 | u8 | **`index_guard_tripped`** |
| 0x06D | 2 | u16 | **`spike_cnt`** (V3.5, WARN-9) | part total, mirrored from MAN — data, not an acceptance gate (WARN-4) |
| 0x06F | 2 | u16 | **`producer_liveness_miss_cnt`** (V3.5, FLAW-6) | part total, mirrored from MAN |
| 0x071 | 395 | — | reserved (0) — runs to `0x1FB`; **V3.5: was 399 B from `0x06D`; `trailer_crc32` offset unchanged. V3.3's "400 B at `0x068`" left `0x1F8…0x1FB` undefined and was corrected in V3.4** |
| **0x1FC** | 4 | u32 | `trailer_crc32` over bytes 0…507 |

### A.6 Host validator invariants (format level)

0. **Version gate:** `format_version`, `man_version`, `trailer_version` must all read **4** and
    agree. Block-header meaning changed in two places (the flags byte became `max_abs_raw`, and bits
    31:25 of the index word became the flags), so a v3 file parsed as v4 would read `flags` as a clip
    code and a v4 file parsed as v3 would lose all flags — both silently. The reference validator
    keeps a separate v3 reader.
1. `hdr_crc32`, `man_crc32`, `trailer_crc32`, every `sector_crc32`, every `hdr_crc8`,
   every `crc32_payload` must verify.
2. **Container (S domain):** `block_seq` strictly +1 ⇒ `S_{k+1} = S_k + 680`, every block at
   `0x1000 + block_seq × 4096`; exception: the final PARTIAL block.
3. **Time (C domain) — replaces V3.3's `first_sample_index == block_seq × 680`:**
   - **clean** (no gap flags, `lost_or_valid = 0`): `C_{k+1} = C_k + 680`;
   - **`OVERFLOW` set:** `C_{k+1} = C_k + 680 + lost_or_valid(k+1)` — **a declared ring jump is legal,
     not a scan failure**; it *is* the gap location (FLAW-1);
   - **`SENSOR_OVR` set, `OVERFLOW` clear:** `C_{k+1} = C_k + 680` **and** `lost_or_valid(k+1) > 0`
     (sensor-side loss moves the counter, never C);
   - **`LOSS_SAT` set:** only `C_{k+1} ≥ C_k + 680` is required and the magnitude is reported as
     "≥ saturated bound" — reported, never silently accepted;
   - **PARTIAL:** contributes no C step; `lost_or_valid` ≤ 680 is its sample count;
   - bits 24:0 must be `< index_field_capacity` (A.3 note 2);
   - **`OVERFLOW` ∧ `SENSOR_OVR` both set in one block → reject the block as file defect** (**V3.5,
     FLAW-2 / A.3 row 5 / §3.4 I6**). The producer cannot generate this state, so a file that contains it
     is corrupt — the validator treats it as **corruption, not as a gap**, and reports it as a defect in
     the *writer* (a build that predates I6, or a torn 8 B index word), never as declared loss. A scan
     that instead tried to interpret such a block would add never-captured samples into **C** and carry
     the error to the end of the part (≈ 735 ppm in a 174 080-sample anchor segment for 4 such blocks);
   - **`C ≥ S` for every block**, with the difference equal to `Σ lost_or_valid` over
     `OVERFLOW`-flagged blocks up to it. **`C < S` is a hard error** — an S written into a C field,
     i.e. exactly the V3.3 confusion, not a warning.
4. **Declared discontinuity** = `OVERFLOW`, or `SENSOR_OVR`, or (`lost_or_valid > 0` **with** a gap
   flag set). **`FIFO_FULL` alone is never a discontinuity** and must not be reported as loss.
5. **Two reconciliations, never mixed (FLAW-2 of V3.3 audit — the C/S domains):**
   - **Time base (C):** `Σ Δcaptured ≈ Δt × Fs_fit` per fit segment, within `fifo_ovr_cnt × 32`,
     **with ring losses excluded from the left-hand side** — `Δcaptured` already contains them;
   - **Container (S):** `Σ samples_in_blocks = part_captured_samples − samples_lost_ring`,
     `Σ lost_or_valid(OVERFLOW blocks) = samples_lost_ring` (saturation-corrected),
     `Σ lost_or_valid(SENSOR_OVR blocks) = samples_lost_sensor` within `fifo_ovr_cnt × 32`, and
     `samples_lost_ring = 680 × ring_ovf_cnt ± 680`. **Both sums are only meaningful because of I6: a
     block that carried both flags would be counted twice and make the two equations unsatisfiable.**
   A file that passes the C rule and fails the S rule has a **counter** fault, not a time-base fault,
   and must be reported as such — the inverse of V3.3's single invariant, which reported a valid file
   as corrupt after any ring jump (a 6 s GC stall: 15 dropped blocks = 10 200 samples of phantom
   "corruption").
   **V3.5 (WARN-4) scope note:** the *cross-artefact* re-derivations — MAN totals vs the anchor log vs
   the HEADER extended counters (`0x0C0…0x1FB`) vs TRAILER totals — are **host `logcore` regression
   tests**, not per-file acceptance criteria and not M2 cut-phase assertions. Acceptance is rule 1 +
   rule 2 + rule 3 + one valid manifest slot + the declared extent (A.5 step 5). A cross-artefact
   disagreement is reported as a **diagnostic**, never as corruption.
6. Anchor entries must carry a matching `part_index` — **which for M0–M2 files is the constant `0`
   (WARN-1); the multi-part identity ladder is tested on the host, not here**.
   **Residuals are expected at ≈ 5–8 ms** (drain latency, WARN-7), **not** the 3.906 ms SSR quantum — a
   residual median *below* ~3.9 ms across many anchors is itself suspicious and usually means the fit
   was taken against the wrong index domain. **±5000 ppm guard per fit segment** — a sanity gate that
   **rejects** the segment — and, **V3.5 (WARN-6), the achieved per-segment residual must be printed in
   the report**: no peak derived from a segment may carry F.7's 0.005 Hz peak-location uncertainty unless
   that segment's residual is quoted alongside it. Segments break on **sensor-side** gaps only
   (`SENSOR_OVR` / sensor-side `lost_or_valid`), **never** on `OVERFLOW` or `FIFO_FULL` (§7.2).
7. A pre-allocated tail beyond the **declared extent** is expected after an unclean cut and is **not**
   corruption. Because nothing truncates, the physical size is always `prealloc_bytes`; a smaller
   physical size means something truncated the file **outside** this specification (WARN-2) and is an
   error.
8. `anchor_reject_cnt > 0` is normal and healthy (the filter is working); a *rising* rate is a
   diagnostic, not a data fault.
9. `max_abs_raw ∈ 0…127`; ≥ 128 → reject. `max_abs_raw == 127` triggers **clip screening**; the
   authoritative clip test is `\|raw\| == 32767` in the payload (App. F.1). **V3.5: the screening result
   is converted into a clipping *duty cycle* per analysis window and compared against the 5 % budget
   (F.1), and its physical meaning is read from `ng_per_lsb` — 5.85…5.90 g at the ±6 g default, not the
   3.90…3.93 g V3.4 believed (FLAW-1).** `max_abs_raw_max` (`0x0C8`) is the part-level summary and the
   input of the BOOT range pre-test (§3.1).
10. **Gap-location report (K12 evidence).** For every declared discontinuity the validator reports its
    wall-clock position and uncertainty, from `C_block = S_block + Σ lost_or_valid(≤ block)`
    (OVERFLOW-flagged blocks only) mapped through the segment fit:
    **± (425 ms block granularity ⊕ ≈ 5–8 ms anchor error)**. A campaign report that cannot place every
    declared gap has failed K12 whatever the counters say.
11. **Configuration cross-checks, not format rules — everything derived from the register, nothing from a
    constant (V3.5, FLAW-1 + WARN-8 + decision D-3).** For **each container record (part) independently**,
    from that record's own HEADER:

    | Check | Rule | Failure means |
    |-------|------|---------------|
    | AA code | `BW bits of ctrl_reg5 = ((ctrl_reg5 >> 6) & 3) == 01` → 400 Hz; `00` (800 Hz) → **reject** | the knee sat at Nyquist (V3.3 FLAW-3) |
    | AA ↔ rate | **`aa_bw_hz × 4 == sample_rate_hz`** | V3.4 stated this **globally**; per decision **D-3** it is now **per record**, because a dual-rate campaign stores two different legal pairs in two different files (`1600/400` and `800/200`) and a global rule would reject the second one |
    | Trusted band | `trusted_band_hz ≤ aa_bw_hz` and `trusted_band_hz ≤ sample_rate_hz / 4` | an analysis claim outside the legal band (FLAW-5) |
    | **Scale, derived** | **`fs_g == 2 << ((ctrl_reg5 >> 3) & 7)`** and **`ng_per_lsb == fs_g × 30000`** | V3.4's rule was `fs_g × 30000 == ng_per_lsb` with `fs_g` read out of a table — internally consistent, **and consistent with a wrong range**: `fs_g = 4` / `120000` passed it while the sensor ran at ±6 g (FLAW-1). Deriving `fs_g` from the byte makes a mis-declared range a **validation failure**, not a footnote. Reference values: `0x40→2/60000`, **`0x48→4/120000`**, **`0x50→6/180000` (default)**, `0x58→8/240000` |
    | Clip arithmetic | `clip_g == 32767 × ng_per_lsb / 10⁹` → **±5.90 g** at the default | a clip policy quoted against the wrong full scale (F.1) |
    | **Alias scan (V3.5, WARN-8)** | `alias_scan_done == 1`, `alias_scan_odr_hz ∈ {800, 400}`, `alias_scan_aa_bw_hz × 4 == alias_scan_odr_hz`, and `alias_scan_odr_hz < sample_rate_hz` | the campaign was **not** pre-scanned: K12's alias clause is unprovable, and any peak that is actually an image of `f_true > 800 Hz` (FLAW-5) may have been reported as a mode |
    | Time base | `odr_abs_ppm_ref != 0x7FFF` when the report quotes an absolute `Fs` (measured by **1PPS / 1 Hz counter gate**, WARN-6) | an absolute-frequency claim with no measurement behind it |

    Inconsistency here is what lets a phantom 600 Hz peak — or a 155 Hz image of a 1755 Hz bracket mode —
    into a modal report, so it is a **validation failure**, not a warning.
12. **V3.5 (FLAW-2) flag-consistency check:** no block may carry `OVERFLOW` ∧ `SENSOR_OVR` together
    (A.3 row 5, §3.4 I6). This is listed separately from rule 3 because the *reporting* differs: rule 3 is
    about index arithmetic, this one is a **producer-contract violation** and must be reported as a fault
    of the writer (firmware or a torn 8 B index word), never as a gap.

---

## Appendix G — Capacity, identity and endurance-run rules (moved from §4.5)

Kept verbatim in substance, out of the active-path text (document rule: no section is deleted; audit
cut §3 moved these endurance-run tables to an appendix because a **1–2 h campaign writes ~69 MB of a
134.2 MB part** and none of the rules below ever fire in it).

**Capacity (80 % rule, container rate 9638 B/s):**

| Volume | Usable (80 %) | Recording time | Parts |
|--------|---------------|----------------|-------|
| 8 GiB | 6.87 GB | **198 h ≈ 8.3 days** | ~50 |
| 32 GiB | 27.5 GB | **792 h ≈ 33 days** | ~200 |
| one part | 134.2 MB | **3.868 h** | 1 |

**Session / part identity** (**V3.5, WARN-1: the ladder below is a host `logcore` regression suite for
M0–M2, not firmware behaviour — `part_index` is written as the constant `0`, the `part full` exit does not
exist, and a part can never fill at 69.4 MB of 134.2 MB**):
- `session_id` is constant across parts of a session; `V3SSSSPP.BIN` carries it in hex.
- Next id = `MAX(manifest.session_id, max_dir_scan_id) + 1` masked to 16 bits, opened with
  **`FA_CREATE_NEW`**, incrementing on `FR_EXIST`. **Backup SRAM is never the authority** (there is no
  VBAT cell, §7.3). Kept in firmware: the *scan* is cheap and it prevents colliding with an existing file.
- `part_index > 255` → close the session and start a new `session_id`. **Inert for M0–M2** (`part_index`
  is constant 0): tested on the host only, so the code path is not deleted but it is never exercised.
- **256 consecutive `FR_EXIST`** → refuse to start, LED 10. Same status: implemented, host-verified,
  unreachable in a one-part campaign.
- Deletion is **host-side only**; the logger never deletes and never renames (App. B, App. D).

**Two V3.4 consequences, stated where they can be found:**
1. The directory scan used for **id allocation** stays in firmware — it is not the recovery sweep that
   was moved to the host (§4.3). Both touch the directory; only one of them was deleted.
2. Because RESCUE no longer sweeps the directory, an **orphan** `V3??????.BIN` (from a cut with the
   rotation gate ON) occupies 128 MiB until the host cleans it, and the id allocation above will
   correctly step past it. With `V3_ENABLE_ROTATION=0` no orphans can be produced at all, which is the
   normal state for M2/M3 and for every campaign.

**Endurance-run rules kept for the gate-ON / product-qualification build only:** the 12 h autonomy
run, the 8 h K1 and 24 h K4 soaks, the 200-point power-cut fuzz and the 21 targeted rotation cuts are
**not** part of the V3.4 schedule (§10.2, App. B.2) but remain valid acceptance criteria when M4 is
executed as a product-qualification milestone post-campaign.

---
───────────────────────────────────────────────────────────────────────────────
END OF FROZEN SPECIFICATION — Docs/01-CONTAINER-FORMAT.md
Format version 4 · Frozen 2026-11-18 · Content hash 346f632cad95576e96cb201355363ba1d2c7874e0974797982b08fb7226c6e9a
Any discrepancy between this file and a generated validator is a validator bug,
never a specification bug. This file is the source of truth.
───────────────────────────────────────────────────────────────────────────────
