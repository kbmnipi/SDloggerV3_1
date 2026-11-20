/**
 ******************************************************************************
 * @file    main.c
 * @brief   M0 bring-up: 84 MHz clock tree + 1 Hz heartbeat on PB2 + the LIS3DSH
 *          WHO_AM_I presence gate over SPI1 (§3.1 step 2).
 *
 * WHAT THIS FILE IS.
 *   The first executable artefact of the SD Logger V3 firmware. Its only job
 *   is to prove, on the bench, the clock gate of Docs/02-FIRMWARE-DESIGN.md
 *   §2.2 before any card, ring or filesystem code exists:
 *     HSE 8 MHz -> PLL(M=8, N=336, P=4) -> SYSCLK 84 MHz, Q=7 -> PLL48CK 48 MHz
 *     AHB 84 · APB1 42 · APB2 84 · Flash 2 WS · regulator Scale 1
 *   The 1 Hz blink is the measurable side-effect: HAL_Delay() is derived from
 *   SysTick, whose reload is recomputed from the ACTUAL AHB frequency inside
 *   HAL_RCC_ClockConfig(), so a wrong clock changes the blink rate.
 *
 * WHAT IT DELIBERATELY DOES NOT DO.
 *   TIM2 (§2.2: 1 MHz free-run + 1 kHz supervisor tick, src/bsp_tim2.c) and the
 *   IWDG (§5.1: 8 s nominal / 5.45 s floor, fed only from the 1 Hz
 *   proven-progress tick, src/bsp_iwdg.c) exist as of this step, and the reset
 *   cause is captured before anything can clear it (§5.1 "a reset is never
 *   silent") and announced as §8's LED code 8.
 *   No SDIO, no FatFs, no ring buffer, no RTC, no ADC1, no KEY
 *   (§10.2 order). No RTOS, no malloc/new, no dynamic allocation (§2.3 rule 4).
 *   SPI1 exists, but only as the §3.1 step 2 presence gate in drv_lis3dsh.c:
 *   polled PIO, no DMA armed, and ODR never started, so the sensor stays in
 *   power-down and cannot emit samples or INT1 edges. Nothing here touches the
 *   ISR hot path.
 *
 * EXPECTED OBSERVATION ON THE BENCH.
 *   PB2 (status LED, active-HIGH, §2.1) toggles every 500 ms -> 1 Hz blink.
 *   LED permanently ON = a bring-up gate failed: clock tree, SPI1 init, the SPI
 *   transaction itself, or no 0x3F in three WHO_AM_I attempts (app_error_halt).
 *   N fast blinks right after reset, then a steady 1 Hz heartbeat = the gate
 *   passed, and N (1..3) is the attempt on which WHO_AM_I first read 0x3F: the
 *   gate is a 3-attempt OR (see the block in main and LIS3DSH_GATE_ATTEMPTS).
 *   Two blinks therefore mean "the part only answers from the second frame".
 *   8 fast blinks BEFORE those N happen only when the previous session ended in
 *   an IWDG or BOR reset (§5.1's last row, §8 code 8): after a diagnosed halt
 *   the sequence is "8, then 1, then heartbeat", and 8 is never a gate result
 *   because the gate cannot report more than LIS3DSH_GATE_ATTEMPTS (3).
 *   The §2.1/§2.2 verification gate itself is closed with a scope on the bus
 *   pins (or MCO), not by eyeballing the LED.
 ******************************************************************************
 */

#include "stm32f4xx_hal.h" /* framework = stm32cube; the builder defines USE_HAL_DRIVER */
#include "drv_lis3dsh.h"   /* §3.1 step 2 presence gate: SPI1 + WHO_AM_I (src/drv_lis3dsh.c) */
#include "bsp_tim2.h"      /* §2.2 supervisor time base: 1 MHz free-run + 1 kHz tick (src/bsp_tim2.c) */
#include "bsp_iwdg.h"      /* §5.1 IWDG 8 s + reset cause; §8 LED 8 (src/bsp_iwdg.c) */

