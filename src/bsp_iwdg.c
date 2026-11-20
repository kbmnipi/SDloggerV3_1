/**
 ******************************************************************************
 * @file    bsp_iwdg.c
 * @brief   IWDG: §5.1's 8 s "proven progress" watchdog (prescaler /256, reload
 *          1000) and the reset-cause capture behind §5.1's "a reset is never
 *          silent" and §8's LED code 8.
 *
 * THE ARITHMETIC, AND WHICH END OF THE SPREAD IS THE "WORST CASE".
 *   §5.1 fixes the settings and quotes both numbers: 1000 x 256 / 32 kHz =
 *   8.000 s nominal, and 5.45 … 15.06 s across the LSI's 17 … 47 kHz spread. The
 *   row tells the designer to "budget the worst case 5.45 s", and the direction
 *   of that word matters: 5.45 s is the SHORTEST window the hardware can hand
 *   out, so it is the one that can cut a legitimate multi-second operation short.
 *   The long end (15.06 s) is uncomfortable in the other direction — a hung
 *   logger stays hung for a quarter of a minute before anything notices — but it
 *   cannot lose declared data, which is the only currency §5.1 is trading in.
 *   Every budget in §5 therefore has to be checked against 5.45 s, and §5.1's
 *   own line is the template: L1 (1.5 s) + clean-stop (1.0 s) = 2.5 s against
 *   5.45 s → ">2x margin". The boot path adds its own line to that list at the
 *   call site in main.c, where the blink and gate delays live.
 *
 * WHY THE WINDOW IS A CONSTANT AND NOT A MEASUREMENT.
 *   LSI is uncalibrated (17…47 kHz), so a calibration pass could in principle
 *   measure it against TIM2 and retune IWDG_RLR to a precise 8.000 s. It is not
 *   done: a timeout that firmware computes can be made effectively infinite by
 *   the same runaway that hangs the firmware, which is the exact failure §5.1
 *   exists to survive. A reload compiled into flash cannot. The spread is
 *   therefore absorbed by the budget, not measured away.
 *
 * WHY THERE IS NO LSION CODE, AND WHAT WOULD CHANGE THAT.
 *   HAL_IWDG_Init()'s first action is __HAL_IWDG_START — IWDG_KR = 0xCCCC — and
 *   the line above it says so out loud: "Enable IWDG. LSI is turned on
 *   automatically" (stm32f4xx_hal_iwdg.c:179-180). RM0090 says the same in its
 *   LSI-oscillator description: LSI starts when the IWDG is enabled by software
 *   (or by the option bytes), without LSION. §10.2.1 P5 (RM0090 available at the
 *   bench) is still open, so the sentence has to be eyeballed once before M0
 *   closes. TODO: verify against bench. If the bench shows RCC_CSR.LSIRDY never
 *   setting, or the counter not counting, with LSION = 0 — STOP and report
 *   UNCERTAIN with that citation instead of "fixing" it: adding LSION + an
 *   LSIRDY wait would paper over the discrepancy, make the watchdog depend on an
 *   oscillator the strategy says it does not own, and lose the evidence.
 *
 * WHY "WINDOW DISABLED" IS NOT A SETTING HERE.
 *   STM32F4's IWDG has no window register: this HAL's IWDG_InitTypeDef is
 *   Prescaler + Reload and nothing else (stm32f4xx_hal_iwdg.h:46-54), so §5.1's
 *   "window disabled" holds by construction. State the consequence honestly,
 *   because it moves work into firmware where one might have expected hardware:
 *   an EARLY feed cannot fault, so a loop that feeds every microsecond while
 *   making no progress is INVISIBLE to this peripheral. §5.1's "progress, not
 *   'the loop is spinning'" is enforced only by the feed condition at the call
 *   sites — which is why bsp_iwdg_feed() is a documented contract instead of a
 *   rename of HAL_IWDG_Refresh().
 ******************************************************************************
 */

