/**
 ******************************************************************************
 * @file    bsp_rtc.c
 * @brief   RTC bring-up (§7) in two phases, the §7.1 double-read millisecond
 *          reader, and the §7.3 boot validity gate. Direct register access only
 *          — the four reasons HAL RTC / RCCEx cannot be used are listed in
 *          include/bsp_rtc.h ("WHY NO HAL RTC / RCCEx CALLS AT ALL").
 *
 * WHERE THIS MODULE SITS (step 6 of the M0 order).
 *   The RTC is the wall-clock anchor of §7 and nothing else: sample instants
 *   stay in the sensor ODR domain, elapsed time stays TIM2's job (§2.4, "TIM2 is
 *   not the sample clock, not an RTC/ADC owner"), and this module is neither a
 *   producer nor a clock source for anything else. It owns no policy, takes no
 *   state decision, installs no interrupt and changes no NVIC setting — `bsp_*`
 *   is "HAL/LL only, no policy" (Docs/00-ARCHITECTURE.md:171). §7.3's fallback
 *   chain (manifest anchor, Backup-SRAM last_utc, clock_valid, LED 7) is
 *   deliberately NOT here: it is app/logcore, and step 17 owns the LED.
 *
 * WHAT THE BOOT PATH PAYS FOR THIS (§5.1, and it is a small, bounded price).
 *   Phase 1 costs microseconds and is placed so that the crystal's own startup
 *   time overlaps the announce + presence gate, which already exist. Phase 2 adds
 *   AT MOST 500 ms, and only when the LSE is absent or slow; measured on the bench
 *   (Test C) the announce+gate path is 3.43-3.50 s of the 5.45 s fast-LSI floor,
 *   so 3.50 + 0.50 = 4.00 s and >= 1.4 s of the floor stays unspent. When the
 *   worst-case 8-pulse announce of main.c's own budget note is used (3.81 s), the
 *   total is 4.31 s and >= 1.14 s unspent. Both are still above §5.1's ">2x" rule
 *   against the feed interval because the first feed happens immediately in the
 *   super-loop; the number that changed is the unspent remainder, and it is
 *   written at the call site in main.c.
 ******************************************************************************
 */

#include <stdint.h>
#include "bsp_rtc.h"       /* own API + the frozen prescaler constants */
#include "bsp_tim2.h"      /* §2.2 1 MHz counter: the only clock this module times with */
#include "stm32f4xx_hal.h" /* PWR/RCC clock-enable macros + the pinned CMSIS register map */

/* ==========================================================================
 * Time budgets (§5.1). Every one of them is a CEILING that the code honours by
 * returning: an expired budget ends the attempt, it never restarts the wait and
 * it never halts. All of them are measured on TIM2 (bsp_tim2_now_us /
 * bsp_tim2_elapsed_us), not on HAL_GetTick(), so this module's worst case does
 * not depend on the SysTick base at all — and TIM2's CNT arithmetic is
 * wrap-safe for intervals below 4294.97 s (§2.2), which every budget here is.
 *
 * RTC_LSE_BOOT_BUDGET_MS is the number that makes phase 2 legal inside BOOT at
 * all. HAL's ceiling for the identical wait is RCC_LSE_TIMEOUT_VALUE =
 * LSE_STARTUP_TIMEOUT = 5000 ms (stm32f4xx_hal_rcc.h:1331,
 * stm32f4xx_hal_conf.h:127): 5 s would outlive the 5.45 s floor on its own,
 * which is exactly the "blocking LSE wait in the boot path" §5.1 forbids.
 * ========================================================================== */
#define RTC_LSE_BOOT_BUDGET_MS  500U    /* phase 2: LSERDY wait inside BOOT           */
#define RTC_LSE_SET_BUDGET_MS   2000U   /* set_utc_ms: thread context only, never BOOT */
#define RTC_DBP_BUDGET_MS       20U     /* PWR_CR.DBP read-back: a bit, not an oscillator */
#define RTC_INITF_BUDGET_MS     100U    /* INITF after INIT: expected in sub-seconds    */
#define RTC_RSF_BUDGET_MS       100U    /* RSF after INIT clear: 2 SSR periods = 7.812 ms needed, 100 ms allowed */

/* §7.1: "repeat up to 3 times ... else: skip this anchor". Three is the
 * strategy's number, not a tuning knob, and it is the same budget the producer
 * will use for block anchors later. */
#define RTC_TORN_RETRY_MAX      3U

/* Arithmetic constants for the ms conversion (integer only, §3.2.1). */
#define RTC_MS_PER_DAY          86400000ULL   /* 24 * 60 * 60 * 1000 */
#define RTC_MS_PER_HOUR         3600000ULL
#define RTC_MS_PER_MINUTE       60000ULL
#define RTC_MS_PER_SECOND       1000ULL


/* ==========================================================================
 * Bench observables (§10.2 M0: "checks passed or reported by LED" is not
 * readable yet — there is no UART and no log file, so a value SWD cannot see is
 * a value that was never measured). __attribute__((used)) is deliberate, not
 * decoration: env:release builds with -O2 plus -ffunction-sections
 * -fdata-sections and --gc-sections, and a file-scope variable that is only ever
 * WRITTEN is otherwise a candidate for removal from the image. "It is volatile"
 * stops the compiler deleting the store; it is not what keeps the .bss section
 * alive at link time, and a symbol that is not in the ELF cannot be read back.
 *
 *   s_rtc_gate        1 = clock valid, 0 = invalid   -> §7.3, the input of LED 7 (step 17) and of HEADER.clock_valid (step 15)
 *   s_rtc_now_ms      ms since 1970-01-01 UTC        -> the value App. A's t_* / §7.1's t_latch_ms is built from
 *   s_lse_startup_ms  phase-2 entry -> LSERDY, in ms; 0xFFFFFFFF = LSE never became ready inside RTC_LSE_BOOT_BUDGET_MS (or phase 2 never ran)
 *   s_rtc_torn_cnt    §7.1 double-read compare failures seen by bsp_rtc_now_ms() -> the F17-class "torn read" counter
 *
 * State deliberately NOT duplicated into a variable, because the silicon already
 * exposes it over SWD and a copy could drift from the register it claims to
 * describe:
 *   RCC_BDCR  0x40002370  LSEON / LSERDY / LSEBYP / RTCSEL / RTCEN / BDRST
 *   RTC_ISR   0x4000280C  INIT / INITF / RSF / INITS / SHPF
 *   RTC_PRER  0x40002810  must read BSP_RTC_PRER_VALUE (0x007F00FF)
 *   RTC_BKP0R 0x40002850  must read BSP_RTC_MAGIC once a time has been set
 * ========================================================================== */
