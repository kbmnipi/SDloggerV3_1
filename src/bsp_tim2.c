/**
 ******************************************************************************
 * @file    bsp_tim2.c
 * @brief   TIM2: the 1 MHz free-running µs counter and the 1 kHz supervisor tick
 *          of §2.2, the 10 ms flag §3.2.7 is built on, and the 1 Hz flag §5.1's
 *          feed condition is built on. Internal timebase — no pin, no DMA.
 *
 * WHAT THIS FILE IS.
 *   The transcription of one §2.2 line into registers: "TIM2: PSC=83 → 1 MHz
 *   free-running 32-bit counter (wrap 4295 s), CH1 compare → 1 kHz supervisor
 *   tick. All elapsed-time arithmetic uses unsigned 32-bit subtraction
 *   (wrap-safe)." Nothing more: no PWM, no pin mapping (§2.2 routes TIM2
 *   nowhere), no DMA, no slave mode, no RTOS hook. The producer of §3.2 and the
 *   consumer of §3.4 both read this counter; neither may configure it
 *   (Docs/00-ARCHITECTURE.md:186 — "TIM2 counter | free-running, read by anyone
 *   | configuration only in BOOT").
 *
 * WHY A COMPARE CHANNEL AND NOT THE UPDATE EVENT.
 *   A free-running 32-bit counter has ARR at its maximum, so its update event
 *   arrives once per 4294.97 s — by construction, the only 1 kHz source available
 *   on this counter is a compare match. That is why §2.2 pairs "free-running
 *   32-bit" with "CH1 compare" in a single line, and why the tick's period is
 *   CCR1's increment (1000 counts) rather than ARR.
 *
 * WHY OC1PE = 0 IS LOAD-BEARING, NOT COSMETIC.
 *   With the compare preload enabled, a CCR1 write goes to the preload buffer
 *   and transfers to the ACTIVE compare register only when the next match occurs
 *   (RM0090, TIM capture/compare). At the moment this ISR runs, CNT is already
 *   past the old compare value, so that "next match" cannot happen until the
 *   counter comes round — up to 4294.97 s later. With OC1PE = 1 the very first
 *   ISR therefore kills the supervisor for one full wrap. With OC1PE = 0 the ISR
 *   write lands in the active register immediately, which is the whole design:
 *   CCR1 advances by exactly 1000 counts per entry, so the tick is drift-free
 *   (it never accumulates the ISR's own entry latency, and the 1 Hz flag raised
 *   at tick 1000 / 2000 / 3000… is exactly 1 s apart).
 *
 * THE ONE HAZARD THIS DESIGN ACCEPTS, stated with numbers.
 *   If an entry is ever more than 1000 µs late, CCR1 += 1000 lands in the past
 *   and the supervisor stalls until the next wrap. The budget is wide: the ISR
 *   body is ~30-40 cycles (0.4-0.5 µs at 84 MHz), and per §3.2.5 the only vectors
 *   above it are PVD 4 ("set a flag, touch no peripheral"), SDIO + its DMA 5,
 *   SPI1 + its DMA + INT1 6 — every one budgeted in µs by §9 (the whole producer
 *   is 2.2 % typical). A stall therefore needs a ~2000x violation of the §9
 *   budget: a runaway higher ISR, not ordinary load.
 *   DETECTOR, cheap and unambiguous for the M0 bench pass: bsp_tim2_tick_1khz()
 *   must equal bsp_tim2_now_us() / 1000 at all times (±1). Equal proves §2.2's
 *   rate and this ISR together; unequal means the tick stalled.
 *   TODO: verify against bench.
 *
 * WHY NO .d1ram SECTION HERE.
 *   Every symbol below is a few bytes of .bss. The D1 placement rule (all
 *   DMA-visible buffers at 0x20000000…, never CCM) protects buffers that DMA2
 *   masters touch; nothing here is DMA-visible — TIM2 raises an interrupt and
 *   the CPU moves the bytes. §10.2.1 P4 (the .d1ram section + linker ASSERT) is
 *   also still open, so there is no section attribute to attach yet.
 ******************************************************************************
 */