#include "bsp_iwdg.h"
#include "stm32f4xx_hal.h"   /* HAL_IWDG + RCC_CSR bit definitions from CMSIS  */

/* ========================================================================== */
/* §5.1 numbers — transcribed, not chosen.                                     */
/* ========================================================================== */

/* §5.1: "Clock / prescaler / reload | LSI 32 kHz nom, /256, reload 1000 → 8.00
   s nominal | independent of HSE/PLL". Independence from HSE/PLL is why this row
   survives every clock-tree failure main.c can detect: a logger whose watchdog
   depends on the clock that just died cannot report the death. */
#define IWDG_PRESCALER        IWDG_PRESCALER_256   /* IWDG_PR.PRD = 11 → /256   */
#define IWDG_PRESCALER_DIV          256U           /* the same, as a number     */
#define IWDG_RELOAD                1000U           /* IWDG_RLR.RLD, 12 bits     */

/* The LSI figures are §5.1's row: 32 kHz nominal, 17 … 47 kHz across parts and
   temperature. They are used here only to prove the budget arithmetic below. */
#define IWDG_LSI_NOMINAL_HZ      32000UL
#define IWDG_LSI_SLOWEST_HZ      17000UL   /* → longest window, 15.06 s */
#define IWDG_LSI_FASTEST_HZ      47000UL   /* → shortest window,  5.45 s */

/* --------------------------------------------------------------------------
 * Build-time guards (same discipline as main.c's PLL guards and bsp_tim2.c):
 * §5.1 is frozen, and a wrong window is not detectable from a blink.
 * -------------------------------------------------------------------------- */
#if (IWDG_RELOAD > 0xFFFUL)
#error "IWDG_RLR.RLD is 12 bits: reload above 0x0FFF is truncated by the hardware, silently shortening the window."
#endif

/* Nominal window: 1000 x 256 = 256 000 LSI cycles; / 32 kHz = 8 s exactly. The
 * x 1000 below is only to keep millisecond resolution through integer division
 * (256 000 / 32 000 would truncate to 8 "units" with no way to prove the .000). */
#if (((IWDG_RELOAD * IWDG_PRESCALER_DIV * 1000UL) / IWDG_LSI_NOMINAL_HZ) != 8000UL)
#error "IWDG prescaler/reload no longer gives the 8.00 s nominal window of 5.1 (K7)."
#endif

/* Fast-LSI floor: 1000 x 256 / 47000 = 5446 ms. Every §5 budget is written
   against 5.45 s, so refuse to ship a design whose floor drops under 5 s — that
   is the value that can cut a legitimate operation short, not merely lengthen a
   hang. (Multiply by 1000 first so the integer division keeps millisecond
   resolution; 256 000 000 still fits in 32 bits.) */
#if ((IWDG_RELOAD * IWDG_PRESCALER_DIV * 1000UL) / IWDG_LSI_FASTEST_HZ < 5000UL)
#error "IWDG worst-case window has fallen below 5 s; every §5.1/§5.3 budget in the strategy assumes the 5.45 s floor."
#endif

/* Slow-LSI ceiling, stated for the record rather than checked: 15.06 s. A hang
   is invisible for that long at worst; §5.3's ladder restarts inside it. */
#define IWDG_WINDOW_MAX_MS   ((IWDG_RELOAD * IWDG_PRESCALER_DIV * 1000UL) / IWDG_LSI_SLOWEST_HZ)

/* --------------------------------------------------------------------------
 * Static storage only (§2.3 rule 4). No DMA touches any of it → no .d1ram need
 * (§10.2.1 P4 is still open anyway).
 * -------------------------------------------------------------------------- */
static IWDG_HandleTypeDef s_iwdg;             /* BOOT-only configuration       */

