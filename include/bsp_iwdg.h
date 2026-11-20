/**
 ******************************************************************************
 * @file    bsp_iwdg.h
 * @brief   IWDG: the 8 s "proven progress" watchdog (§5.1) and the reset-cause
 *          capture that makes §5.1's "a reset is never silent" true (§8 LED 8).
 *          (§5.1 policy table · §8 LED code 8 · K7 in Docs/00-ARCHITECTURE.md:98)
 *
 * THE CONTRACT IN ONE PARAGRAPH.
 *   §5.1: LSI 32 kHz nominal, prescaler /256, reload 1000 → 8.00 s nominal, and
 *   a real timeout of 5.45 … 15.06 s across the LSI spread of 17…47 kHz, of
 *   which the design budgets the 5.45 s FLOOR. Fed by the consumer only — the
 *   remark in §5.1 ("feeding from an ISR turns the watchdog into decoration") is
 *   why bsp_iwdg_feed() has exactly one caller today (the super-loop) and why no
 *   ISR in this project may ever gain one. K7 in Docs/00-ARCHITECTURE.md:98 is
 *   the same row in KPI form: "8.0 s nominal (5.45–15.06 s over LSI spread),
 *   refresh on proven progress only".
 *
 * WHY THE RESET CAUSE LIVES HERE.
 *   §5.1's last row makes the reset flags part of the watchdog contract ("a
 *   reset is never silent; LED 8 on the next boot"), and the flags are one
 *   software clear away from being gone: RCC_CSR.RMVF wipes them. They must be
 *   read before anything else runs, so bsp_reset_cause_init() is the FIRST
 *   statement of main(), ahead of HAL_Init(). This module owns the decode and
 *   the clear; the reporting (LED 8 now, the Backup-SRAM journal and the MAN
 *   mirror of §5.1 later) is policy and belongs to the state machine of step 17.
 *
 * WHAT IS DELIBERATELY NOT HERE.
 *   No LSION handling: see the long note in bsp_iwdg.c — LSI starts with the
 *   IWDG. No WWDG, no PVD interaction, no Backup-SRAM journal, no MAN mirror, no
 *   low-power (STOP) handling: §5.1 keeps all of those, this step is the timer
 *   and the cause code only. Also not here: the L1/L3 recovery rungs — the
 *   watchdog only resets, it does not decide what a reset means.
 ******************************************************************************
 */

#ifndef BSP_IWDG_H
#define BSP_IWDG_H

#include <stdint.h>   /* uint8_t — C99, no C++ (§2.3 coding rules) */

/* --------------------------------------------------------------------------
 * Decoded RCC_CSR reset flags — a bit-per-cause bitmask, so a caller can ask
 * the question §8 actually asks ("was the previous session ended by an
 * IWDG/BOR reset?") with one AND instead of a ladder of if-else arms.
 *
 * Values are deliberately sparse (bit 0..5, never bit 24..31): a future member
 * cannot collide with the raw RCC_CSR layout, and 0 stays meaningful — "no
 * cause decoded at all", which on real hardware means bsp_reset_cause_init()
 * was not the first statement of main().
 *
 * NOT included on purpose: RCC_CSR.SFTRSTF. It is what OpenOCD's SYSRESETREQ
 * sets (App. H in Docs/90-DECISIONS-LOG.md records the bench re-test using
 * exactly that), so on a debug bench it would be the most common bit and the
 * least informative one. §5.1's LED-8 rule is IWDG/BOR only, so that is what
 * is decoded here.
 * -------------------------------------------------------------------------- */
#define BSP_RST_CAUSE_IWDG   0x01U   /* IWDG counter reached 0 → a K7 miss   */
#define BSP_RST_CAUSE_WWDG   0x02U   /* WWDG reset — not used by firmware    */
#define BSP_RST_CAUSE_POR    0x04U   /* POR/PDR reset: a real power cycle    */
#define BSP_RST_CAUSE_PIN    0x08U   /* NRST pin: the bench RESET button     */
#define BSP_RST_CAUSE_BOR    0x10U   /* BOR trip: a §6 VDD event, LED 8 half */
#define BSP_RST_CAUSE_LPWR   0x20U   /* LPWR (standby) reset — §6.3 later    */

/**
 * @brief  Capture and clear the hardware reset flags. FIRST statement of main().
 * @param  none
 * @return none. Side effects: reads RCC->CSR, latches the decoded cause, then
 *         sets RCC_CSR.RMVF, which clears the six sticky flags.
 * @note   ORDER IS THE WHOLE POINT. The flags are sticky and are cleared by a
 *         POR/PIN reset or by software writing RMVF. HAL_Init() does not clear
 *         them, but HAL_RCC_DeInit() does and the unwind paths of clock setup
 *         are allowed to call it — so reading after HAL_Init() would be a
 *         dependency on HAL internals, and reading after SystemClock_Config()
 *         a dependency on every guard in main.c. Read first.
 * @note   The clear uses SET_BIT, not a whole-register write: RCC_CSR also holds
 *         LSION, and nothing in this step may disturb the oscillator that the
 *         IWDG is about to be started by.
 */
void bsp_reset_cause_init(void);

/**
 * @brief  The cause captured at boot, unchanged for the whole session.
 * @return OR of the BSP_RST_CAUSE_* bits, or 0 if none was decoded.
 * @note   §8's code 8 ("previous session ended by IWDG/BOR reset") is
 *         ((cause & (BSP_RST_CAUSE_IWDG | BSP_RST_CAUSE_BOR)) != 0). Today
 *         main.c makes that call once at BOOT; step 17 moves it into the state
 *         machine, and §5.1 adds the two other homes for the same byte (the
 *         16-entry Backup-SRAM journal and the V3LOG.MAN mirror).
 */
uint8_t bsp_reset_cause_get(void);

/**
 * @brief  Arm the IWDG: /256, reload 1000 → 8.00 s nominal, 5.45 s worst case.
 * @param  none
 * @return none. Side effects: writes IWDG_KR/PR/RLR and waits for the IWDG
 *         update flags; on return the counter is RUNNING and cannot be stopped
 *         again until the next reset (§5.1, RM0090 IWDG chapter).
 * @note   CALL AFTER bsp_tim2_init() AND AFTER SystemClock_Config(), and budget
 *         the rest of BOOT against the 5.45 s floor: HAL_IWDG_Init() refreshes
 *         once on the way out (stm32f4xx_hal_iwdg.c:206), so the first window
 *         starts the moment this returns. The arithmetic lives at the call site
 *         in main.c, where the blink and gate delays it must share it with are.
 * @note   Not retried, not repaired: a watchdog has no "give me another try"
 *         path. A failure of the setup is recorded, never silenced.
 */
void bsp_iwdg_init(void);

/**
 * @brief  Reload the counter with 1000 → another full window.
 * @param  none
 * @return none.
 * @note   CALLER CONTRACT (§5.1, binding): super-loop / consumer context only,
 *         and only on PROVEN PROGRESS — today "the 1 Hz supervisor tick is
 *         alive"; later "≥1 block committed since the last feed", "a completed
 *         recovery/rotation/scan sub-step", or "a long operation feeding at
 *         most once per 500 ms, conditioned on measurable advance". Never from
 *         an ISR, and never as the unconditional last statement of the loop.
 */
void bsp_iwdg_feed(void);

#endif /* BSP_IWDG_H */

