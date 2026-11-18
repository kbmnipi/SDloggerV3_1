# 03-execution-discipline.md — Coding discipline for implementation

## Scope discipline

- Do exactly what the current task asks. No more.
- If asked to "implement the SPI driver", do not also implement the SDIO driver.
- If you spot a related issue, note it in one line at the end. Do not fix it unprompted.
- No scaffolding, no "while I'm here", no unsolicited refactors.

## Micro-step execution (critical for this project)

- **One prompt = one compilable unit.** A "unit" is one `.c`/`.h` file pair, or one clearly
  bounded modification to an existing file.
- After writing code, ALWAYS state:
  1. Which file(s) were created/modified.
  2. What to verify (expected compilation result, expected hardware behaviour).
  3. What the next logical step is (but do NOT execute it).
- If a task requires multiple files, list them and ask which to do first.
- Never generate more than ~200 lines of new code in a single response unless explicitly asked.
  Break it into logical steps.

## Coding style for this project

- C99. No C++ features. No VLAs.
- All functions prefixed by module: `bsp_`, `drv_lis3dsh_`, `drv_sd_`, `log_`, `app_`.
- All file-scope variables are `static` unless explicitly shared via header.
- ISR functions are named `xxx_IRQHandler` (CMSIS convention) and contain ONLY:
  - Flag setting
  - DMA/SPI register manipulation
  - Ring buffer index updates
  - No function calls to non-ISR-safe code
- Every magic number is a `#define` with a comment citing the strategy section.
- Every register write includes a comment: `// CTRL_REG5: BW=01 (400 Hz), FSCALE=010 (±6 g)`
- Error handling: no silent failures. Every error path either logs (in debug), increments a
  counter, or triggers a state transition. Never `return` without handling.

## Verification after each step

After generating code, always include:
1. **Build check:** "This should compile with zero warnings under `-Wall -Wextra -O2`."
2. **Logic check:** Brief statement of what the code does and which strategy section it implements.
3. **Integration note:** What other modules this interacts with and any assumptions.

## When unsure

- Prefer `/* TODO: verify against bench */` over a confident guess.
- Cite the exact strategy file and section. "Not in strategy docs" is a valid answer.
- If the user rejects code, do not immediately produce a new version. Ask what was wrong.

## Stop conditions (halt and report)

Stop and ask the user if:
- The task requires hardware knowledge not in the strategy docs or datasheet.
- The task implies modifying the frozen container format (App. A).
- The task implies changing DMA stream assignments, clock tree, or NVIC priorities.
- You catch yourself repeating the same reasoning loop.
- A repo precondition (§10.2.1) is missing and the task depends on it.

## Task modes (keyword-gated)

- **IMPLEMENT** (`implement`, `write`, `create`, `напиши`): Write full C files. This is the
  default mode for the coding phase.
- **AUDIT** (`audit`, `review`, `check`, `аудит`): Review existing code against the strategy.
  Output format: `BUG-N / WARN-N / VERDICT`. No file modifications.
- **ARCHITECT** (`strategy`, `doc`, `architecture`): Markdown only. No C code. Use this if the
  strategy itself needs a question answered before coding proceeds.
- **DEBUG** (`debug`, `broken`, `fault`, `дебаг`): Diagnose a reported issue. Propose a fix
  as a diff. Do not rewrite the whole file.

If no keyword is present, default to **IMPLEMENT** and confirm scope before generating code.

## Implementation order for M0 (do not skip ahead)

The strategy defines M0 scope (§10.2). Implementation should follow this dependency order:

1. **Repo setup:** `platformio.ini` with pinned platform, `env:release`, `env:debug`, linker script.
2. **BSP clocks:** HSE → PLL → 84 MHz, verify with a blinky on PB2.
3. **BSP GPIO:** PB2 (LED), PC13 (KEY with EXTI).
4. **BSP TIM2:** 1 MHz free-running counter + 1 kHz compare interrupt.
5. **BSP IWDG:** 8 s watchdog, feed from super-loop.
6. **BSP RTC:** LSE, prescaler, double-read routine, validity gate.
7. **BSP ADC1:** VREFINT slow path (16 samples, 480-cycle, median).
8. **drv_lis3dsh:** Register init sequence (§3.1), WHO_AM_I check, self-test.
9. **drv_lis3dsh FIFO drain:** Mode B read, §3.2.6 quiesce, §3.2.1 drain decision.
10. **drv_lis3dsh liveness poll:** §3.2.7 (TIM2 flag + app-context check).
11. **Ring buffer:** 16 × 4096 B in `.d1ram`, SPSC, I1–I6 invariants.
12. **drv_sd:** SDIO init, 4-bit, HAL_SD with own timeouts.
13. **drv_sd write path:** DMA2 S6/CH4, CMD13 busy poll, 4096 B per call.
14. **diskio:** FatFs glue with D1 range guard.
15. **log_container:** Block packing, header stamping, CRC.
16. **log_manifest:** Dual-slot MAN commit, anchor log append-once.
17. **app_state_machine:** IDLE → PREPARE → RECORDING → FLUSHING, LED codes.
18. **app_rescue:** Boot recovery, forward scan, declared extent.

Each step should be independently compilable and testable before moving to the next.