#include "bsp_tim2.h"
#include "stm32f4xx_hal.h"   /* HAL_TIM + CMSIS registers; USE_HAL_DRIVER by builder */

/* ========================================================================== */
/* §2.2 numbers — transcribed from the frozen clock tree, not chosen.          */
/* ========================================================================== */

/* TIM2 hangs off APB1. §2.2 states the result directly: "AHB 84 · APB1 42
   (TIM2 kernel 84) · APB2 84". The reason APB1's 42 MHz becomes 84 MHz is the
   RCC rule in RM0090's clock tree: when an APB prescaler differs from 1, the
   timer clock feeding that bus is multiplied by 2. main.c:187 programs the /2
   that makes it true (RCC_HCLK_DIV2), and main.c's own guards already fail the
   build if the PLL constants stop producing 84 MHz. */
#define TIM2_KERNEL_HZ         84000000UL   /* §2.2: APB1 42 MHz x 2            */
#define TIM2_PSC                  83U       /* 84 / (83 + 1) = 1.000 MHz        */
#define TIM2_ARR          0xFFFFFFFFUL      /* free-running: wrap 4294.97 s     */
#define TIM2_TICK_US           1000UL       /* 1 kHz: 1 ms = 1000 x 1 µs        */
#define TIM2_TICKS_PER_10MS      10UL       /* §3.2.7 liveness window           */
#define TIM2_TICKS_PER_1HZ     1000UL       /* §5.1 "live 1 Hz state tick"      */
#define TIM2_IRQ_PREEMPT          7U        /* §3.2.5 priority plan             */

/* --------------------------------------------------------------------------
 * Build-time guards, same shape as main.c's PLL guards: §2.2 is frozen, and
 * the arithmetic must fail the compile rather than ship a wrong supervisor
 * rate. A wrong TIM2 rate is worse than a crash — every elapsed-time claim in
 * the firmware (§3.2.7's 12 ms, §4.3's 27.2 s cadence, §7's ODR window) would
 * be off by the same silent factor while the log kept looking plausible.
 * -------------------------------------------------------------------------- */
#if (TIM2_KERNEL_HZ != 84000000UL)
#error "TIM2 kernel is no longer the 84 MHz of 2.2: do not re-derive this module from a re-tuned clock tree."
#endif

#if (((TIM2_KERNEL_HZ / (TIM2_PSC + 1UL)) != 1000000UL) || \
     ((TIM2_KERNEL_HZ % (TIM2_PSC + 1UL)) != 0UL))
#error "TIM2_PSC no longer divides the kernel clock to exactly 1 MHz. Section 2.2 (PSC=83 -> 1 MHz) is frozen."
#endif

/* --------------------------------------------------------------------------
 * Static storage only — no malloc, no new (§2.3 rule 4).
 * -------------------------------------------------------------------------- */
static TIM_HandleTypeDef s_tim2;               /* HAL handle, BOOT-only config   */

/* Written by the ISR, taken (read-and-clear) by thread context. volatile
   because -O2 must not cache a flag across HAL_Delay() or hoist a clear out of
   the super-loop: the flag is the entire protocol between the two contexts. */
static volatile uint32_t s_tick_1khz;          /* 1 ms units, wraps at 49.71 d   */
static volatile uint8_t  s_flag_10ms;          /* §3.2.7 trigger, latched        */
static volatile uint8_t  s_flag_1hz;           /* §5.1 feed condition, latched   */

/* Prescalers, ISR-private: one writer, one reader, both the ISR — so neither
   volatile nor a lock is warranted. uint16_t is the smallest type that still
   holds TIM2_TICKS_PER_1HZ - 1 without a cast. */
static uint16_t s_div_10ms;
static uint16_t s_div_1hz;

/* Bench evidence, read over SWD (same idiom as s_spi_err_cnt in
   drv_lis3dsh.c). §2.3 "no silent failures": a void-returning init still
   counts what went wrong instead of pretending it did not. */