static volatile uint8_t  s_rtc_gate       __attribute__((used));
static volatile uint64_t s_rtc_now_ms     __attribute__((used));
static volatile uint32_t s_lse_startup_ms __attribute__((used)) = UINT32_MAX;
static volatile uint32_t s_rtc_torn_cnt   __attribute__((used));

/* §7.1's fallback value: "return last-good or 0". Static storage only, as ever
 * (§2.3: no malloc/new/dynamic allocation anywhere in this project). */
static volatile uint64_t s_rtc_last_good_ms;   /* the last accepted conversion  */
static volatile uint8_t  s_rtc_last_good_ok;   /* 0 = nothing accepted since reset */

/* A raw snapshot, exactly as the silicon holds it. §7.1's compare runs on these
 * RAW BCD words on purpose: converting first would hide a tear that only shows
 * up inside one BCD field (for example RTC_TR_ST = 0b110, a legal 3-bit field
 * that is not a legal BCD tens digit). */
typedef struct {
    uint32_t tr;    /* RTC_TR  raw (RTC_BASE + 0x00) */
    uint32_t ssr;   /* RTC_SSR raw (RTC_BASE + 0x28) */
    uint32_t dr;    /* RTC_DR  raw (RTC_BASE + 0x04) */
} rtc_raw_t;

/* Decoded, binary, human-readable time. `year` is the full AD year: the F4 date
 * register carries only the last two digits of a 20xx year. */
typedef struct {
    uint16_t year;   /* 2000 + the RTC year field, e.g. 2026 */
    uint8_t  month;  /* 1..12   */
    uint8_t  day;    /* 1..31   */
    uint8_t  hour;   /* 0..23 (24 h format: RTC_CR.FMT = 0) */
    uint8_t  minute; /* 0..59   */
    uint8_t  second; /* 0..59   */
} rtc_time_t;


/* --------------------------------------------------------------------------
 * rtc_dbp_enable — open the backup domain for writing.
 * WHY IT EXISTS: PWR_CR.DBP gates every write to RCC_BDCR and to the RTC
 * registers (RM0090 PWR / BackupRTC domain). With DBP clear the writes are
 * IGNORED, not queued — so without this, "LSEON is set" would be a lie and a
 * dead LSE would be diagnosed as a crystal fault.
 * @return 1 = DBP confirmed set, 0 = not confirmed within RTC_DBP_BUDGET_MS.
 * -------------------------------------------------------------------------- */
static uint8_t rtc_dbp_enable(void)
{
    uint32_t t0;

    __HAL_RCC_PWR_CLK_ENABLE();        /* PWR sits on APB1; its kernel clock is NOT enabled by default */
    SET_BIT(PWR->CR, PWR_CR_DBP);      /* PWR_CR.DBP (bit 8): disable backup-domain write protection */

    /* Read-back, bounded. DBP is a plain R/W bit in PWR, not an oscillator, so
       this returns on the next APB read; the loop exists to make the write
       ORDERED with respect to the BDCR writes that follow it, and so that a
       failure is a returned value instead of an assumption. */
    t0 = bsp_tim2_now_us();
    while ((PWR->CR & PWR_CR_DBP) == 0U)
    {
        if (bsp_tim2_elapsed_us(t0) >= (RTC_DBP_BUDGET_MS * 1000U))
        {
            return 0U;
        }
    }
    return 1U;
}

/* --------------------------------------------------------------------------
 * rtc_wait_lserdy — bounded poll for "the LSE oscillator is running".
 * RCC_BDCR.LSERDY (bit 1) is set by hardware when the crystal is stable. This is
 * the ONLY place the module waits on anything external, and the budget is what
 * keeps it legal inside the boot path (§5.1).
 * @param budget_ms  ceiling in ms, measured on TIM2.
 * @return 1 = LSERDY seen, 0 = budget expired (caller then reports INVALID).
 * -------------------------------------------------------------------------- */
static uint8_t rtc_wait_lserdy(uint32_t budget_ms)
{
    uint32_t t0 = bsp_tim2_now_us();

    for (;;)
    {
        if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U)   /* RCC_BDCR.LSERDY (bit 1) */
        {
            return 1U;
        }
        if (bsp_tim2_elapsed_us(t0) >= (budget_ms * 1000U))
        {
            return 0U;   /* §7.3's declared path, not a fault to hide */
        }
    }
}

/* --------------------------------------------------------------------------
 * rtc_enter_init_mode — request the RM0090 initialization mode and confirm it.
 * WHY INIT IS WRITTEN AS A WHOLE-REGISTER WRITE, NOT `RTC->ISR |= RTC_ISR_INIT`:
 * RTC_ISR's low and middle bits (ALRAWF, ALRBWF, WUTWF, SHPF(r), INITS(r),
 * ALRAF, ALRBF, WUTF, TSF, TSOVF, TAMP1F/2F) are WRITE-1-TO-CLEAR. OR-ing a
 * read-back of ISR would write 1 into any of them that happened to be pending
 * and silently erase a flag that has nothing to do with the RTC bring-up — the
 * alarm/wakeup flags belong to step 17 and §6.3, and destroying them here would
 * be a side effect outside this module's contract. Writing INIT=1 and 0 into
 * every other position sets INIT, leaves the write-1-to-clear flags untouched
 * (0 has no effect on them), and cannot disturb the read-only INITF/RSF/INITS.
 * @return 1 = INITF confirmed (init mode entered), 0 = refused or timed out.
 * -------------------------------------------------------------------------- */
