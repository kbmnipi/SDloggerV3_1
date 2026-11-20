/**
 ******************************************************************************
 * @file    bsp_rtc.h
 * @brief   The §7 wall-clock anchor: two-phase LSE bring-up, the frozen
 *          127/255 prescaler (SSR 256 Hz), the §7.1 double-read millisecond
 *          reader, and the §7.3 boot validity gate.
 *          (§2.2 clock source · §5.1 boot budget · §7 preamble · §7.1 · §7.3)
 *
 * THE PRESCALER CHAIN IS TRANSCRIBED, NOT CHOSEN (§7 preamble).
 *   LSE 32 768 Hz -> RTC_PRER.PREDIV_A = 127 -> F1 = 32768/(127+1) = 256 Hz ->
 *   RTC_PRER.PREDIV_S = 255 -> 256/(255+1) = 1.000000 Hz calendar tick, and the
 *   RTC_SSR sub-second field therefore ticks at 256 Hz, so its LSB is
 *   1/256 s = 3.90625 ms (§7: "SSR LSB 3.906 ms"). §7.4 lists the same
 *   arithmetic in the traceability table as "exact (32768/128/256 = 1.000000 Hz),
 *   audit-verified". Nothing in this module may re-tune it, and bsp_rtc_now_ms()'s
 *   fraction term is only meaningful while RTC_PRER holds BSP_RTC_PRER_VALUE.
 *
 * LSE ONLY — LSI BELONGS TO THE WATCHDOG AND NEVER TO THE RTC (§2.2).
 *   The clock table row is explicit: RTC = LSE 32.768 kHz on PC14/PC15,
 *   IWDG = LSI ~32 kHz. Two oscillators, two jobs. The IWDG's LSI starts by
 *   itself with the watchdog (§5.1; measured on the bench as RCC_CSR.LSIRDY = 1
 *   with LSION = 0, App. H Test A record in Docs/90-DECISIONS-LOG.md), so an
 *   LSI-fed RTC is reachable by accident, not by intent. If RCC_BDCR.RTCSEL is
 *   found set to a foreign source this module reports it by leaving it visible
 *   in BDCR (0x40002370 over SWD) and returns INVALID from the gate — it does
 *   not "repair" it, because changing RTCSEL requires a backup-domain reset,
 *   and that erases the calendar together with the §7.3 magic word. That
 *   decision belongs to §7.3 item 4 (app/logcore), not to BSP.
 *
 * PC14 / PC15 ARE NEVER TOUCHED AS GPIO.
 *   With RCC_BDCR.LSEON set and RCC_BDCR.LSEBYP left at 0 (crystal mode, no
 *   external clock drive), the oscillator owns those two pins in hardware. There
 *   is no GPIO write anywhere in src/bsp_rtc.c, deliberately.
 *
 * NOT IN THIS MODULE — each item has an owner elsewhere:
 *   §7.3 item 4 fallback chain (the manifest's last anchor UTC, Backup-SRAM
 *   last_utc behind its CRC, else 0; clock_valid in the HEADER, CLOCK_INVALID in
 *   every block header, LED 7) = app/logcore, and step 17 owns all LED codes.
 *   get_fattime() = needs FatFs, which is not vendored (§10.2.1 P1 still open).
 *   The IDLE 5 s alarm / wakeup timer (§6.3, step 17), and §7.1's anchor
 *   plausibility filter + odr_est (producer territory, step 9+). This module
 *   installs no interrupt and changes no NVIC setting.
 *   §7.1's "single owner: the producer" rule: at step 6 there is no producer, so
 *   every call below is BOOT-only and single-context, which is exactly what makes
 *   the shadow-lock reads safe today. When the producer arrives, only it calls
 *   bsp_rtc_now_ms() (§7.1) and a thread-context reader takes the published
 *   anchor instead.
 ******************************************************************************
 */