static volatile uint32_t s_tim2_err_cnt;
static volatile uint32_t s_tim2_kernel_hz_measured;

/**
 * @brief  Arm TIM2: 1 MHz free-running counter + CH1 compare at 1 kHz (§2.2).
 * @param  none
 * @return none — every failure path is counted in s_tim2_err_cnt instead.
 * @note   Side effects: RCC_APB1ENR.TIM2EN = 1; TIM2 PSC/ARR/CR1 (HAL), CCMR1,
 *         CCR1, CCER, SR, DIER; NVIC TIM2_IRQn priority 7 + enable; CR1.CEN = 1.
 *         NO GPIO register is touched — see the note on PA5 further down.
 * @note   FAILURE POLICY, stated because the API cannot report it (void). The
 *         HAL calls here can only fail through __HAL_LOCK/state misuse or a
 *         peripheral that refuses to initialise. If either does, the tick never
 *         starts, so the super-loop never sees a 1 Hz flag, never feeds, and the
 *         armed IWDG ends BOOT in a reset. That is deliberate, and it is not
 *         silent: s_tim2_err_cnt != 0 over SWD names the fault, while a
 *         running-but-mis-scaled supervisor would corrupt every interval in the
 *         log and still look like success (§2.3 "no silent failures").
 *         A kernel-clock MISMATCH alone does NOT stop the arming, for the
 *         symmetric reason: HAL's own tick and timeouts scale with the same
 *         error, so the board keeps blinking and the two SWD words
 *         (s_tim2_err_cnt, s_tim2_kernel_hz_measured) report the true ratio
 *         instead of the bench seeing an unexplained reset loop.
 */