static uint8_t rtc_enter_init_mode(void)
{
    uint32_t t0;

    /* A calendar shift in progress blocks INIT (RM0090 RTC_ISR.SHPF). Nothing in
       this project shifts the calendar yet (step 17 adds the shift for §7.4's
       ppm trim), so this is a guard against a foreign write, not a wait. */
    if ((RTC->ISR & RTC_ISR_SHPF) != 0U)
    {
        return 0U;
    }

    RTC->ISR = RTC_ISR_INIT;               /* RTC_ISR.INIT (bit 7) = enter initialization mode */

    t0 = bsp_tim2_now_us();
    while ((RTC->ISR & RTC_ISR_INITF) == 0U)   /* RTC_ISR.INITF (bit 6) = init mode entered */
    {
        if (bsp_tim2_elapsed_us(t0) >= (RTC_INITF_BUDGET_MS * 1000U))
        {
            return 0U;
        }
    }
    return 1U;
}

/* --------------------------------------------------------------------------
 * rtc_exit_init_mode — leave the initialization mode.
 * Same whole-register write as above: 0 into the write-1-to-clear positions is a
 * no-op, so pending alarm/wakeup flags survive the exit.
 * Side effect: hardware begins re-synchronising the shadow registers; RSF is what
 * reports it (see rtc_wait_rsf).
 * -------------------------------------------------------------------------- */
static void rtc_exit_init_mode(void)
{
    RTC->ISR = 0U;   /* clears RTC_ISR.INIT (bit 7) and nothing else that is writable */
}

/* --------------------------------------------------------------------------
 * rtc_wait_rsf — wait for the shadow registers to be synchronised with APB1.
 * RM0090 sets RSF two sub-second periods after INIT is cleared; with the frozen
 * 256 Hz SSR that is 2 x 3.90625 = 7.8125 ms, so 100 ms is a ~13x bench
 * allowance rather than a timing claim. TODO: confirm the exact wording against
 * the RM0090 RTC chapter at the bench (§10.2.1 P5 is still open).
 * @return 1 = RSF set, 0 = budget expired.
 * -------------------------------------------------------------------------- */
static uint8_t rtc_wait_rsf(void)
{
    uint32_t t0 = bsp_tim2_now_us();

    while ((RTC->ISR & RTC_ISR_RSF) == 0U)   /* RTC_ISR.RSF (bit 5) */
    {
        if (bsp_tim2_elapsed_us(t0) >= (RTC_RSF_BUDGET_MS * 1000U))
        {
            return 0U;
        }
    }
    return 1U;
}


/* --------------------------------------------------------------------------
 * rtc_snapshot — one §7.1 double read of the three shadowed calendar registers.
 * THE SIX READS, IN THE ORDER THEY HAPPEN (this order IS the protocol):
 *     RTC->TR  ->  RTC->SSR  ->  RTC->DR  ->  RTC->TR  ->  RTC->SSR  ->  RTC->DR
 * §7.1 states the same order in its own pseudo-code: "(TR1, SSR1, DR1) = read
 * RTC_TR -> RTC_SSR -> RTC_DR", annotated there as "F4 order: reading time locks
 * shadow regs until DR is read", then "(TR2, SSR2, DR2) = read again". Reading TR
 * starts the lock and reading DR releases it, so the first triple is one coherent
 * set and the second triple is the NEXT coherent set; a mismatch therefore means a
 * 1 Hz calendar boundary was crossed somewhere inside the six accesses, which is
 * the exact event that would otherwise slip into the file as a mixed old/new
 * timestamp.
 * Compared RAW, before any conversion, for the reason stated on rtc_raw_t.
 * @param out  filled with the first coherent triple when 1 is returned.
 * @return 1 = accepted snapshot, 0 = torn (or not convertible, see below).
 * -------------------------------------------------------------------------- */
static uint8_t rtc_snapshot(rtc_raw_t *out)
{
    uint32_t tr2, ssr2, dr2;

    out->tr  = RTC->TR;    /* read 1/6: RTC_TR (0x40002800) — starts the shadow lock */
    out->ssr = RTC->SSR;   /* read 2/6: RTC_SSR (0x40002828) */
    out->dr  = RTC->DR;    /* read 3/6: RTC_DR  (0x40002804) — releases the shadow lock */

    tr2  = RTC->TR;        /* read 4/6 */
    ssr2 = RTC->SSR;       /* read 5/6 */
    dr2  = RTC->DR;        /* read 6/6 */

    if ((out->tr != tr2) || (out->ssr != ssr2) || (out->dr != dr2))
    {
        return 0U;   /* §7.1's "-> accept" condition failed: this anchor is skipped */
    }

    /* A sub-second count larger than PREDIV_S cannot come from the frozen
       127/255 chain (§7), so no millisecond conversion exists for this snapshot
       at all. Treating it as "not accepted" is the strict reading: it can only
       turn a VALID into an INVALID, never the reverse, and it closes the only
       way this module could produce a confident-looking number from a prescaler
       someone else changed. Bench evidence is the register itself:
       RTC_PRER (0x40002810) must read 0x007F00FF. */
    if ((out->ssr & RTC_SSR_SS) > (uint32_t)BSP_RTC_PREDIV_S)
    {
        return 0U;
    }
    return 1U;
}

/* --------------------------------------------------------------------------
 * rtc_bcd2bin — one BCD field to binary, with the digit check that makes a
 * corrupted field visible instead of silently wrong.
 * A field nibble pair of 0xA/0x5 is NOT "105 seconds", it is corruption, and
 * returning 105 would let it pass a range check that assumes BCD. So an illegal
 * digit sets *bad and the caller fails the whole snapshot.
 * -------------------------------------------------------------------------- */