/* ========================================================================== */
/* §2.2 PLL constants — transcribed from the frozen clock tree, not chosen.   */
/*   HSE 8 MHz -> /M=8 -> 1.000 MHz reference -> xN=336 -> VCO 336 MHz        */
/*   VCO /P=4 -> SYSCLK 84 MHz       VCO /Q=7 -> SDIOCLK/PLL48CK 48 MHz       */
/* ========================================================================== */
#define PLL_M   8U    /* §2.2  HSE 8 MHz / 8 = 1.000 MHz phase-detector reference */
#define PLL_N   336U  /* §2.2  VCO = 1 MHz x 336 = 336 MHz (RM0090 valid 100..432 MHz) */
#define PLL_P   4U    /* §2.2  SYSCLK = 336 / 4 = 84 MHz (constant; never re-tuned) */
#define PLL_Q   7U    /* §2.2  PLL48CK = 336 / 7 = 48 MHz (SDIO kernel clock; later SDIO_CK = 48/(CLKDIV 0 + 2) = 24 MHz) */

/* --------------------------------------------------------------------------
 * Build-time guard: the -DHSE_VALUE=8000000 flag in platformio.ini is not
 * optional. The vendored HAL defaults to 25000000U
 * (stm32f4xx_hal_conf_template.h:94, behind "#if !defined(HSE_VALUE)"), which
 * with these same PLL constants would silently produce a 262.5 MHz SYSCLK —
 * out of spec and out of Flash's rated range. Fail the compile instead of
 * shipping a wrong clock.
 * -------------------------------------------------------------------------- */
#if !defined(HSE_VALUE)
#error "HSE_VALUE is not defined: add -DHSE_VALUE=8000000 to build_flags (platformio.ini)."
#endif

#if (HSE_VALUE != 8000000UL)
#error "HSE_VALUE is not 8000000 Hz. The board carries an 8 MHz crystal (Docs/02-FIRMWARE-DESIGN.md 2.1/2.2): the -DHSE_VALUE=8000000 flag from platformio.ini is not reaching the compiler."
#endif

#if (((HSE_VALUE / PLL_M) * PLL_N) / PLL_P) != 84000000UL
#error "PLL M/N/P no longer yields SYSCLK 84 MHz. Section 2.2 is frozen - do not retune it."
#endif

#if (((HSE_VALUE / PLL_M) * PLL_N) / PLL_Q) != 48000000UL
#error "PLL Q no longer yields PLL48CK 48 MHz. SDIOCLK 48 MHz is mandatory (section 2.2)."
#endif

/* --------------------------------------------------------------------------
 * Status LED (§2.1): PB2, push-pull, active-HIGH, 2 MHz drive.
 * PB2 is also BOOT1. With BOOT0=0 the boot path is flash and BOOT1 is
 * don't-care, but §2.1 requires confirming the resistor network on the bench.
 * -------------------------------------------------------------------------- */
#define STATUS_LED_PORT           GPIOB
#define STATUS_LED_PIN            GPIO_PIN_2       /* PB2 — §2.1 */

/* Heartbeat half-period: 500 ms ON + 500 ms OFF = 1 Hz (§10.2 M0 "LED codes
 * readable"). HAL_Delay is legal ONLY in this thread: SysTick is the lowest
 * priority exception, so HAL_Delay stays correct here (§2.3 rule 3). TIM2 now
 * exists at preempt 7 (§3.2.5) and can preempt both this loop and the SysTick
 * tick — but its ISR calls no HAL and reads no tick state (src/bsp_tim2.c), so
 * uwTick still advances and HAL_Delay's wait is only lengthened by ≤ 0.5 µs. */
#define HEARTBEAT_HALF_PERIOD_MS  500U

/* --------------------------------------------------------------------------
 * §8 LED code 8: "previous session ended by IWDG/BOR reset". The count IS the
 * code — it is the strategy's number, not a choice — and reusing
 * app_blink_pass() keeps the rhythm identical to the presence gate's announce,
 * so the two are told apart by count alone: the gate can never emit more than
 * LIS3DSH_GATE_ATTEMPTS (3), and 8 has no other meaning while the state machine
 * of step 17 does not exist yet.
 * TODO (step 17): replace with the §8 pattern proper — N short pulses every 4 s
 * at ≤ 0.5 % duty — once the state machine owns LED timing.
 * -------------------------------------------------------------------------- */
#define RESET_CAUSE_BLINK_COUNT   8U