/* SWD evidence, same idiom as s_lis3dsh_log[] in main.c. volatile so -O2 keeps
   the stores. s_reset_cause is what §5.1 wants in the MAN/journal later, so it
   is kept readable for the whole session rather than consumed once. */
static volatile uint8_t s_reset_cause;
static volatile uint8_t s_iwdg_init_status;   /* HAL_StatusTypeDef of Init()    */

/**
 * @brief  Decode RCC_CSR's sticky reset flags, then clear them.
 * @param  none
 * @return none. Side effects: writes s_reset_cause; sets RCC_CSR.RMVF.
 * @note   MUST BE THE FIRST STATEMENT OF main() — ahead of HAL_Init(). See the
 *         header for why the position, not the code, is the contract.
 * @note   The six masks come from the pinned CMSIS device header (bit positions
 *         25…31, RCC_CSR_BORRSTF/PORRSTF/PINRSTF/SFTRSTF/IWDGRSTF/WWDGRSTF/
 *         LPWRRSTF), so this decode cannot drift from the silicon if the platform
 *         package is re-pinned.
 * @note   One read into a local, then decode from it: RCC_CSR is a live register
 *         (LSIRDY changes under us once the IWDG runs), and reading it twice
 *         would let two different snapshots describe one cause code.
 */
void bsp_reset_cause_init(void)
{
    const uint32_t csr = READ_REG(RCC->CSR);
    uint8_t cause = 0U;

    if ((csr & RCC_CSR_IWDGRSTF_Msk) != 0UL) { cause |= BSP_RST_CAUSE_IWDG; }
    if ((csr & RCC_CSR_WWDGRSTF_Msk) != 0UL) { cause |= BSP_RST_CAUSE_WWDG; }
    if ((csr & RCC_CSR_PORRSTF_Msk)  != 0UL) { cause |= BSP_RST_CAUSE_POR;  }
    if ((csr & RCC_CSR_PINRSTF_Msk)  != 0UL) { cause |= BSP_RST_CAUSE_PIN;  }
    if ((csr & RCC_CSR_BORRSTF_Msk)  != 0UL) { cause |= BSP_RST_CAUSE_BOR;  }
    if ((csr & RCC_CSR_LPWRRSTF_Msk) != 0UL) { cause |= BSP_RST_CAUSE_LPWR; }

    s_reset_cause = cause;

    /* SET_BIT, not a whole-register write: the flags are rc_sr (writing 1
       clears), and reading them back into the written value is exactly how they
       get cleared here — while LSION/LSIRDY keep the values they had, because
       SET_BIT cannot disturb a bit it did not read as 1 in order to write. */
    SET_BIT(RCC->CSR, RCC_CSR_RMVF_Msk);
}

/**
 * @brief  The reset cause captured by bsp_reset_cause_init().
 * @param  none
 * @return OR of BSP_RST_CAUSE_* bits, or 0 if no flag was set.
 * @note   Read-only for the rest of the session: the hardware flags are already
 *         cleared, so this variable is the only copy until step 17 mirrors it
 *         into the Backup-SRAM journal and V3LOG.MAN (§5.1, last row).
 */
uint8_t bsp_reset_cause_get(void)
{
    return s_reset_cause;
}