/* ==========================================================================
 * WHY NO HAL RTC / RCCEx CALLS AT ALL.
 * The step-6 brief allows HAL for the clock/init-mode plumbing; it is still
 * unusable here, and all four reasons are in the pinned sources:
 *   1) HAL_RTC_Init() writes RTC_PRER unconditionally from its InitStructure,
 *      while §7.3 needs a backup domain that kept power (VBAT) to keep the
 *      PRER/calendar it already has untouched.
 *   2) HAL_RCCEx_PeriphCLKConfig() force-resets the whole backup domain whenever
 *      RTCSEL differs from the requested source — the F405 branch is
 *      stm32f4xx_hal_rcc_ex.c:2640-2641 (__HAL_RCC_BACKUPRESET_FORCE / _RELEASE),
 *      warned in its own header comment at :2545-2547 ("all backup domain (RTC
 *      and RCC_BDCR register expect BKPSRAM) will be reset"). That deletes the
 *      calendar AND RTC_BKP0R = 0xA55A3C3C: it destroys the evidence §7.3 reads.
 *   3) The same function waits for LSE up to RCC_LSE_TIMEOUT_VALUE =
 *      LSE_STARTUP_TIMEOUT = 5000 ms (stm32f4xx_hal_rcc.h:1331,
 *      stm32f4xx_hal_conf.h:127) — a blocking wait that alone outlives the 5.45 s
 *      fast-LSI IWDG floor of §5.1.
 *   4) HAL_RTC_GetTime() and HAL_RTC_GetDate() are two separate APB access
 *      sequences, so RTC_SSR lands OUTSIDE the TR..DR window §7.1's shadow lock
 *      needs, and HAL cannot sandwich two full read sets and compare raw words.
 * Every RTC/RCC/PWR touch in src/bsp_rtc.c is therefore a direct register access
 * against the pinned CMSIS device header: RTC_TypeDef at stm32f405xx.h:538-580,
 * RCC_BDCR bits at :9951-9969, PWR_CR.DBP at :9095-9097, RTC_CR/RTC_ISR/RTC_PRER/
 * RTC_SSR field masks at :10200-10300 and :10462-10464.
 * ========================================================================== */

#ifndef BSP_RTC_H
#define BSP_RTC_H

#include <stdint.h>   /* uint8_t / uint16_t / uint64_t — C99, no C++ (§2.3 coding rules) */

/* ==========================================================================
 * §7 frozen prescaler chain. See the header comment for the arithmetic; these
 * are transcribed values, not tunables.
 * ========================================================================== */
#define BSP_RTC_PREDIV_A   127U   /* RTC_PRER.PREDIV_A: 32768 / (127+1) = 256 Hz (F1) */
#define BSP_RTC_PREDIV_S   255U   /* RTC_PRER.PREDIV_S: 256 / (255+1) = 1.000000 Hz  */
#define BSP_RTC_PRER_VALUE (((uint32_t)BSP_RTC_PREDIV_A << 16) | (uint32_t)BSP_RTC_PREDIV_S) /* 0x007F00FF */
#define BSP_RTC_SSR_HZ     (BSP_RTC_PREDIV_S + 1U)  /* 256 = the RTC_SSR counter's own rate */

/* §7.3 item 2 — the backup-domain magic. Absent means the backup domain lost
 * power, so the calendar it carries is not trustworthy whatever the calendar
 * fields read as. 0xA55A3C3C is the strategy's value; RTC_BKP0R is its home
 * (RTC_BASE 0x40002800 + 0x50 = 0x40002850, readable over SWD). */
#define BSP_RTC_MAGIC      0xA55A3C3CU

/* §7.3 item 3 — plausibility window 2026 <= year <= 2099. The F4 date register
 * holds the year as two BCD digits of a 20xx year (RTC_DR YT/YU), so the window
 * is stated on the field value itself: 26..99. */
#define BSP_RTC_YEAR_BASE  2000U
#define BSP_RTC_YEAR_MIN   26U
#define BSP_RTC_YEAR_MAX   99U