static uint8_t rtc_bcd2bin(uint8_t tens, uint8_t units, uint8_t *bad)
{
    if ((tens > 9U) || (units > 9U))
    {
        *bad = 1U;
        return 0U;
    }
    return (uint8_t)((tens * 10U) + units);
}

/* --------------------------------------------------------------------------
 * rtc_decode — raw BCD words to the rtc_time_t fields, no policy in it.
 * Field positions are the pinned masks (stm32f405xx.h:10086-10170 for RTC_TR /
 * RTC_DR), so this is transcription, not invention: the 12-hour PM bit
 * (RTC_TR_PM, bit 22) is ignored on purpose because RTC_CR.FMT is kept at 0
 * (24-hour), which is what "24h format" of step 6 means.
 * @return 1 = every field is a legal BCD pair, 0 = at least one is not.
 * -------------------------------------------------------------------------- */
static uint8_t rtc_decode(const rtc_raw_t *raw, rtc_time_t *t)
{
    uint8_t bad = 0U;
    uint8_t tens, units;

    tens  = (uint8_t)((raw->dr & RTC_DR_YT) >> RTC_DR_YT_Pos);   /* RTC_DR.YT  bits 23:20 */
    units = (uint8_t)((raw->dr & RTC_DR_YU) >> RTC_DR_YU_Pos);   /* RTC_DR.YU  bits 19:16 */
    t->year  = (uint16_t)(BSP_RTC_YEAR_BASE + rtc_bcd2bin(tens, units, &bad));

    tens  = (uint8_t)((raw->dr & RTC_DR_MT) >> RTC_DR_MT_Pos);   /* RTC_DR.MT  bit 12     */
    units = (uint8_t)((raw->dr & RTC_DR_MU) >> RTC_DR_MU_Pos);   /* RTC_DR.MU  bits 11:8  */
    t->month = rtc_bcd2bin(tens, units, &bad);

    tens  = (uint8_t)((raw->dr & RTC_DR_DT) >> RTC_DR_DT_Pos);   /* RTC_DR.DT  bits 5:4   */
    units = (uint8_t)((raw->dr & RTC_DR_DU) >> RTC_DR_DU_Pos);   /* RTC_DR.DU  bits 3:0   */
    t->day   = rtc_bcd2bin(tens, units, &bad);

    tens  = (uint8_t)((raw->tr & RTC_TR_HT) >> RTC_TR_HT_Pos);   /* RTC_TR.HT  bits 21:20 */
    units = (uint8_t)((raw->tr & RTC_TR_HU) >> RTC_TR_HU_Pos);   /* RTC_TR.HU  bits 19:16 */
    t->hour  = rtc_bcd2bin(tens, units, &bad);

    tens  = (uint8_t)((raw->tr & RTC_TR_MNT) >> RTC_TR_MNT_Pos); /* RTC_TR.MNT bits 14:12 */
    units = (uint8_t)((raw->tr & RTC_TR_MNU) >> RTC_TR_MNU_Pos); /* RTC_TR.MNU bits 11:8  */
    t->minute = rtc_bcd2bin(tens, units, &bad);

    tens  = (uint8_t)((raw->tr & RTC_TR_ST) >> RTC_TR_ST_Pos);   /* RTC_TR.ST  bits 6:4   */
    units = (uint8_t)((raw->tr & RTC_TR_SU) >> RTC_TR_SU_Pos);   /* RTC_TR.SU  bits 3:0   */
    t->second = rtc_bcd2bin(tens, units, &bad);

    return (bad == 0U) ? 1U : 0U;
}


/* --------------------------------------------------------------------------
 * rtc_plausible — §7.3 item 3's window, applied to a decoded time.
 * Deliberately NOT a month-length check: §7.3's gate is "2026 <= year <= 2099"
 * plus the field ranges, and a 31 February that survives here is caught where it
 * matters — the host-side validator of §10.4 / App. A.6 sees the whole calendar,
 * and a BSP that quietly rejects legal-looking values would hide the difference
 * between "the RTC was never set" and "someone wrote a wrong date into it".
 * @return 1 = inside §7.3's window, 0 = outside it.
 * -------------------------------------------------------------------------- */
static uint8_t rtc_plausible(const rtc_time_t *t)
{
    uint16_t offset;

    if (t->year < BSP_RTC_YEAR_BASE)
    {
        return 0U;   /* cannot happen with a legal BCD decode, kept as a guard */
    }
    offset = (uint16_t)(t->year - BSP_RTC_YEAR_BASE);

    if ((offset < BSP_RTC_YEAR_MIN) || (offset > BSP_RTC_YEAR_MAX)) return 0U;  /* §7.3: 2026..2099 */
    if ((t->month < 1U) || (t->month > 12U))                        return 0U;
    if ((t->day   < 1U) || (t->day   > 31U))                        return 0U;
    if (t->hour   >= 24U)                                           return 0U;
    if (t->minute >= 60U)                                           return 0U;
    if (t->second >= 60U)                                           return 0U;
    return 1U;
}

/* --------------------------------------------------------------------------
 * rtc_days_from_civil — days since 1970-01-01 from (year, month, day).
 * WHY THIS EXISTS INSTEAD OF timegm(): §7.4 calls the prescaler arithmetic
 * "exact (32768/128/256 = 1.000000 Hz), audit-verified", and a day count that is
 * part of a timestamp has to be as auditable as the prescaler. libc <time.h>
 * brings struct tm, locale tables and a 64-bit time_t path into a §2.3 codebase
 * that forbids float in an ISR context and forbids dynamic storage outright; this
 * is ~10 lines of unsigned integer arithmetic that -Wall/-Wextra checks and that
 * a hardware engineer can verify by hand against a known date.
 * The algorithm is the standard civil-day mapping that treats the year as
 * starting 1 March, which is what makes the leap-day term fit in one line:
 * era = 400-year block, yoe = year-of-era, doy = day-of-era-year from 1 March.
 * Hand-checked: 2026-11-20 -> 20777 days, and 20777 x 86400000 = 1 795 132 800 000 ms.
 * @return days since 1970-01-01 (unsigned, exact for the §7.3 window 2026..2099).
 * -------------------------------------------------------------------------- */
