# 02-project-context.md — Project context and current state

## Idea
A standalone, battery-powered 3-axis vibration logger. Continuous capture from an accelerometer
to a MicroSD card. Reliable, lossless operation under normal use. Graceful behavior across power
events. The goal is reliability, not features.

## Current state: READY FOR M0 IMPLEMENTATION

Strategy: **Rev 3.5, container format v4, FROZEN.**
All audits closed. Documents split into 6 files (see `01-role.md` for the map).
Next action: satisfy repo preconditions (§10.2.1), then implement M0 BSP.

## Hardware we build on

- **MCU:** STM32F405RGT6 (WeAct core board, LQFP64). Cortex-M4F @ 84 MHz constant.
  1 MB Flash, 128 KB D1 SRAM, 64 KB CCM.
- **Storage:** MicroSD via hardware 4-bit SDIO.
  - SDIO data: PC8–PC11 (D0–D3). Clock: PC12 (CK). Command: PD2 (CMD).
  - Card Detect: PA8 (to be confirmed on bench; not used in M0–M2 state machine).
- **Sensor:** LIS3DSH accelerometer on SPI1.
  - SCK PA5, MISO PA6, MOSI PA7, CS PA4, INT1 PB5.
- **User I/O:** Button PC13 (active HIGH), LED PB2 (active HIGH).
- **Clocks:** HSE 8 MHz crystal, LSE 32.768 kHz.
- **Battery monitoring:** deferred. VDD sense via internal VREFINT only.

## Toolchain

- PlatformIO in VS Code. `framework = stm32cube`.
- Target: `platform = ststm32@<pinned version>`, `board = genericSTM32F405RG`.
- Release build: `env:release` with `-O2`. Debug build: `env:debug` with `-Og -g3`.
- Compiler: gcc-arm-none-eabi (PlatformIO-managed).
- FatFs R0.12c (vendored in `lib/`).
- STM32 HAL/LL (vendored or pinned via PlatformIO).

## Repo preconditions (must exist before M1 — §10.2.1)

These are NOT optional. If any is missing, implementation of M1/M2 acceptance criteria is blocked.

| # | Precondition | Status |
|---|---|---|
| P1 | `ff.c` (FatFs R0.12c) vendored in `lib/` with version pinned | TODO |
| P2 | HAL vendored/pinned; `platform = ststm32@x.y.z` in `platformio.ini` | TODO |
| P3 | `env:release` with `-O2` exists and builds | TODO |
| P4 | `.d1ram` linker section + `ASSERT` + `-fstack-usage` map check | TODO |
| P5 | RM0090 PDF available at bench (for SPI under-run wording, B1) | TODO |
| P6 | All `.clinerules` paths use `Docs/` (not `docs/`) | DONE |

## Key configuration constants (from strategy — do not change)

| Item | Value | Source |
|---|---|---|
| SYSCLK | 84 MHz (HSE 8 MHz, PLL M=8 N=336 P=4) | §2.2 |
| SDIOCLK | 48 MHz (PLL Q=7) | §2.2 |
| SDIO_CK | 24 MHz (CLKDIV=0, 4-bit) | §2.2 |
| SPI1 clock | 5.25 MHz (APB2 84 / BR=16), mode 3 | §2.1 |
| TIM2 | 1 MHz free-running (PSC=83), CH1 → 1 kHz tick | §2.2 |
| IWDG | /256, reload 1000 → 8.0 s nominal | §5.1 |
| RTC prescaler | PREDIV_S=127, PREDIV_A=255 → SSR 256 Hz | §7 |
| ODR | 1600 Hz, watermark WTMP=8, FIFO Stream mode | §3.1 |
| CTRL_REG5 | 0x50 (BW=400 Hz, ±6 g) | §3.1, FLAW-1 |
| Block size | 4096 B = 16 B header + 680 × 6 B payload | A.3 |
| Ring | 16 × 4096 B = 64 KB in D1, `.d1ram` section | §3.4 |
| MAN cadence | every 64 blocks = 27.2 s | §4.3 |
| Anchor cadence | every 4 MAN commits = 108.8 s | §4.3 |
| VDD policy | 3 consecutive 1 s medians; no fast path in M0–M2 | §6.2 |

## DMA2 assignment (preferred, V3.5 B1)

| Peripheral | Stream | Channel | Note |
|---|---|---|---|
| SPI1_RX | DMA2_Stream0 | CH3 | Sensor data receive |
| SPI1_TX | DMA2_Stream3 | CH3 | Dummy bytes for clock |
| SDIO | DMA2_Stream6 | CH4 | Card write DMA |

Arbitration order: S0 > S3 > S6 (capture favoured over card).

## What NOT to do

- Do not use SPI mode 0 or 1. The sensor requires mode 3 (CPOL=1, CPHA=1).
- Do not set `CTRL_REG4` before all other sensor config registers. ODR starts last.
- Do not read `FIFO_SRC` via DMA while streams are armed without quiesce (§3.2.6).
- Do not call `f_write` with more than 4096 bytes.
- Do not use `HAL_Delay` inside any ISR or callback.
- Do not enable rotation (`V3_ENABLE_ROTATION=0` for all campaign builds).