/* --------------------------------------------------------------------------
 * §3.1 step 2 presence gate: attempt budget and bench encoding.
 *
 * LIS3DSH_GATE_ATTEMPTS — WHO_AM_I is read up to this many times and the gate
 * PASSES IF ANY ATTEMPT READS 0x3F (logical OR, not AND). Why that is a
 * legitimate gate and not a weakened one is argued where it is used, in main().
 * LIS3DSH_GATE_GAP_MS — separation between attempts. 10 ms is the datasheet's
 * conservative floor for the "any event" waits ([DS §9/§11: after STRT/BOOT
 * >= 10 ms]) and is free in BOOT, so the gap is never the variable under test;
 * at 5.25 MHz one frame is 3.05 us, i.e. the gap is ~3000x the frame.
 * GATE_XFER_FAIL_MARKER — what s_lis3dsh_log[] holds for an attempt where the
 * transfer itself failed. Chosen as a value that is not an answer, so "no byte"
 * can never be mistaken for "a byte that was not 0x3F" when the log is read.
 * -------------------------------------------------------------------------- */
#define LIS3DSH_GATE_ATTEMPTS     3U
#define LIS3DSH_GATE_GAP_MS       10U
#define GATE_XFER_FAIL_MARKER     0xEEU

/* Bench encoding of a PASS: N fast blinks, where N = the number of the attempt
 * on which 0x3F first appeared. 80 ms is ~6x shorter than the 500 ms heartbeat
 * half-period, so the announce cannot be confused with the heartbeat that
 * follows. A FAIL is LED solid ON via app_error_halt(): no blinks at all.
 * Re-watch the announce by pressing RESET — it runs once per boot. */
#define SENSOR_OK_BLINK_HALF_MS   80U
#define SENSOR_OK_PAUSE_MS        1000U

/* Per-attempt evidence, the actual payload of this experiment and the reason it
   is worth more than a pass/fail LED: one byte per attempt, so a run that passes
   on the second try is distinguishable from one that passes on the first.
   Read over SWD; volatile so -O2 keeps every store. 0xEE = transfer failed. */
static volatile uint8_t s_lis3dsh_log[LIS3DSH_GATE_ATTEMPTS];

/* Last byte the bus produced, readable over SWD (there is no UART yet, so this
   is the cheapest evidence available, and volatile is what stops -O2 from
   removing it). 0x3F = present · 0xFF = nothing drove MISO · 0x00 = held low. */
static volatile uint8_t s_lis3dsh_id;

static void SystemClock_Config(void);
static void bsp_status_led_init(void);
static void app_blink_pass(uint32_t times);
static void app_error_halt(void);