static uint32_t rtc_days_from_civil(uint32_t y, uint32_t m, uint32_t d)
{
    uint32_t era, yoe, doy, doe, mm;

    if (m <= 2U)
    {
        y -= 1U;   /* Jan/Feb belong to the previous era-year in this mapping */
    }
    era = y / 400U;                                   /* 146097-day blocks           */
    yoe = y - (era * 400U);                           /* [0, 399]                    */
    mm  = (m > 2U) ? (m - 3U) : (m + 9U);             /* 1 March = 0 ... 28 Feb = 11 */
    doy = ((153U * mm + 2U) / 5U) + d - 1U;           /* [0, 365]                    */
    doe = (yoe * 365U) + (yoe / 4U) - (yoe / 100U) + doy;   /* day of era  [0, 146096] */
    return (era * 146097U) + doe - 719468U;           /* -719468 = 1970-01-01        */
}

/* --------------------------------------------------------------------------
 * rtc_civil_from_days — the inverse, used only by bsp_rtc_set_utc_ms().
 * Same mapping, run backwards. Hand-checked against the forward function above
 * (20777 -> 2026-11-20), which is what makes the round trip trustworthy.
 * -------------------------------------------------------------------------- */
static void rtc_civil_from_days(uint32_t days, uint32_t *y, uint32_t *m, uint32_t *d)
{
    uint32_t z, era, doe, yoe, doy, mp;

    z   = days + 719468U;
    era = z / 146097U;
    doe = z - (era * 146097U);
    yoe = (doe - (doe / 1460U) + (doe / 36524U) - (doe / 146096U)) / 365U;
    doy = doe - ((365U * yoe) + (yoe / 4U) - (yoe / 100U));
    mp  = (5U * doy + 2U) / 153U;

    *d = doy - ((153U * mp + 2U) / 5U) + 1U;
    *m = (mp < 10U) ? (mp + 3U) : (mp - 9U);
    *y = (era * 400U) + yoe;
    if (*m <= 2U)
    {
        *y += 1U;   /* undo the 1-March year shift */
    }
}

/* --------------------------------------------------------------------------
 * rtc_to_ms — an accepted snapshot to milliseconds since 1970-01-01 UTC.
 * THE FRACTION TERM, spelled out: the synchronous prescaler counts PREDIV_S = 255
 * -> 0 at 256 Hz inside each calendar second, so the part of the second that has
 * already elapsed is (255 - SSR) / 256, and in integer milliseconds
 * ms = (BSP_RTC_PREDIV_S - SSR) * 1000 / BSP_RTC_SSR_HZ. The division truncates,
 * which biases the value down by at most 3.906 ms — the same direction and the
 * same size as §7.1's own "SSR quantization 3.906 ms" note, and the reason §7.1
 * expects anchor residuals of 5-8 ms rather than 3.906 ms. No float, no double
 * (§3.2.1 numeric contract, and §2.3 bans them in an ISR context).
 * @return the timestamp. Callers have already accepted the snapshot and its
 *         plausibility; this function performs no validation of its own.
 * -------------------------------------------------------------------------- */
static uint64_t rtc_to_ms(const rtc_time_t *t, uint32_t ssr)
{
    uint32_t days;
    uint64_t ms;

    days = rtc_days_from_civil((uint32_t)t->year, (uint32_t)t->month, (uint32_t)t->day);

    ms  = (uint64_t)days * RTC_MS_PER_DAY;
    ms += (uint64_t)t->hour   * RTC_MS_PER_HOUR;
    ms += (uint64_t)t->minute * RTC_MS_PER_MINUTE;
    ms += (uint64_t)t->second * RTC_MS_PER_SECOND;
    /* The fraction is computed in 32-bit ON PURPOSE. The numerator's worst case is
       255 x 1000 = 255 000, which fits uint32_t by a factor of 16 000, and dividing
       a 32-bit value by 256 compiles to a shift. Written in 64-bit the same
       expression needs libgcc's __aeabi_uldivmod (~600 B) — a routine with no
       business living in a boot path for a value that never exceeds 999. */
    ms += (uint64_t)(((uint32_t)(BSP_RTC_PREDIV_S - ssr) * 1000U)
                     / (uint32_t)BSP_RTC_SSR_HZ);
    return ms;
}


/* ==========================================================================
 * PUBLIC API
 * ========================================================================== */

/**
 * @brief  Phase 1 — start the LSE, then get out of the way.
 * @param  none
 * @return none. Side effects: PWR kernel clock enabled, PWR_CR.DBP set and
 *         confirmed, RCC_BDCR.LSEON set. No GPIO write, no LSEBYP write, no
 *         RTCSEL write, no RTC register touched, no wait longer than the DBP
 *         read-back.
 * @note   Runs BEFORE the announce and the presence gate on purpose: the crystal's
 *         own startup time then overlaps boot work that already exists instead of
 *         becoming a wait at the end of the path (§5.1). By the time
 *         bsp_rtc_init() is reached, the oscillator has already had the announce
 *         plus the presence gate (up to ~2.3 s) to stabilise.
 * @note   DBP is confirmed before LSEON is written because a write into the backup
 *         domain while DBP is clear is IGNORED by hardware. Without the confirm, a
 *         later "no LSE" reading would point at the crystal when the real cause was
 *         backup-domain write protection (§2.3: no silent failures).
 */
void bsp_rtc_lse_start(void)
{
    (void)rtc_dbp_enable();

    /* RCC_BDCR.LSEON (bit 0) = the LSE oscillator on PC14/PC15. LSEBYP (bit 2) is
       never written: 0 = crystal mode, and bypass mode would mean an external
       32.768 kHz drive this board does not have (§2.2). SET_BIT, never a
       whole-register write, so RTCSEL/RTCEN and everything else in BDCR survive
       the call. */
    SET_BIT(RCC->BDCR, RCC_BDCR_LSEON);
}

