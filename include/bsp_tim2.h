/**
 ******************************************************************************
 * @file    bsp_tim2.h
 * @brief   TIM2 = the firmware supervisor: a free-running 1 MHz microsecond
 *          counter, a 1 kHz compare tick, and the 10 ms / 1 s flags that later
 *          supervisors and the IWDG feed are built on.
 *          (§2.2 time base · §3.2.5 priority · §3.2.7 liveness flag · §5.1 feed)
 *
 * TWO 1 ms BASES COEXIST ON PURPOSE — DO NOT MERGE THEM.
 *   SysTick is the HAL time base: HAL_InitTick() arms it at 1 ms and
 *   SysTick_Handler() in main.c is all it ever does, because HAL_Delay() and
 *   HAL_GetTick() read nothing else. That base belongs to HAL.
 *   TIM2 is the strategy's supervisor, transcribed from §2.2 verbatim: "TIM2:
 *   PSC=83 → 1 MHz free-running 32-bit counter (wrap 4295 s), CH1 compare →
 *   1 kHz supervisor tick. All elapsed-time arithmetic uses unsigned 32-bit
 *   subtraction (wrap-safe)." TIM2 owns every elapsed-µs claim in the firmware
 *   (§3.2.7 "now − t_last_end_us > 12 ms", the 27.2 s MAN cadence of §4.3, the
 *   ODR window of §7), the 1 kHz/10 ms/1 Hz flags, and the progress heartbeat
 *   the IWDG feed is taken from (§5.1 "a live 1 Hz state tick").
 *   Two bases, two jobs: merging them would put every HAL timeout behind the
 *   supervisor's own counters, and those counters are what the watchdog trusts.
 *
 * WHY TIM2 AND NOT SysTick FOR THIS JOB (§2.2, §3.2.5).
 *   32-bit, so a microsecond elapsed-time read never needs a software extension
 *   and a wrap is 4294.97 s away rather than 71.6 s; a compare channel, so the
 *   tick is drift-free (CCR1 += 1000, not "reload 1000 again after doing work");
 *   and a real NVIC priority (7, §3.2.5) instead of the fixed lowest-priority
 *   SysTick, so the supervisor is not starved by a HAL callback. SysTick stays
 *   exactly where §3.2.5 puts it: 15, the floor (TICK_INT_PRIORITY = 0x0F,
 *   framework-stm32cubef4 .../Inc/stm32f4xx_hal_conf.h:147).
 *
 * WHAT IS DELIBERATELY NOT HERE YET.
 *   The consumers of the flags: §3.2.7's producer-liveness poll (step 10), the
 *   sampling supervisor (§3.2.5, step 9+), the 1 s VREFINT median batch (§6.2,
 *   step 7), the state tick that §5.1 names as a feed condition (step 17).
 *   This module raises flags and counts microseconds; it takes no decision and
 *   knows of no state — `bsp_*` is "HAL/LL only, no policy"
 *   (Docs/00-ARCHITECTURE.md:171).
 ******************************************************************************
 */

#ifndef BSP_TIM2_H
#define BSP_TIM2_H

#include <stdint.h>   /* uint8_t / uint32_t — C99, no C++ (§2.3 coding rules) */

/**
 * @brief  Arm TIM2 as the free-running µs counter and the 1 kHz supervisor.
 * @param  none
 * @return none. Side effects: enables the TIM2 kernel clock, writes TIM2 CR1 /
 *         PSC / ARR / CCMR1 / CCR1 / CCER / DIER / SR, sets CR1.CEN, and enables
 *         TIM2_IRQn in the NVIC at preempt priority 7 (§3.2.5).
 * @note   CALL ONCE, AFTER SystemClock_Config(), BEFORE bsp_iwdg_init().
 *         "Configuration only in BOOT" (Docs/00-ARCHITECTURE.md:186): PSC/ARR
 *         are frozen by §2.2, and re-arming resets CNT to 0, which would rewind
 *         every elapsed-time claim the producer has already made.
 * @note   Why before the IWDG: §5.1's feed condition is the 1 Hz flag this
 *         function starts producing. If the order were reversed, the boot path
 *         between arming the watchdog and the first tick would be unprotected.
 * @note   Survives STOP (§6.3) without re-init: STOP gates the APB1 timer clock
 *         but does not lose TIM2's register content, so the wake path of step
 *         17 restores the clock tree only and CNT resumes where it stopped.
 *         TODO: verify against bench at the §6.3 step.
 */