/**
 * @brief  Bring SYSCLK to 84 MHz from the 8 MHz HSE crystal through the main PLL.
 * @param  none
 * @return none — on any failure calls app_error_halt(); never continues with a
 *         partial clock (§2.3 "no silent failures").
 * @note   Side effects: enables the PWR clock, writes PWR_CR.VOS = Scale 1,
 *         starts HSE and the PLL, programs Flash ACR latency to 2 WS, switches
 *         SYSCLK to PLLCLK, sets the AHB/APB1/APB2 dividers and (inside
 *         HAL_RCC_ClockConfig) re-runs HAL_InitTick() so HAL_Delay becomes
 *         84 MHz-correct — verified in the vendored HAL at
 *         Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c:722.
 * @note   §2.2 forbids any later PLL re-tuning at runtime: reconfiguring P would
 *         stop PLLQ (SDIOCLK) and force Flash WS + APB changes + HAL_InitTick
 *         re-init while a card is mounted. This function runs exactly once.
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};   /* static storage only — no heap (§2.3 rule 4) */
    RCC_ClkInitTypeDef clk = {0};

    /* PWR peripheral clock, required before touching regulator scaling.
       HAL_MspInit() is left weak/empty in this project, so this function owns
       the enable (CubeMX does the same on F4). */
    __HAL_RCC_PWR_CLK_ENABLE();

    /* Regulator voltage Scale 1 — the scaling rated for HCLK up to 168 MHz, so
       the constant 84 MHz of §2.2 sits inside it.
       HAL F4 1.28.3 has neither HAL_PWREx_ConfigSupply() nor PWR_FLAG_VOSRDY
       (checked in the vendored headers), so the documented macro is used:
       it writes PWR_CR.VOS = 01 (Scale 1). */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* --- Oscillator + PLL stage (§2.2) --- */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;   /* only HSE feeds the clock tree */
    osc.HSEState       = RCC_HSE_ON;               /* 8 MHz crystal, NOT bypass: HSEBYP stays 0.
                                                      HAL F4's RCC_OscInitTypeDef has no HSE
                                                      pre-divider field (unlike F1/L1), so the
                                                      PLL sees the raw 8 MHz and divides by M=8. */
    osc.PLL.PLLState   = RCC_PLL_ON;               /* main PLL on, and in use as SYSCLK source */
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;        /* §2.2: PLL fed from HSE, never from HSI */
    osc.PLL.PLLM       = PLL_M;                    /* 8   -> 1.000 MHz reference (§2.2) */
    osc.PLL.PLLN       = PLL_N;                    /* 336 -> VCO 336 MHz (§2.2) */
    osc.PLL.PLLP       = RCC_PLLP_DIV4;            /* 4   -> SYSCLK 84 MHz (§2.2) */
    osc.PLL.PLLQ       = PLL_Q;                    /* 7   -> PLL48CK/SDIOCLK 48 MHz (§2.2) */

    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        /* HSE failed to lock, or the PLL refused to lock (bad M/N/P/Q, or a
           conflicting previous configuration). Not recoverable at step 2. */
        app_error_halt();
    }

    /* --- Bus divider stage (§2.2: AHB 84 · APB1 42 · APB2 84) --- */
    clk.ClockType      = RCC_CLOCKTYPE_HCLK  | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;  /* SYSCLK = PLL, not HSI/HSE (§2.2) */
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;          /* AHB  = 84 MHz -> Cortex-M4F, DMA2, SDIO kernel (§2.2) */
    clk.APB1CLKDivider = RCC_HCLK_DIV2;            /* APB1 = 42 MHz; TIM2 sits on APB1, so its kernel is 42x2 = 84 MHz (§2.2) */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;            /* APB2 = 84 MHz -> SPI1 at 84/16 = 5.25 MHz (§2.1) */

    /* Flash wait states at 84 MHz = 2 (§2.2 "FLASH 2 WS").
       HAL F4 1.28.3 spells this FLASH_LATENCY_2 == FLASH_ACR_LATENCY_2WS
       (Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_flash_ex.h:276);
       "FLASH_LATENCY_2WS" is not a valid HAL name on F4. */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK)
    {
        app_error_halt();
    }
}

/**
 * @brief  Configure PB2 as the active-HIGH status output (§2.1).
 * @param  none
 * @return none.
 * @note   Runs BEFORE SystemClock_Config() in main() on purpose: if the clock
 *         bring-up fails, app_error_halt() needs a working output to report it.
 */
static void bsp_status_led_init(void)
{
    GPIO_InitTypeDef io = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();  /* GPIOB peripheral clock — the LED is on PB2 (§2.1) */

    io.Pin   = STATUS_LED_PIN;        /* PB2 (§2.1) */
    io.Mode  = GPIO_MODE_OUTPUT_PP;   /* push-pull (§2.1) */
    io.Pull  = GPIO_NOPULL;           /* driven output; the external circuit sets the level */
    io.Speed = GPIO_SPEED_FREQ_LOW;   /* 2 MHz (§2.1 "push-pull, 2 MHz") — the lowest slew
                                         that still drives the LED cleanly, which keeps edge
                                         EMI down on a rig carrying flying sensor leads (§2.1) */
    HAL_GPIO_Init(STATUS_LED_PORT, &io);

    /* Deterministic start state: LED OFF, so the first 500 ms half-cycle turns it
       ON and the blink phase is repeatable from every reset (§10.2 M0). */
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  Announce "presence gate PASSED": N fast blinks, then a pause.
 * @param  times  Blinks to emit = the number of the attempt on which WHO_AM_I
 *                first read 0x3F (1..LIS3DSH_GATE_ATTEMPTS). Never 0: the
 *                no-match path halts in app_error_halt() before this is reached.
 * @return none. Side effects: drives PB2, consumes (times x 160 ms) + 1000 ms of
 *         HAL_Delay time in thread context.
 * @note   WHY A DISTINCT PATTERN: §10.2 makes M0's exit "LED codes readable". With
 *         no UART yet, the blink is the only channel, so "sensor found" must not
 *         look like the 1 Hz heartbeat. Explicit SET/RESET instead of TogglePin
 *         keeps the pattern phase-deterministic whatever state the previous
 *         function left the pin in.
 * @note   HAL_Delay stays in thread context only (§2.3 rule 3): SysTick is the
 *         lowest-priority exception and no producer exists yet.
 */
static void app_blink_pass(uint32_t times)
{
    for (uint32_t n = 0U; n < times; n++)
    {
        HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_SET);
        HAL_Delay(SENSOR_OK_BLINK_HALF_MS);      /* 80 ms ON  */
        HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_RESET);
        HAL_Delay(SENSOR_OK_BLINK_HALF_MS);      /* 80 ms OFF */
    }

    /* Separator gap: guarantees the last fast blink cannot merge into the first
       half-cycle of the heartbeat that follows. */
    HAL_Delay(SENSOR_OK_PAUSE_MS);
}