/**
 * @brief  Phase 2 — bounded LSE confirmation, RTC clock selection, prescaler
 *         programming or preservation, and the re-synchronisation wait.
 * @param  none
 * @return none — including on every failure path. §7.3 makes "invalid" a state,
 *         not a fault to retry, so this function never halts and never loops.
 * @note   Call after the presence gate, before the super-loop. The arithmetic
 *         against the 5.45 s watchdog floor is written at the call site in main.c.
 */
void bsp_rtc_init(void)
{
    uint32_t t0;
    uint32_t sel;
    uint8_t  magic_ok;
    uint8_t  inits_set;

    t0 = bsp_tim2_now_us();

    /* 1. Bounded LSE confirmation (§5.1). "Already ready" is the common case here
       and costs one register read, because phase 1 started the oscillator ~2.3 s
       of boot time ago. */
    if (rtc_wait_lserdy(RTC_LSE_BOOT_BUDGET_MS) == 0U)
    {
        s_lse_startup_ms = UINT32_MAX;   /* never became ready inside the budget */
        return;   /* gate stays INVALID; §7.3's fallback chain decides what time to use */
    }
    s_lse_startup_ms = bsp_tim2_elapsed_us(t0) / 1000U;

    if (rtc_dbp_enable() == 0U)
    {
        return;   /* backup domain still write-protected: RTCSEL/RTCEN cannot be written */
    }

    /* 2. Clock source: LSE, and only LSE (§2.2 — "LSI belongs to IWDG and must
       never feed the RTC"). RTCSEL is writable while RTCEN is clear, which is why
       it is handled here and not after the enable. Three possible states:
         0              -> a fresh backup domain that was never configured: program LSE.
         already LSE     -> the previous session's choice, kept.
         LSI or HSE/32   -> foreign. The honest response is to stop and leave the
                            evidence in RCC_BDCR (0x40002370 over SWD), because
                            changing RTCSEL requires a backup-domain reset, and that
                            erases the calendar AND the §7.3 magic together. Choosing
                            to do that is §7.3 item 4 = app/logcore, not BSP. */
    sel = RCC->BDCR & RCC_BDCR_RTCSEL;
    if (sel == 0U)
    {
        SET_BIT(RCC->BDCR, RCC_RTCCLKSOURCE_LSE);  /* RTCSEL = 01 = LSE (stm32f4xx_hal_rcc.h:254) */
    }
    else if (sel != (uint32_t)RCC_RTCCLKSOURCE_LSE)
    {
        return;
    }

    /* RCC_BDCR.RTCEN (bit 15): from here the RTC peripheral has a kernel clock and
       its registers mean something. Before this, reads of RTC->ISR/PRER/BKP0R
       return values that cannot be interpreted — which is why bsp_rtc_gate()
       checks RTCEN before it believes "RSF set". */
    SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);

    /* 3. Program the prescaler, or preserve what is already there.
       The condition is §7.3 read from the other side: a domain that still holds
       the magic AND reports a configured calendar (INITS clear) is the VBAT case,
       and the thing worth protecting is exactly what re-entering initialization
       mode would overwrite. A fresh or never-initialised domain needs the frozen
       127/255 written before anything it produces can be trusted. */
    magic_ok  = (RTC->BKP0R == BSP_RTC_MAGIC) ? 1U : 0U;   /* RTC_BKP0R (0x40002850) */
    inits_set = ((RTC->ISR & RTC_ISR_INITS) != 0U) ? 1U : 0U;   /* RTC_ISR.INITS (bit 4) */

    if ((magic_ok == 0U) || (inits_set != 0U))
    {
        if (rtc_enter_init_mode() == 0U)
        {
            return;   /* INITF never asserted (or SHPF blocked it): the gate reads the truth back */
        }

        /* RTC_PRER in one write: PREDIV_A = 127 (bits 22:16) -> F1 = 32768/128 = 256 Hz,
           PREDIV_S = 255 (bits 14:0) -> 256/(255+1) = 1.000000 Hz calendar tick.
           §7's chain, transcribed; the constant is BSP_RTC_PRER_VALUE = 0x007F00FF. */
        RTC->PRER = BSP_RTC_PRER_VALUE;

        /* RTC_CR.FMT (bit 6) = 0 -> 24-hour format (the "24h format" of step 6).
           RTC_CR.BYPSHAD (bit 5) = 0 -> the shadow registers ARE used, which is
           what makes §7.1's TR->SSR->DR order meaningful; BYPSHAD = 1 would give
           §7.1 unshadowed reads and turn the double read into a hope. */
        RTC->CR &= ~(RTC_CR_FMT | RTC_CR_BYPSHAD);

        if (RTC->PRER != BSP_RTC_PRER_VALUE)
        {
            /* Read-back mismatch means the write never landed: RTC_WPR (0x40002824,
               reset value 0xFF = unprotected) or a missing DBP is where to look, and
               both are readable over SWD without adding another variable. Leaving
               initialization mode with a wrong prescaler would run a calendar at the
               wrong rate, so this returns and lets the gate report INVALID. */
            rtc_exit_init_mode();
            return;
        }

        rtc_exit_init_mode();

        /* TODO: verify against bench and cite the RM0090 RTC chapter (§10.2.1 P5 is
           still open). INITS is cleared on exit only if the calendar registers were
           fully programmed, and step 6 deliberately does NOT write a calendar it has
           no source for. The expected bench state after a power cut with no VBAT is
           therefore RSF set, INITS still set, BKP0R without the magic, and
           bsp_rtc_gate() == 0 — §7.3's "clock invalid" branch, which is precisely
           what bsp_rtc_set_utc_ms() exists to fix. */
        (void)rtc_wait_rsf();
    }
    else
    {
        /* VBAT kept the backup domain powered: PRER, the calendar and the magic all
           belong to the previous session. Touching any of them here would replace a
           known-good clock with a half-written one, which is the failure §7.3 exists
           to prevent. */
        (void)rtc_wait_rsf();
    }
}