void bsp_tim2_init(void)
{
    const uint32_t hclk_hz  = HAL_RCC_GetHCLKFreq();    /* §2.2: 84 MHz         */
    const uint32_t pclk1_hz = HAL_RCC_GetPCLK1Freq();   /* §2.2: 42 MHz (APB1)  */
    uint32_t kernel_hz;

    /* RCC_APB1ENR.TIM2EN — the kernel clock has to run before any TIM2 register
       write, or the APB1 write is ignored and the reads back as reset state. */
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* --- §2.2 consistency check (BOOT cost: 2 reads + 1 compare, no hot path).
       Rule under test: a timer clock is its APB clock DOUBLED when that APB's
       prescaler differs from 1 — §2.2 states the outcome as "APB1 42 (TIM2
       kernel 84)". If anyone edits main.c's APB1 divider, PSC = 83 stops
       meaning 1 µs and every number in this module is wrong by that ratio while
       still compiling cleanly. So: measure, record, compare, keep the evidence. */
    kernel_hz = (pclk1_hz == hclk_hz) ? pclk1_hz : (pclk1_hz * 2UL);
    s_tim2_kernel_hz_measured = kernel_hz;

    if (kernel_hz != TIM2_KERNEL_HZ)
    {
        s_tim2_err_cnt++;   /* §2.2's APB chain no longer matches this file     */
    }

    /* --- §2.2 time base: PSC = 83 → 1 MHz, ARR = max → free-running ---------
       CounterMode UP / CKD = DIV1: no clock-filter division is needed because no
       TI/ETR is used (no input capture, no slave mode), and CKD only affects the
       capture sampling filter and the tDTS-synchronised paths (RM0090 TIM_CR1).
       ARPE = 1 keeps the ARR shadow authoritative, so a stray later write cannot
       shorten the 4294.97 s wrap mid-run. RCR does not exist on TIM2. */
    s_tim2.Instance               = TIM2;
    s_tim2.Init.Prescaler         = TIM2_PSC;
    s_tim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_tim2.Init.Period            = TIM2_ARR;
    s_tim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_tim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    s_tim2.Init.RepetitionCounter = 0U;

    /* TIM_Base_SetConfig() writes ARR/PSC and ends with TIMx->EGR = TIM_EGR_UG
       (stm32f4xx_hal_tim.c:6812-6818): that loads the prescaler counter, copies
       the ARR shadow AND clears CNT — so the epoch of bsp_tim2_now_us() is this
       call, with no "first counted pulse skipped" uncertainty at CEN time. */
    if (HAL_TIM_Base_Init(&s_tim2) != HAL_OK)
    {
        s_tim2_err_cnt++;
        return;   /* no counter, no tick, therefore no feed: FAILURE POLICY above */
    }

    /* --- CH1 compare: the 1 kHz supervisor, register-level by design ---------
       Why not HAL_TIM_OC_ConfigChannel()? The one field that decides whether
       this design works at all is OC1PE = 0 (see "WHY OC1PE = 0 IS LOAD-BEARING"
       at the top of the file). It belongs beside the ISR instruction that
       depends on it, not inside a struct filled in a path the ISR never takes —
       and the HAL output-compare state machine would then have to be started for
       a channel that must never reach a pin (§2.3 rule 6 keeps the ISR free of
       HAL bookkeeping; .clinerules/03 requires register names on register code).

       NO GPIO WRITE: TIM2_CH1 can appear on PA0 / PA15 / PA5 through AF1. This
       module enables no GPIO clock and touches no MODER/AFR, so the channel
       stays internal (§2.2 "internal timebase only") — and PA5, SPI1 SCK per
       §2.1 with its BUG-1 pull-up (drv_lis3dsh.c), cannot be stolen by an AF1
       side effect. */
    CLEAR_BIT(TIM2->CCMR1,
              TIM_CCMR1_CC1S    /* 00  = output direction (not capture)          */
            | TIM_CCMR1_OC1M    /* 000 = frozen: OC1REF is never driven          */
            | TIM_CCMR1_OC1PE   /*   0 = NO preload: ISR CCR1 writes are live    */
            | TIM_CCMR1_OC1FE   /*   0 = no fast output path (no pin to drive)   */
            | TIM_CCMR1_OC1CE); /*   0 = not an internal trigger source          */

    WRITE_REG(TIM2->CCR1, TIM2_TICK_US);   /* first match: 1000 µs after CEN     */

    SET_BIT(TIM2->CCER, TIM_CCER_CC1E);    /* channel logic enabled. CC1P/CC1NE
                                              are meaningless with OC1M = frozen
                                              and no output routed anywhere.    */

    /* Every flag to zero, for two reasons: HAL's EGR.UG can leave UIF set, and a
       re-init must not inherit a pending CC1IF — an inherited one would make the
       first entry after NVIC enable an unearned tick (CCR1 += 1000 for a match
       that already happened), shifting the whole 1 kHz phase by one period. */
    WRITE_REG(TIM2->SR, 0U);

    SET_BIT(TIM2->DIER, TIM_DIER_CC1IE);   /* CC1IF -> NVIC TIM2 request. DIER is
                                              left with exactly this one bit set:
                                              no UIE, no DMA requests, so a CH1
                                              compare match is the ONLY event
                                              that can vector to this handler.  */

    /* --- NVIC: §3.2.5's plan is "… → TIM2 7 → KEY EXTI 7 → SysTick 15".
       HAL_Init() already selected NVIC_PRIORITYGROUP_4, which is exactly the
       "NVIC 4-bit preempt, 0 sub-priority" §3.2.5 assumes — that grouping is
       NOT re-set here (task rule: do not touch it), and HAL's tick stays at
       TICK_INT_PRIORITY 0x0F = 15 (stm32f4xx_hal_conf.h:147), the plan's floor.
       Sub-priority 0: the group has no sub-priority bits, so the argument is
       carried for API symmetry and has no effect. */
    HAL_NVIC_SetPriority(TIM2_IRQn, TIM2_IRQ_PREEMPT, 0U);
    NVIC_ClearPendingIRQ(TIM2_IRQn);      /* idempotent re-init: no free tick    */
    HAL_NVIC_EnableIRQ(TIM2_IRQn);

    /* CR1.CEN = 1 — the counter starts here, and with it the 1 ms until the
       first compare match. Nothing above this line can delay that first match by
       more than one instruction stream, because CNT only advances once CEN is
       set. HAL_TIM_Base_Start() touches nothing but CEN (stm32f4xx_hal_tim.c:
       HAL_TIM_Base_Start sets MR1/CCSE only for slave/one-pulse modes, neither
       of which is enabled on TIM2 here). */
    if (HAL_TIM_Base_Start(&s_tim2) != HAL_OK)
    {
        s_tim2_err_cnt++;   /* CEN refused: same consequence as the init failure */
    }
}