/**
 * @brief  Defensive halt for a failed bring-up gate. No silent failures (§2.3).
 * @param  none
 * @return none — never returns.
 * @note   WHY THIS EXISTS: if HSE never locks or the PLL never locks, every
 *         later timing claim (this 500 ms blink, the 1 kHz supervisor tick,
 *         1600 Hz capture, the 27.2 s MAN cadence) is meaningless. Continuing
 *         would produce a plausible-looking log whose sample intervals are
 *         silently wrong — exactly the failure the lossless contract exists
 *         to prevent (Docs/00-ARCHITECTURE.md).
 * @note   BENCH SIGNATURE: LED stuck ON instead of blinking at 1 Hz. This is a
 *         bring-up signal, not a §5 LED code — TODO (verify against bench):
 *         replace with the §5 error code + journal at step 17.
 */
static void app_error_halt(void)
{
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_SET);  /* PB2 solid ON = a bring-up gate failed (clock tree, SPI1 init/transfer, or WHO_AM_I) */
    __disable_irq();
    for (;;)
    {
        /* IWDG-ARMED CONSEQUENCE (step 5; the halt code below it is deliberately
           UNCHANGED — nothing here feeds the watchdog, and that is the point).
           §5.1's feed is "proven progress", and a diagnosed bring-up gate failure
           has none to prove: this loop spins forever without feeding, so the halt
           now ENDS IN AN IWDG RESET in ≤ 5.45 s (8.00 s nominal). Masking
           interrupts cannot postpone it — the IWDG counts from its own LSI and
           PRIMASK is not in its path, which is what makes this halt survivable
           on a deployed logger with no debugger attached. What the bench sees:
           LED solid ON for one window, then the next boot announcing §8's code 8
           (bsp_reset_cause_init() latched BSP_RST_CAUSE_IWDG before anything
           could clear it) and the 1 Hz heartbeat — i.e. the failure is reported
           instead of swallowing the board, per §5.1 "a reset is never silent".
           Inspecting the chip DURING the halt uses connect-under-reset via NRST
           (App. H "Bench debug topology" in Docs/90-DECISIONS-LOG.md), which is
           why NRST is wired at all. TODO (step 17): replace the solid-ON signal
           with the §5/§8 error code and the Backup-SRAM journal entry.
           One property of this loop is load-bearing and is restated here so that a later
           edit cannot lose the reason for the shape it has:
           Deliberately not HAL_Delay-based — SysTick cannot be trusted here. */
    }
}

/**
 * @brief  SysTick exception handler — the HAL time base.
 * @param  none
 * @return none. Executes in Handler mode once per millisecond (1 kHz, §2.2 SysTick).
 * @note   WHY THIS EXISTS: Cube HAL F4 does not ship a SysTick_Handler. HAL_Init()
 *         -> HAL_InitTick() arms SysTick with TICKINT, and without this strong
 *         definition the SysTick vector resolves to the startup file's weak
 *         Default_Handler (an infinite `B .`), so uwTick never advances and
 *         HAL_Delay() hangs on its first call. Measured on the bench before this
 *         fix: ICSR.VECTACTIVE=15 (SysTick), xPSR=0x6100000F, pc=0x08001030,
 *         uwTick 0 -> 0 across a 1 s run window while SysTick VAL kept counting.
 * @note   Global symbol ON PURPOSE — not static, and no prototype/extern added
 *         here: a strong definition in this translation unit is what overrides the
 *         weak startup_stm32f405xx entry, which is what makes the .isr_vector
 *         SysTick slot point at real code instead of Default_Handler.
 * @note   §2.3: SysTick is the lowest-priority exception, so this body is safe
 *         under every producer the project will add later (TIM2 / SPI1 / SDIO /
 *         DMA2 all sit above it). It does nothing but advance the time base —
 *         no logging, no SDIO/FatFs, no SPI1/FIFO work (§2.3 hot-path rules).
 */
