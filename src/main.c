/**
 ******************************************************************************
 * @file    main.c
 * @brief   M0 bring-up, step 2: 84 MHz clock tree + 1 Hz heartbeat on PB2.
 *
 * WHAT THIS FILE IS.
 *   The first executable artefact of the SD Logger V3 firmware. Its only job
 *   is to prove, on the bench, the clock gate of Docs/02-FIRMWARE-DESIGN.md
 *   §2.2 before any sensor or card code exists:
 *     HSE 8 MHz -> PLL(M=8, N=336, P=4) -> SYSCLK 84 MHz, Q=7 -> PLL48CK 48 MHz
 *     AHB 84 · APB1 42 · APB2 84 · Flash 2 WS · regulator Scale 1
 *   The 1 Hz blink is the measurable side-effect: HAL_Delay() is derived from
 *   SysTick, whose reload is recomputed from the ACTUAL AHB frequency inside
 *   HAL_RCC_ClockConfig(), so a wrong clock changes the blink rate.
 *
 * WHAT IT DELIBERATELY DOES NOT DO.
 *   No SDIO, no SPI1, no FatFs, no ring buffer, no sensor (§10.2 order).
 *   No TIM2, no IWDG, no RTC, no ADC1. No RTOS, no malloc/new, no dynamic
 *   allocation (§2.3 rule 4). Nothing here touches the ISR hot path.
 *
 * EXPECTED OBSERVATION ON THE BENCH.
 *   PB2 (status LED, active-HIGH, §2.1) toggles every 500 ms -> 1 Hz blink.
 *   LED permanently ON = clock bring-up failed (see app_error_halt).
 *   The §2.1/§2.2 verification gate itself is closed with a scope on the bus
 *   pins (or MCO), not by eyeballing the LED.
 ******************************************************************************
 */

#include "stm32f4xx_hal.h" /* framework = stm32cube; the builder defines USE_HAL_DRIVER */

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
 * priority exception and there is no ISR here yet (§2.3 rule 3). */
#define HEARTBEAT_HALF_PERIOD_MS  500U

static void SystemClock_Config(void);
static void bsp_status_led_init(void);
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
 * @brief  Defensive halt for a failed clock bring-up. No silent failures (§2.3).
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
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_SET);  /* PB2 high = clock fault */
    __disable_irq();
    for (;;)
    {
        /* No IWDG yet (step 5): nothing can rescue this loop except a debugger.
           Deliberately not HAL_Delay-based — SysTick cannot be trusted here. */
    }
}

/**
 * @brief  Entry point: HAL init -> LED init -> 84 MHz clock -> 1 Hz heartbeat.
 * @return never returns.
 * @note   The super-loop is the single consumer context of the whole firmware
 *         (§2.3 "one super-loop consumer plus event-driven producers"). At step
 *         2 there is no producer yet, so the loop holds only the heartbeat.
 */
int main(void)
{
    /* HAL_Init(): SysTick at 1 ms on the post-reset clock (HSI 16 MHz), NVIC
       priority grouping, and the weak/empty HAL_MspInit() hook. */
    HAL_Init();

    /* LED first, clock second — see the note on bsp_status_led_init(). */
    bsp_status_led_init();

    /* §2.2: SYSCLK 84 MHz, PLL48CK 48 MHz, AHB 84 / APB1 42 / APB2 84, 2 WS. */
    SystemClock_Config();

    for (;;)
    {
        HAL_GPIO_TogglePin(STATUS_LED_PORT, STATUS_LED_PIN);  /* PB2 toggle (§2.1) */
        HAL_Delay(HEARTBEAT_HALF_PERIOD_MS);                  /* 500 ms high + 500 ms low = 1 Hz */
    }
}