/**
 * @brief  Phase 1 — start the LSE, then get out of the way.
 * @param  none
 * @return none. Side effects: enables the PWR kernel clock, sets PWR_CR.DBP
 *         (waits for its read-back within 20 ms only), sets RCC_BDCR.LSEON.
 *         Writes nothing to PC14/PC15, to LSEBYP, to RTCSEL or to any RTC
 *         register.
 * @note   CALL IMMEDIATELY AFTER bsp_iwdg_init() in main.c. LSE startup on a
 *         real crystal is hundreds of milliseconds to seconds, and that time has
 *         to be spent while the boot path is doing work that already exists —
 *         the §8 code 8 announce and the §3.1 step 2 presence gate, up to ~2.3 s
 *         in the worst case — instead of inside a wait. This call returns in
 *         microseconds, which is what keeps §5.1's rule true.
 * @note   DBP is set first because it gates EVERY write into the backup domain:
 *         with DBP clear the LSEON write is ignored rather than queued, so a dead
 *         LSE would look like a crystal problem when it is a write-protection
 *         problem. §2.3: no silent failures.
 */
void bsp_rtc_lse_start(void);

/**
 * @brief  Phase 2 — finish the RTC bring-up with a bounded wait, or give up.
 * @param  none
 * @return none. Side effects: once LSE is ready, sets RCC_BDCR.RTCSEL = LSE (only
 *         when it reads 0 or already LSE) and sets RCC_BDCR.RTCEN; then either
 *         programs the frozen prescaler (RTC_PRER = 0x007F00FF, 24 h format,
 *         BYPSHAD cleared) through the RM0090 initialization mode, or leaves
 *         PRER/calendar untouched. Never halts, never resets, never touches
 *         PC14/PC15.
 * @note   CALL AFTER THE PRESENCE GATE, BEFORE THE SUPER-LOOP. The LSERDY poll is
 *         capped at 500 ms measured on TIM2 (§5.1 forbids a blocking LSE wait in
 *         the boot path; HAL's own ceiling for the identical wait is 5000 ms, and
 *         that alone would outlive the 5.45 s fast-LSI watchdog floor). An expired
 *         budget returns with the gate left INVALID — §7.3's declared failure
 *         path — and is recorded as s_lse_startup_ms = 0xFFFFFFFF.
 * @note   WHY THE INIT BRANCH IS CONDITIONAL: if RTC_BKP0R holds the magic AND
 *         RTC_ISR.INITS is clear, the backup domain kept power (VBAT) and the
 *         previous session's calendar is precisely what §7.3 wants preserved;
 *         re-entering initialization mode would hand that session a
 *         half-written calendar. Writing PRER is what a fresh or never-initialised
 *         domain needs, so it happens only when the magic is absent or INITS is
 *         set.
 */
void bsp_rtc_init(void);

/**
 * @brief  §7.3 validity gate at boot (the "year 2000 rock" gate).
 * @return 1 = clock valid, 0 = clock invalid. Caches nothing: every call re-reads
 *         RCC_BDCR, RTC_ISR, RTC_BKP0R and takes a fresh §7.1 double read, so a
 *         caller may re-evaluate after any event that could change the answer.
 *         Side effect: publishes the answer to s_rtc_gate (SWD-readable).
 * @note   The conditions, cheapest first: the RCC_BDCR access preconditions (LSE
 *         ready, RTCSEL = LSE, RTCEN set); RTC_ISR.RSF set (shadow registers
 *         synchronised with the APB bus); RTC_ISR.INITS clear (the calendar has
 *         been fully configured); not sitting in initialization mode;
 *         RTC_BKP0R == BSP_RTC_MAGIC (§7.3 item 2); the §7.1 double read matched;
 *         and the calendar fields plausible — year offset 26..99, month 1..12,
 *         day 1..31, hour < 24, minute < 60, second < 60 (§7.3 item 3). Any
 *         failure returns 0. Nothing here is repaired or retried: §7.3's answer to
 *         a failure is its fallback chain, and that chain is app/logcore.
 * @note   Strictly STRICTER than §7.3's list, never laxer. The BDCR/RTCEN terms
 *         are hardware-access preconditions (with RTCEN clear the RTC registers
 *         are not accessible at all, so "RSF set" would be a claim about a read
 *         that cannot happen), and a snapshot whose sub-second count exceeds
 *         PREDIV_S cannot belong to the frozen 127/255 chain, so no millisecond
 *         value exists for it. Both can only turn VALID into INVALID.
 */