void SysTick_Handler(void)
{
    HAL_IncTick();   /* uwTick++ — the only timing source HAL_Delay relies on */
}

/**
 * @brief  Entry point: reset cause -> HAL -> LED -> 84 MHz -> TIM2 -> IWDG ->
 *         BOOT announcements -> 1 Hz heartbeat fed at 1 Hz.
 * @return never returns.
 * @note   The super-loop is the single consumer context of the whole firmware
 *         (§2.3 "one super-loop consumer plus event-driven producers"). At step
 *         2 there is no producer yet, so the loop holds only two things: the
 *         heartbeat and the §5.1 watchdog feed. Both belong to this context by
 *         rule and not by convenience — §5.1: "Who feeds | consumer only
 *         (super-loop) | feeding from an ISR turns the watchdog into
 *         decoration" — and the only progress signal in existence is TIM2's
 *         1 Hz supervisor flag.
 */
int main(void)
{
    /* §5.1 "a reset is never silent" — FIRST statement of the program, ahead of
       HAL_Init() and of every clock write. RCC_CSR's reset flags are sticky:
       only a POR/PIN reset or a software write of RMVF clears them, and HAL's
       clock-unwind paths are allowed to do the latter, so the capture has to
       happen before anything that could. Reads RCC->CSR and nothing else. */
    bsp_reset_cause_init();

    /* HAL_Init(): SysTick at 1 ms on the post-reset clock (HSI 16 MHz), NVIC
       priority grouping, and the weak/empty HAL_MspInit() hook. */
    HAL_Init();

    /* LED first, clock second — see the note on bsp_status_led_init(). */
    bsp_status_led_init();

    /* §2.2: SYSCLK 84 MHz, PLL48CK 48 MHz, AHB 84 / APB1 42 / APB2 84, 2 WS. */
    SystemClock_Config();

    /* ======================================================================
     * §2.2 supervisor, then §5.1 watchdog — the order is a dependency, not a
     * preference. §5.1's feed condition is "a live 1 Hz state tick", and that
     * tick is produced by bsp_tim2_init(); arming the IWDG first would leave
     * the boot path running with a watchdog armed and no progress signal with
     * which to satisfy it. Both are BOOT-only configurations and neither is
     * repeated: TIM2's placement rule is "configuration only in BOOT"
     * (Docs/00-ARCHITECTURE.md:186), and an IWDG cannot be stopped once started.
     * ==================================================================== */
    bsp_tim2_init();       /* 1 MHz free-run, 1 kHz tick, 10 ms + 1 Hz flags */
    bsp_iwdg_init();       /* window starts counting the instant this returns */

    /* ----------------------------------------------------------------------
     * §8 code 8: "previous session ended by IWDG/BOR reset" — §5.1's last row
     * ("a reset is never silent; LED 8 on the next boot"), announced once per
     * boot BEFORE the presence gate. Absorbed by the state machine at step 17,
     * which will own all LED timing; here it is a boot-time echo.
     *
     * WHY IT CANNOT BE CONFUSED WITH THE GATE'S ANNOUNCE: both use
     * app_blink_pass()'s 80 ms rhythm, and the count is the whole message — the
     * gate emits 1..LIS3DSH_GATE_ATTEMPTS (3) at most, so 8 pulses has exactly
     * one reading in §8's list.
     *
     * THE BOOT BUDGET IT HAS TO FIT (§5.1's 5.45 s fast-LSI floor, since
     * HAL_IWDG_Init() above already reloaded the counter with 1000):
     *   this announce, 8 x (80+80) + 1000 pause ......... 2280 ms
     *   presence gate, 3 x 10 ms timeout + 2 x 10 ms gap    50 ms
     *   gate pass blink at N = 3: 3 x 160 + 1000          1480 ms
     *   first super-loop iteration: feeds immediately — the 1 Hz flag was
     *                                 latched at t = 1000 ms and no one has
     *                                 taken it yet, so there is no wait
     *   ------------------------------------------------------------
     *   worst case ≈ 3.81 s  → ≈ 1.64 s (30 % of the window) still unspent at
     *   the 5.45 s floor, and > 4 s against the 8.00 s nominal.
     * A gate that never passes instead halts at ≈ 2.35 s without feeding and is
     * reset at 5.45…8.00 s, and the cycle repeats with this announce at its
     * head — which is the observable §5.1 loop this step exists to create.
     * TODO: verify against bench (HAL_Delay is 1 ms-tick quantised and the SPI
     * timeout is measured start-to-finish, so the real total is a measurement).
     * ---------------------------------------------------------------------- */
    if ((bsp_reset_cause_get() & (BSP_RST_CAUSE_IWDG | BSP_RST_CAUSE_BOR)) != 0U)
    {
        app_blink_pass(RESET_CAUSE_BLINK_COUNT);   /* §8 code 8, once per boot */
    }

    /* ======================================================================
     * §3.1 step 2 — LIS3DSH presence gate over SPI1 (§2.1 wiring, [DS §5.2]
     * framing, [DS §7.4] fixed ID), TOLERANT FORM: up to
     * LIS3DSH_GATE_ATTEMPTS reads of WHO_AM_I, PASS IF ANY ONE of them reads
     * 0x3F. It runs after the clock gate because SPI1's 5.25 MHz is APB2/16 and
     * the HAL transfer timeout needs the live 1 ms SysTick tick, and after the
     * LED exists because every failure below ends in app_error_halt().
     *
     * WHY OR-OF-3 IS STILL A GATE AND NOT A WEAKENING OF IT.
     *   The bench now shows all four signals present (CS asserted, 16 clocks,
     *   MOSI, MISO) and still no 0x3F on the first read, and the owner's note
     *   from an earlier project is that the part answered WHO_AM_I only from the
     *   second read. Two mechanisms produce exactly that signature, and neither
     *   of them says anything about the health of the part:
     *     1) F4 master-enable corner. HAL_SPI_TransmitReceive() sets
     *        SPI1_CR1.SPE and writes SPI1_DR in the same call, so attempt 1 is
     *        the first frame after the peripheral is armed, while attempt N>1
     *        runs with SPE already set. [DS §5.2] defines the frame boundary by
     *        the CS edges alone, so a mis-framed first byte is not a "corrupt
     *        answer" — the sensor never recognises the command and stays
     *        tri-stated, which reads as 0xFF on a pulled-up MISO.
     *     2) The interface is selected by CS itself: [DS §5] — "CS high = I2C
     *        enabled / SPI idle, CS low = SPI active" — so the first CS-low
     *        edge can be consumed by the interface-mode switch itself. The
     *        33 kΩ CS pull-up (§2.1's MANDATORY M0 item) IS now fitted, and the
     *        re-test with it in place returned the same log (0xFF, then 0x3F,
     *        s_spi_err_cnt = 0): the pull-up removes the floating-CS risk, it
     *        does not remove the throwaway first frame — finding BF-1,
     *        Docs/90-DECISIONS-LOG.md — so the 3-attempt budget below stays and
     *        is independent of the pull-up.
     *   One miss is therefore not evidence of an absent part. One 0x3F, however,
     *   IS a positive identification: no amount of retrying can invent 0x3F on a
     *   bus that is genuinely dead, because every attempt then reads the same
     *   pulled-up 0xFF or the same 0x00. The cost is ~20 ms of BOOT; the gain is
     *   separating "sensor missing" from "first frame eaten".
     *
     * WHAT MAKES THIS AN EXPERIMENT AND NOT A HOPE: s_lis3dsh_log[] keeps the
     * byte of every attempt, so the run says WHICH attempt answered. Two fast
     * blinks confirms the theory; one blink means the first read was fine after
     * all and the real fault is elsewhere; solid ON after three attempts refutes
     * it and points back at wiring, supply or mode. Trailing zero entries in the
     * log were simply never attempted (the loop broke early).
     *
     * BENCH ENCODING (there is still no UART):
     *   N fast blinks, N = 1..3 = PASS, and N is the attempt that first read
     *                             0x3F — §10.2's M0 sensor wiring check
     *   LED solid ON            = FAIL, 0x3F never appeared
     *   SWD evidence: s_lis3dsh_log[] (per-attempt byte, 0xEE = the transfer
     *   itself failed), s_lis3dsh_id (last byte seen), s_spi_err_cnt.
     * ==================================================================== */
    if (drv_lis3dsh_spi_init() != DRV_LIS3DSH_OK)
    {
        app_error_halt();   /* the peripheral itself refused: nothing else can proceed */
    }

    /* Declared here (C99): one owner, one lifetime for the readback. */
    uint8_t sensor_id = 0U;
    uint32_t attempt;
    uint32_t pass_attempt = 0U;   /* 0 = no attempt has matched yet */

    for (attempt = 1U; attempt <= LIS3DSH_GATE_ATTEMPTS; attempt++)
    {
        if (drv_lis3dsh_who_am_i(&sensor_id) == DRV_LIS3DSH_OK)
        {
            s_lis3dsh_log[attempt - 1U] = sensor_id;

            if (sensor_id == LIS3DSH_ID_WHO_AM_I)
            {
                pass_attempt = attempt;
                break;   /* one 0x3F is enough — this is deliberately an OR gate */
            }
        }
        else
        {
            /* A sentinel instead of a bus value: "no byte at all" must never be
               read back later as "a byte that happened to be wrong". */
            s_lis3dsh_log[attempt - 1U] = GATE_XFER_FAIL_MARKER;
        }

        if (attempt < LIS3DSH_GATE_ATTEMPTS)
        {
            HAL_Delay(LIS3DSH_GATE_GAP_MS);   /* thread context only (§2.3 rule 3) */
        }
    }

    s_lis3dsh_id = sensor_id;   /* last byte the bus produced (0 = never got one) */

    if (pass_attempt == 0U)
    {
        app_error_halt();   /* 0x3F never appeared in LIS3DSH_GATE_ATTEMPTS attempts */
    }

    app_blink_pass(pass_attempt);   /* N blinks = the N-th attempt answered */

    for (;;)
    {
        /* §5.1 FEED, mapped to the policy table line by line:
           "Who feeds | consumer only (super-loop)"        → this is that loop;
           "Feed condition | ... or a live 1 Hz state tick in IDLE/ERROR/L3" →
           bsp_tim2_take_1hz_flag() IS that tick (TIM2 CH1 compare, §2.2), and
           taking it is the proof of progress. The other feed conditions
           (≥1 block committed; a completed recovery/rotation/scan sub-step) do
           not exist yet — no filesystem, no ring, no ladder.
           CHECK BEFORE WORK, not after: the feed is attempted at the top of each
           iteration, so the loop can never start a 500 ms delay without having
           had its chance to feed first.
           REAL MARGIN — and it is not the 500 ms of the loop body. The feed is
           gated by a flag that is set once per 1000 ms, so the loop's ~500 ms
           period is the POLL rate, while the FEED rate is 1 Hz: consecutive
           feeds are ≈ 1000 ms apart typically, and the worst-case phase
           relationship (a take that just misses a set, then the full 500 ms
           delay, then the take that finds it) puts the bound at ≈ 1500 ms.
           Against the 5.45 s fast-LSI floor that is 3.6x, and against the 8.00 s
           nominal 5.3x — above the >2x that §5.1's own budget line demands, and
           it is the number to compare, not the loop period. Tightening it would
           mean feeding on a faster flag, which §5.1 explicitly does not want:
           progress, not "the loop is spinning". From step 17 the state machine
           owns this call site, adds block commits, and applies the ≤ 1 feed per
           500 ms long-operation rule to f_expand / SD re-init / RESCUE. */
        if (bsp_tim2_take_1hz_flag() != 0U)
        {
            bsp_iwdg_feed();   /* consumer context, proven progress (§5.1) */
        }

        HAL_GPIO_TogglePin(STATUS_LED_PORT, STATUS_LED_PIN);  /* PB2 toggle (§2.1) */
        HAL_Delay(HEARTBEAT_HALF_PERIOD_MS);                  /* 500 ms high + 500 ms low = 1 Hz */
    }
}