/**
 * @brief  TIM2 compare-match handler: the 1 kHz supervisor tick.
 * @param  none
 * @return none. Executes in Handler mode at 1 kHz (CCR1 match, §2.2).
 * @note   Contents are EXACTLY what .clinerules/03 allows an ISR here: clear the
 *         flag, advance the compare, count, set flags. No function call
 *         (HAL_TIM_IRQHandler would reach the callback layer and HAL_GetTick),
 *         no division, no float, no peripheral other than TIM2, and no reference
 *         of any kind to SDIO/FatFs or SPI1/FIFO (§2.3 hot-path rules). Body: 8
 *         register touches + 2 compares ≈ 30-40 cycles = 0.4-0.5 µs, i.e.
 *         0.04-0.05 % of the CPU at 1 kHz — inside the §9 budget it never
 *         mentions, which is the point of keeping it this small.
 * @note   Vector identity: TIM2_IRQHandler is the strong definition that wins
 *         over the weak startup entry — the same mechanism SysTick_Handler uses
 *         in main.c and for the same reason: without it the .isr_vector slot
 *         resolves to Default_Handler ("B .") and the tick never runs. No
 *         prototype is added here or in the header on purpose.
 */
void TIM2_IRQHandler(void)
{
    /* TIM2_SR.CC1IF — the request source. DIER has only CC1IE set
       (bsp_tim2_init), so a genuine entry always finds it. The test is
       defensive: a debugger write to ISER/ICER or a stale NVIC flop must never
       be able to invent a tick, because each invention would move CCR1 by
       1000 µs and every interval downstream (12 ms liveness, 27.2 s MAN, 1 s
       median) would inherit the shift. */
    if ((READ_REG(TIM2->SR) & TIM_SR_CC1IF) == 0U)
    {
        return;
    }

    /* TIM2_SR.CC1IF: writing 0 to the bit clears it, and the 1s written to every
       other flag position leave those flags untouched (RM0090 TIM_SR: CCxIF and
       CCxOF are cleared by writing 0, never by writing 1). Clear BEFORE
       advancing the compare so a match arriving during the rest of this ISR can
       never be acknowledged by mistake. */
    WRITE_REG(TIM2->SR, (uint32_t)~TIM_SR_CC1IF);

    /* TIM2_CCR1 += 1000: the next match is one period after THIS match, not one
       period after this ISR ran — that is what makes the tick drift-free, and it
       is only live because OC1PE = 0 (header note). Unsigned arithmetic on
       uint32_t is modulo 2^32 by definition, so the compare crosses the
       4294.97 s CNT wrap for free with no special case (§2.2 "wrap-safe
       unsigned subtraction" is the other half of the same rule). The 1000 µs
       entry-latency hazard and its SWD detector are documented at the top. */
    WRITE_REG(TIM2->CCR1, READ_REG(TIM2->CCR1) + TIM2_TICK_US);

    /* Supervisor count in 1 ms units. §3.2.5 compares this against the captured
       sample count (the C domain of §1.3.7); it is also what turns §7's ODR
       window into a count difference instead of a measurement. */
    s_tick_1khz++;

    /* 10 ms prescaler → §3.2.7's producer-liveness poll trigger. Compared
       against a constant, never a remainder: no division in an ISR (project
       rule). The counter is reset rather than allowed to wrap, and the flag is
       OR'd rather than incremented: a latched flag, so a thread that was busy
       for 35 ms sees one flag and runs the poll once — §3.2.7 times its 12 ms
       condition with bsp_tim2_elapsed_us(), never with flag arithmetic. */
    s_div_10ms++;
    if (s_div_10ms >= (uint16_t)TIM2_TICKS_PER_10MS)
    {
        s_div_10ms  = 0U;
        s_flag_10ms = 1U;
    }

    /* 1 Hz prescaler → §5.1's "a live 1 Hz state tick": today the super-loop's
       IWDG feed condition, from step 17 the state machine's own 1 Hz transition
       tick (LED codes, VDD medians of §6.2, 1 Hz card poll of §8). */
    s_div_1hz++;
    if (s_div_1hz >= (uint16_t)TIM2_TICKS_PER_1HZ)
    {
        s_div_1hz  = 0U;
        s_flag_1hz = 1U;
    }
}