/**
 * @brief  §7.3 validity gate at boot — the "year 2000 rock" gate.
 * @param  none
 * @return 1 = clock valid, 0 = clock invalid; also published to s_rtc_gate.
 *         Caches nothing: the answer is computed from live registers on every
 *         call, so the state machine of step 17 may re-evaluate it after an event
 *         (a VBAT change, a host time set) without a reset.
 * @note   BOOT-context today (main.c calls it once, before the super-loop). The
 *         §7.1 single-owner rule is what protects these reads once the producer
 *         exists, and this function is not the producer.
 */
uint8_t bsp_rtc_gate(void)
{
    uint32_t   isr;
    rtc_raw_t  raw;
    rtc_time_t t;
    uint8_t    ok = 1U;

    /* ACCESS PRECONDITIONS, cheapest first. Strictly stricter than §7.3's list and
       never laxer: with the LSE not running, or RTCSEL foreign, or RTCEN clear, the
       RTC registers carry no interpretable information at all, so believing "RSF
       set" without these three would be a claim about a read that cannot mean
       anything. Each check is on its own field so a failure names itself. */
    if ((RCC->BDCR & RCC_BDCR_LSERDY) == 0U)                                    ok = 0U;  /* bit 1  */
    if ((RCC->BDCR & RCC_BDCR_RTCSEL) != (uint32_t)RCC_RTCCLKSOURCE_LSE)        ok = 0U;  /* bits 9:8 = 01 (§2.2) */
    if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0U)                                     ok = 0U;  /* bit 15 */

    /* §7.3 item 1: RTC_ISR.INITS / RSF. */
    isr = RTC->ISR;                                                       /* 0x4000280C */
    if ((isr & RTC_ISR_RSF) == 0U)                                        ok = 0U;  /* bit 5: shadow regs not synced with APB1 */
    if ((isr & RTC_ISR_INITS) != 0U)                                      ok = 0U;  /* bit 4: calendar never fully configured */
    if ((isr & (RTC_ISR_INIT | RTC_ISR_INITF)) != 0U)                    ok = 0U;  /* bits 7/6: not allowed to be inside init mode */

    /* §7.3 item 2: the magic. Absent = the backup domain lost power, so nothing the
       calendar fields say is trustworthy, whatever they read as. */
    if (RTC->BKP0R != BSP_RTC_MAGIC)                                      ok = 0U;  /* 0x40002850 */

    /* §7.1 + §7.3 item 3: one accepted double read, decodable as legal BCD, and
       inside the 2026..2099 window. Chained rather than independent because
       decoding an unaccepted snapshot is exactly the mistake §7.1 exists to stop. */
    if (rtc_snapshot(&raw) == 0U)                                         ok = 0U;
    else if (rtc_decode(&raw, &t) == 0U)                                  ok = 0U;
    else if (rtc_plausible(&t) == 0U)                                     ok = 0U;

    s_rtc_gate = ok;
    return ok;
}


/**
 * @brief  Milliseconds since 1970-01-01 00:00:00 UTC from an accepted §7.1 read.
 * @param  none
 * @return the accepted value, or the last accepted value if every read of this
 *         call was torn, or 0 if nothing has ever been accepted. Published to
 *         s_rtc_now_ms either way.
 * @note   Only meaningful when bsp_rtc_gate() returned 1; this function does not
 *         replace the gate and does not decide anything the gate decides.
 */
uint64_t bsp_rtc_now_ms(void)
{
    rtc_raw_t  raw;
    rtc_time_t t;
    uint64_t   ms;
    uint32_t   attempt;

    /* §7.1's loop, kept in its own shape: up to three double reads, and a failed
       compare means "skip this anchor" rather than "use the mixed value anyway".
       s_rtc_torn_cnt counts COMPARE failures only — the torn-read signature — so
       it stays a number that means one thing when the producer's
       anchor_reject_cnt of §7.1 is added beside it later. */
    for (attempt = 1U; attempt <= RTC_TORN_RETRY_MAX; attempt++)
    {
        if (rtc_snapshot(&raw) == 0U)
        {
            s_rtc_torn_cnt++;
            continue;
        }

        /* A legal BCD decode inside the §7.3 window is required before a number is
           produced. This is not counted as "torn" because nothing was torn: it is
           the §7.3 plausibility question, and it is already visible there as
           s_rtc_gate == 0, which is the cheaper and more honest place to see it. */
        if (rtc_decode(&raw, &t) == 0U)
        {
            continue;
        }
        if (rtc_plausible(&t) == 0U)
        {
            continue;
        }

        ms = rtc_to_ms(&t, raw.ssr & RTC_SSR_SS);   /* (255 - SSR) * 1000 / 256, integer only */

        s_rtc_last_good_ms = ms;
        s_rtc_last_good_ok = 1U;
        s_rtc_now_ms       = ms;
        return ms;
    }

    /* §7.1's other half: "else: skip this anchor". For a boot-time reader that
       becomes "return the last accepted value, else 0" — never a half-converted
       value from a snapshot that failed its own compare. */
    ms = (s_rtc_last_good_ok != 0U) ? s_rtc_last_good_ms : 0ULL;
    s_rtc_now_ms = ms;
    return ms;
}

/**
 * @brief  Program the calendar from a millisecond timestamp and stamp the §7.3
 *         magic. Host/bench path only — main.c does not call it.
 * @param  ms  Milliseconds since 1970-01-01 00:00:00 UTC.
 * @return none. Writes nothing at all (and specifically no magic) on any rejected
 *         input or any failed step, so a failed set leaves the next boot reading
 *         INVALID instead of a clock that looks trustworthy.
 * @note   THREAD CONTEXT ONLY: the LSERDY wait here is 2 s, which the §5.1 boot
 *         budget cannot pay for.
 */