uint8_t bsp_rtc_gate(void);


/**
 * @brief  Milliseconds since 1970-01-01 00:00:00 UTC — the container's time
 *         domain (§7.1's t_latch_ms, App. A's t_* stamps).
 * @param  none
 * @return the accepted value; or the last accepted value when every read of this
 *         call was torn; or 0 when nothing has ever been accepted. Side effects:
 *         publishes the result to s_rtc_now_ms and counts compare failures in
 *         s_rtc_torn_cnt.
 * @note   MEANINGFUL ONLY WHEN bsp_rtc_gate() RETURNED 1. The gate is what
 *         establishes that prescaler, magic and plausibility all hold; this
 *         function re-reads and re-checks, but it is not the gate and does not
 *         replace it.
 * @note   THE READ ORDER IS THE PROTOCOL (§7.1): TR -> SSR -> DR, then the same
 *         three again, then compare all three RAW words — §7.1's own form,
 *         "if (TR1==TR2 && SSR1==SSR2 && DR1==DR2) -> accept", up to 3 attempts,
 *         "else: skip this anchor". Raw comparison on purpose: comparing after
 *         BCD conversion would hide a tear that only shows up inside one BCD
 *         field. §7.1's cost note (1-3 us per read pair) is why the retry budget
 *         is cheap enough to keep.
 * @note   HAL_RTC_GetTime()/HAL_RTC_GetDate() cannot be used for this: two
 *         separate APB sequences put RTC_SSR outside the TR..DR shadow-lock
 *         window (see the WHY NO HAL block above).
 * @note   Fraction term: ms_within_second = (PREDIV_S - SSR) * 1000 / (PREDIV_S+1),
 *         integer arithmetic only — no float, no double, no libc <time.h>
 *         (§3.2.1 numeric contract; §7's 3.906 ms SSR quantisation). The day
 *         number comes from a 20-line integer civil-calendar routine rather than
 *         timegm(), which would drag libc time tables and a 64-bit time_t path
 *         into something §7.4 calls exact.
 */
uint64_t bsp_rtc_now_ms(void);

/**
 * @brief  Set the RTC calendar and stamp the §7.3 magic — the host/bench path.
 * @param  ms  Milliseconds since 1970-01-01 00:00:00 UTC. A value whose date does
 *             not land inside the §7.3 window (2026..2099) is REFUSED outright:
 *             the two-digit BCD year field could not hold it, and writing it
 *             would hand the next boot a calendar the gate must then reject.
 * @return none. Side effects: enters initialization mode, writes the frozen PRER,
 *         the 24 h format and RTC_DR / RTC_TR / RTC_SSR, exits, waits for RSF,
 *         and only then writes RTC_BKP0R = BSP_RTC_MAGIC. On any failure it
 *         returns having written NO magic, so the next boot reads INVALID instead
 *         of a clock that looks trustworthy.
 * @note   THREAD CONTEXT ONLY, AND NOT PART OF THE STEP-6 BOOT PATH: it may wait
 *         up to 2 s for LSERDY, which the §5.1 boot budget cannot afford. main.c
 *         does not call it. Its caller is whoever owns §7.3's fallback chain, or a
 *         host "set time" command once the filesystem exists.
 * @note   THE ORDER IS NOT INTERCHANGEABLE: magic LAST, because the magic is the
 *         claim that the calendar behind it is good. Magic first with a failed
 *         calendar write is a trustworthy-looking lie.
 */
void bsp_rtc_set_utc_ms(uint64_t ms);

#endif /* BSP_RTC_H */