/**
 * @brief  Read the free-running counter (microseconds since bsp_tim2_init()).
 * @param  none
 * @return TIM2_CNT, 1 count = 1 µs (§2.2).
 * @note   One 32-bit load of a 32-bit counter is an atomic snapshot: no
 *         high/low latch sequence and no interrupt mask is needed, which is what
 *         makes it legal from the producer's ISR as well as from the thread.
 */
uint32_t bsp_tim2_now_us(void)
{
    return READ_REG(TIM2->CNT);
}

/**
 * @brief  Wrap-safe elapsed time: (now - t0) mod 2^32, the only form §2.2 allows.
 * @param  t0  An earlier bsp_tim2_now_us() value.
 * @return Elapsed microseconds. Correct for any t0 as long as the true interval
 *         is under 4294.97 s, which is every interval this firmware measures.
 * @note   Not written out at the call sites on purpose: this module owns the one
 *         rule §2.2 permits for elapsed time, so a future edit cannot quietly
 *         turn it into signed or absolute arithmetic in some other file.
 */
uint32_t bsp_tim2_elapsed_us(uint32_t t0)
{
    return (uint32_t)(bsp_tim2_now_us() - t0);
}

/**
 * @brief  Supervisor tick count (1 kHz compare matches since init).
 * @param  none
 * @return s_tick_1khz — one volatile 32-bit read: atomic, and at most one tick
 *         stale if a compare match lands while it is being read.
 */
uint32_t bsp_tim2_tick_1khz(void)
{
    return s_tick_1khz;
}

/**
 * @brief  Take the 10 ms flag (§3.2.7 trigger), read-and-clear.
 * @param  none
 * @return 1 if at least one 10 ms period elapsed since the last take, else 0.
 * @note   The read and the clear form one critical section: without it, a set
 *         landing between them would be erased by the clear. The cost is
 *         __get_PRIMASK/__set_PRIMASK — two instructions, ~24 ns masked at
 *         84 MHz, three orders of magnitude below the µs-scale ISR latencies §9
 *         already budgets — and PRIMASK is RESTORED, not blindly re-enabled, so
 *         a caller that already masked interrupts stays masked.
 */
uint8_t bsp_tim2_take_10ms_flag(void)
{
    const uint32_t primask = __get_PRIMASK();
    uint8_t taken;

    __disable_irq();
    taken       = s_flag_10ms;
    s_flag_10ms = 0U;
    __set_PRIMASK(primask);

    return taken;
}

/**
 * @brief  Take the 1 Hz flag (§5.1 feed condition), read-and-clear.
 * @param  none
 * @return 1 if at least one second elapsed since the last take, else 0.
 * @note   Same critical section, same cost argument as bsp_tim2_take_10ms_flag().
 *         The consumer reads 1 as "the supervisor is alive", which is the
 *         progress §5.1 demands — not as permission to run a task list.
 */
uint8_t bsp_tim2_take_1hz_flag(void)
{
    const uint32_t primask = __get_PRIMASK();
    uint8_t taken;

    __disable_irq();
    taken      = s_flag_1hz;
    s_flag_1hz = 0U;
    __set_PRIMASK(primask);

    return taken;
}