void bsp_rtc_set_utc_ms(uint64_t ms)
{
    uint64_t   days64, rem;
    uint32_t   days, y, m, d;
    uint32_t   hour, minute, second, ms_in_sec, ssr;
    uint32_t   dr, tr, sel;
    uint16_t   y_off;

    if (rtc_wait_lserdy(RTC_LSE_SET_BUDGET_MS) == 0U)
    {
        return;   /* no clock source to write against */
    }
    if (rtc_dbp_enable() == 0U)
    {
        return;   /* backup domain write protection is still on */
    }

    sel = RCC->BDCR & RCC_BDCR_RTCSEL;
    if (sel == 0U)
    {
        SET_BIT(RCC->BDCR, RCC_RTCCLKSOURCE_LSE);   /* LSE only, §2.2 */
    }
    else if (sel != (uint32_t)RCC_RTCCLKSOURCE_LSE)
    {
        return;   /* same foreign-source refusal as bsp_rtc_init(): see its comment */
    }
    SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);

    /* Reject before writing anything: a date outside the §7.3 window cannot be
       encoded in the two BCD year digits anyway, and writing a wrapped year would
       hand the next boot a calendar that the gate must then reject. */
    days64 = ms / RTC_MS_PER_DAY;
    if (days64 > (uint64_t)0xFFFFFFFFU)
    {
        return;   /* beyond what the 32-bit day count can represent */
    }
    days = (uint32_t)days64;

    rtc_civil_from_days(days, &y, &m, &d);

    if (y < BSP_RTC_YEAR_BASE)
    {
        return;
    }
    y_off = (uint16_t)(y - BSP_RTC_YEAR_BASE);
    if ((y_off < BSP_RTC_YEAR_MIN) || (y_off > BSP_RTC_YEAR_MAX))
    {
        return;   /* §7.3 window: 2026..2099 */
    }
    if ((m < 1U) || (m > 12U) || (d < 1U) || (d > 31U))
    {
        return;
    }
    /* Round-trip guard: the inverse is only trusted if running it forward lands on
       the same day number. If it does not, an off-by-one in either direction would
       have been written into the silicon as a wrong date, so this refuses instead. */
    if (rtc_days_from_civil(y, m, d) != days)
    {
        return;
    }

    rem       = ms % RTC_MS_PER_DAY;
    hour      = (uint32_t)(rem / RTC_MS_PER_HOUR);
    rem      -= (uint64_t)hour * RTC_MS_PER_HOUR;
    minute    = (uint32_t)(rem / RTC_MS_PER_MINUTE);
    rem      -= (uint64_t)minute * RTC_MS_PER_MINUTE;
    second    = (uint32_t)(rem / RTC_MS_PER_SECOND);
    ms_in_sec = (uint32_t)(rem % RTC_MS_PER_SECOND);

    /* The inverse of §7.1's fraction term: SSR counts PREDIV_S -> 0 inside the
       second, so the value that represents "ms_in_sec already elapsed" is
       255 - (ms_in_sec * 256 / 1000). ms_in_sec <= 999 -> the term is <= 255, so
       ssr stays inside [0, 255] by construction. */
    ssr = BSP_RTC_PREDIV_S - ((ms_in_sec * (uint32_t)BSP_RTC_SSR_HZ) / 1000U);

    /* BCD packing straight into the pinned field positions (stm32f405xx.h:10129-10170
       for RTC_DR, :10086-10128 for RTC_TR). Every tens digit here is <= 9 by the
       range checks above, so no field can be written with a non-BCD value. */
    dr = (((uint32_t)(y_off / 10U)) << RTC_DR_YT_Pos)   /* RTC_DR.YT bits 23:20 */
       | (((uint32_t)(y_off % 10U)) << RTC_DR_YU_Pos)   /* RTC_DR.YU bits 19:16 */
       | (((uint32_t)(m / 10U))     << RTC_DR_MT_Pos)   /* RTC_DR.MT bit 12     */
       | (((uint32_t)(m % 10U))     << RTC_DR_MU_Pos)   /* RTC_DR.MU bits 11:8  */
       | (((uint32_t)(d / 10U))     << RTC_DR_DT_Pos)   /* RTC_DR.DT bits 5:4   */
       | (((uint32_t)(d % 10U))     << RTC_DR_DU_Pos);  /* RTC_DR.DU bits 3:0   */

    tr = (((uint32_t)(hour / 10U))   << RTC_TR_HT_Pos)  /* RTC_TR.HT bits 21:20 */
       | (((uint32_t)(hour % 10U))   << RTC_TR_HU_Pos)  /* RTC_TR.HU bits 19:16 */
       | (((uint32_t)(minute / 10U)) << RTC_TR_MNT_Pos) /* RTC_TR.MNT bits 14:12 */
       | (((uint32_t)(minute % 10U)) << RTC_TR_MNU_Pos) /* RTC_TR.MNU bits 11:8 */
       | (((uint32_t)(second / 10U)) << RTC_TR_ST_Pos)  /* RTC_TR.ST bits 6:4   */
       | (((uint32_t)(second % 10U)) << RTC_TR_SU_Pos); /* RTC_TR.SU bits 3:0   */

    if (rtc_enter_init_mode() == 0U)
    {
        return;
    }

    RTC->PRER = BSP_RTC_PRER_VALUE;             /* 0x007F00FF: 127 / 255 (§7) */
    RTC->CR  &= ~(RTC_CR_FMT | RTC_CR_BYPSHAD); /* 24-hour, shadow registers in use */
    RTC->DR   = dr;                             /* calendar first: DR is what releases the shadow lock on read */
    RTC->TR   = tr;
    RTC->SSR  = ssr;

    rtc_exit_init_mode();

    if (rtc_wait_rsf() == 0U)
    {
        return;   /* registers never re-synchronised: the write is not trustworthy, so no magic */
    }

    /* Magic LAST, on purpose. It is the claim that the calendar behind it is good;
       writing it before the calendar would survive a partial write as a lie. */
    RTC->BKP0R = BSP_RTC_MAGIC;

    s_rtc_last_good_ok = 0U;   /* the cached reader value belongs to the old clock */
    s_rtc_last_good_ms = 0ULL;
}