void bsp_tim2_init(void);

/**
 * @brief  Read the free-running counter: microseconds since bsp_tim2_init().
 * @return TIM2->CNT, 1 count = 1 µs exactly (84 MHz / (PSC 83 + 1), §2.2).
 * @note   One 32-bit load is an atomic snapshot: TIM2 is a 32-bit timer and CNT
 *         is one register, so no high/low latch sequence and no interrupt mask
 *         is needed. Legal from thread and from ISR context.
 * @note   The epoch is bsp_tim2_init(), NOT power-on: HAL's TIM_Base_SetConfig()
 *         ends with TIMx->EGR = TIM_EGR_UG (stm32f4xx_hal_tim.c:6818), which
 *         reloads the prescaler and clears CNT. Never compare this value across
 *         a re-init for that reason.
 */
uint32_t bsp_tim2_now_us(void);

/**
 * @brief  Wrap-safe elapsed time, the only form §2.2 allows.
 * @param  t0  A value returned earlier by bsp_tim2_now_us().
 * @return (now - t0) mod 2^32, in µs.
 * @note   Unsigned subtraction is wrap-correct for ANY t0 as long as the true
 *         interval is < 2^32 µs = 4294.97 s. §2.2: "All elapsed-time arithmetic
 *         uses unsigned 32-bit subtraction (wrap-safe)". No signed arithmetic,
 *         no counter extension, no 64-bit accumulator.
 */
uint32_t bsp_tim2_elapsed_us(uint32_t t0);

/**
 * @brief  Supervisor tick count since bsp_tim2_init().
 * @return number of 1 kHz compare matches, wrapping at 2^32 ms ≈ 49.71 days.
 * @note   The counter the sampling supervisor compares the captured-sample count
 *         (the C domain of §1.3.7) against (§3.2.5). It is a tick count, not a
 *         timestamp: for elapsed time use bsp_tim2_elapsed_us(), which resolves
 *         to 1 µs instead of 1 ms and cannot lag by more than a tick.
 */
uint32_t bsp_tim2_tick_1khz(void);

/**
 * @brief  Take the 10 ms flag: return it and clear it (read-and-clear).
 * @return 1 if at least one 10 ms period elapsed since the last take, else 0.
 * @note   This is §3.2.7's normative trigger: "TIM2's 1 kHz supervisor raises a
 *         10 ms flag; in app (thread) context the logger reads FIFO_SRC
 *         (through §3.2.6)…". The poll itself is step 10 and lives in
 *         drv_lis3dsh, not here — this module only raises and hands over.
 * @note   Latched, not pulsed: set every 10 ms, cleared only by this call, so a
 *         thread that was busy for 35 ms still sees exactly one flag and runs
 *         the poll once. Callers time the condition with
 *         bsp_tim2_elapsed_us(), never by counting flags.
 */
uint8_t bsp_tim2_take_10ms_flag(void);

/**
 * @brief  Take the 1 Hz flag: return it and clear it (read-and-clear).
 * @return 1 if at least one second elapsed since the last take, else 0.
 * @note   Today this is §5.1's "a live 1 Hz state tick" feed condition, and the
 *         super-loop in main.c feeds the IWDG from it. From step 17 the state
 *         machine consumes the same flag as its 1 Hz transition tick (LED codes,
 *         the VDD medians of §6.2, the 1 Hz card poll of §8) and remains the
 *         feed owner — feeding is consumer-only per §5.1, never from an ISR.
 */
uint8_t bsp_tim2_take_1hz_flag(void);

#endif /* BSP_TIM2_H */