/**
 * @brief  Arm the IWDG with §5.1's settings and start it.
 * @param  none
 * @return none — the HAL result is recorded in s_iwdg_init_status, not returned.
 * @note   Side effects: IWDG_KR = 0xCCCC (start), KR = 0x5555 (unlock), PR/RLR
 *         written, wait for IWDG_SR.{PVU,RVU}, KR = 0xAAAA (reload). That is
 *         HAL_IWDG_Init() verbatim (stm32f4xx_hal_iwdg.c:164-210) and it is the
 *         whole reason the handle carries only two Init fields.
 * @note   THE WINDOW STARTS WHEN THIS RETURNS. The closing KR = 0xAAAA
 *         (stm32f4xx_hal_iwdg.c:206) reloads the counter with 1000, so from this
 *         instant the counter is running down through the 8.000 s nominal —
 *         5.45 s at 47 kHz — and the rest of BOOT shares that time. The budget is
 *         written where it has to be spent, at the call site in main.c.
 * @note   Cannot trip itself: the wait for PVU/RVU is bounded by HAL's own
 *         HAL_IWDG_DEFAULT_TIMEOUT (stm32f4xx_hal_iwdg.c:123-124; with
 *         LSI_VALUE = 32000 that is 6.1 s + LSI startup), and during it the
 *         counter is still loaded with the RESET RLR = 0x0FFF, i.e. 32.8 s at
 *         nominal LSI and 61.7 s at the 17 kHz limit. Practically the flags clear
 *         in about 6 LSI periods (µs). A hang inside this function is therefore
 *         not a reset cause worth worrying about — but it is also not possible:
 *         the loop exits on HAL_GetTick(), whose time base main.c has already
 *         proven before reaching here.
 * @note   FAILURE POLICY: a non-HAL_OK status is stored and execution continues.
 *         The counter is already running by the first statement of
 *         HAL_IWDG_Init(), so no return value can mean "watchdog missing" — the
 *         only realistic failure (a PR/RLR update that never became valid) leaves
 *         the DEFAULT window in force, which is longer, not absent. Trading a
 *         slow watchdog for a reset at BOOT would destroy the boot diagnostics
 *         (the presence gate, LED 8) that tell the bench what actually happened.
 *         Retrying or re-arming is pointless: PR/RLR are already unlocked and the
 *         peripheral is not stoppable. s_iwdg_init_status is the evidence.
 */
void bsp_iwdg_init(void)
{
    s_iwdg.Instance       = IWDG;
    s_iwdg.Init.Prescaler = IWDG_PRESCALER;   /* IWDG_PR.PRD = /256 (§5.1)      */
    s_iwdg.Init.Reload    = IWDG_RELOAD;      /* IWDG_RLR.RLD = 1000 (§5.1)     */

    s_iwdg_init_status = (uint8_t)HAL_IWDG_Init(&s_iwdg);
}

/**
 * @brief  Reload the down-counter with 1000: another full window.
 * @param  none
 * @return none.
 * @note   Exactly IWDG_KR = 0xAAAA. HAL_IWDG_Refresh() is one write and cannot
 *         fail or return early (stm32f4xx_hal_iwdg.c:238-245), which is why this
 *         wrapper has no error path and why feeding can never become a blocking
 *         operation — a feed that can wait is a feed that hides a hang.
 * @note   The reload itself is asynchronous: the counter is reloaded in the LSI
 *         domain, about 2 LSI periods later (≈ 62 µs at 32 kHz nominal, 118 µs at
 *         17 kHz). Nothing in this firmware needs to observe it sooner.
 * @note   OBSERVABILITY, stated because it shapes the M0 test: IWDG_KR is
 *         write-only and IWDG_SR only reports PR/RLR/counter update state, so a
 *         successful feed cannot be read back. The proof for step 5 is therefore
 *         the reset itself — §10.2's M0 exit criterion "IWDG reset detected and
 *         mirrored": stop feeding (app_error_halt() does exactly that today),
 *         wait out ≤ 5.45 s worst case, and check that the next boot decodes
 *         BSP_RST_CAUSE_IWDG and announces LED 8.
 * @note   WHO MAY CALL (§5.1, restated at the declaration): the consumer only,
 *         on proven progress. Concretely today: the super-loop, gated on the 1 Hz
 *         supervisor flag. Never an ISR, never an unconditional loop tail, never
 *         "just in case" around a long operation — §5.1's long-operation rule
 *         demands a feed conditioned on measurable advance, at most once per
 *         500 ms, and no such operation exists yet.
 */
void bsp_iwdg_feed(void)
{
    (void)HAL_IWDG_Refresh(&s_iwdg);
}


