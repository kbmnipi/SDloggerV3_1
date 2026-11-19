/**
 ******************************************************************************
 * @file    drv_lis3dsh.c
 * @brief   LIS3DSH on SPI1 — M0 presence gate (WHO_AM_I read), minimum slice.
 *
 * WHAT THIS FILE IS.
 *   The first touch of the sensor bus. It brings up exactly what one polled
 *   WHO_AM_I read needs and nothing more:
 *     - PA5/PA6/PA7 as SPI1 AF5, PA4 as software CS, PB5 as a plain input (§2.1)
 *     - SPI1 master, 8-bit, MSB first, mode 3 (CPOL=1/CPHA=1), 5.25 MHz (§2.1)
 *     - a 2-byte CS-framed read: 0x80|addr, then one clocked-out data byte.
 *       [DS §5.2]: the command byte is RW(bit7) + AD6..AD0, and there is NO
 *       MS / auto-increment bit inside it on this part — auto-increment can only
 *       come from CTRL_REG6.ADD_INC, which this step does not touch.
 *   §3.1 step 2 makes this read the presence gate, and the readback belongs in the
 *   container HEADER at 0x04D (Docs/01-CONTAINER-FORMAT.md:178) — a forensic
 *   record, not merely a boot check.
 *
 * AUDIT BUG-1 — CLOSED IN THIS FILE (M0 bench finding BF-1 explained, not re-described).
 *   The BF-1 signature (attempt 1 = 0xFF, attempt 2 = 0x3F, s_spi_err_cnt = 0, all
 *   four signals clean on the scope) is produced on the MCU side, not by the part:
 *   SCK carried an internal pull-DOWN, so it sat LOW for as long as SPI1_CR1.SPE = 0.
 *   HAL sets SPE only inside a transfer (stm32f4xx_hal_spi.c:1207-1212) and never
 *   clears it on the PIO path (:1269-1381, SPI_EndRxTxTransaction :3617-3658), and
 *   this driver asserts CS before calling it. So the 0 -> 1 SPE write drove SCK to
 *   its CPOL = 1 idle level *under a low CS*: a rising edge, and [DS §5.2:290]
 *   defines the rising edge as the CAPTURE edge. The part shifts its command byte by
 *   one bit, never decodes 0x8F, never drives SDO in the data phase, and the pull-up
 *   on PA6 reads 0xFF — while the master still runs its 16 clocks, so no OVR, no
 *   MODF, no timeout is even possible. Attempt 2 answered because SPE was already 1
 *   and no such transition existed any more. Two rules now hold the bus:
 *     1. PA5 carries an internal PULL-UP: SCK sits at the CPOL = 1 idle level even
 *        with SPE = 0, as [DS §5.2:289] requires ("SPC is held high while CS is
 *        high"). PA7 keeps its pull-down — SDI is data, not a clock.
 *     2. CR1.SPE is armed once in drv_lis3dsh_spi_init(), before any CS edge, so the
 *        only idle-level transition happens while CS is HIGH and the part is
 *        deselected. The pull-up removes the low state; the pre-arm removes the
 *        transition. Both are needed: neither one alone closes BF-1.
 *   The 3-attempt gate of §3.1 stays untouched: it is the measurement instrument for
 *   this fix, not part of it. Expected new bench result after the fix is 0x3F on
 *   attempt 1; 0xFF on attempt 1 again would REFUTE the mechanism and leave the
 *   interface-mode-switch explanation of §3.1:165-167 standing.
 *
 * AUDIT WARN-5 — CLOSED IN THIS FILE. lis3dsh_flush_peripheral() clears and re-sets
 *   SPE (__HAL_SPI_CLEAR_MODFFLAG ends by writing CR1.SPE = 0, stm32f4xx_hal_spi.h:
 *   417-428), i.e. it can re-create the BUG-1 edge from the error path. It now
 *   releases CS as its first action, so the ordering rule "re-arm SPE only with CS
 *   released" is enforced by the code instead of inherited from the call site.
 *
 * AUDIT WARN-6 — CLOSED IN THIS FILE. drv_lis3dsh_write_reg() reads the byte back and
 *   compares. A command byte has no room for a check field ([DS §5.2:296-313]), so a
 *   lost or shifted RW bit turns a write into a read that the part answers happily
 *   and the master can never distinguish from success. Read-back is the only probe.
 *   Still not called in this build — no register is configured yet.
 *
 * WHAT IT DELIBERATELY DOES NOT DO (scope discipline).
 *   No STRT reset, no CTRL_REG5 / FIFO_CTRL / CTRL_REG6 / CTRL_REG3 / CTRL_REG4
 *   (§3.1 steps 1, 3..7), no self-test (§3.3), no FIFO drain / mode B (§3.2), no
 *   DMA. ODR is never started, so the sensor stays in power-down and cannot drive
 *   INT1 or shift its FIFO while the wiring is still being proven.
 *
 * WHY HAL AND NOT LL/REGISTERS HERE.
 *   .clinerules/01-role.md: "SPI1 in the ISR hot path uses LL/register-level".
 *   This is BOOT / super-loop context, not the hot path: one 16-clock frame before
 *   any producer exists. HAL_SPI_TransmitReceive() is used because it owns the F4
 *   full-duplex enable / BSY / RXNE sequence correctly, which removes a class of
 *   first-byte and overrun bugs from a bring-up probe. The capture path that
 *   follows (EXTI + DMA2 S0/S3, §3.2.2..§3.2.6) is LL/register-level and replaces
 *   this call rather than sharing the peripheral with it.
 ******************************************************************************
 */

#include "drv_lis3dsh.h"

#include <stddef.h>          /* NULL */
#include "stm32f4xx_hal.h"   /* SPI_HandleTypeDef, GPIO_InitTypeDef, GPIO/HAL_RCC macros */

/* ==========================================================================
 * Pin contract — transcribed from §2.1 "Buses, pins"; the PCB is frozen.
 *   PA5 = SCK, PA6 = MISO, PA7 = MOSI, CS = PA4, INT1 = PB5.
 *   AF5 = SPI1/I2S1 on this pin group, corroborated by the ST-shipped BSP for the
 *   same silicon and the same sensor pins:
 *   framework-stm32cubef4/Drivers/BSP/STM32F4-Discovery/stm32f4_discovery.h:147-155
 *   (SPI1, GPIOA, GPIO_AF5_SPI1, PA5 / PA6 / PA7).
 * ========================================================================== */
#define SPI1_PIN_PORT         GPIOA          /* §2.1: all three SPI1 pins sit on GPIOA */
#define SPI1_SCK_PIN          GPIO_PIN_5     /* §2.1 SCK  -> AF5 */
#define SPI1_MISO_PIN         GPIO_PIN_6     /* §2.1 MISO -> AF5 (sensor SDO) */
#define SPI1_MOSI_PIN         GPIO_PIN_7     /* §2.1 MOSI -> AF5 (sensor SDI) */
#define SPI1_AF               GPIO_AF5_SPI1  /* RM0090 Table 9: AF5 = SPI1/I2S1 */

#define LIS3DSH_CS_PORT       GPIOA          /* §2.1: CS = PA4, software-managed */
#define LIS3DSH_CS_PIN        GPIO_PIN_4

#define LIS3DSH_INT1_PORT     GPIOB          /* §2.1: INT1 = PB5 (input here, no EXTI yet) */
#define LIS3DSH_INT1_PIN      GPIO_PIN_5

/* Speed of the AF pins. ST's GPIO speed modes are bounded by f_max: LOW = 2 MHz
   (f_max 5.5 MHz) is a hairline fit for the 5.25 MHz SCK of §2.1, so MEDIUM
   (f_max 25 MHz) is the slowest mode with real margin. HIGH (50 MHz) is available
   but §2.1's WARN-5 note (flying leads to an external sensor board) says prefer a
   slower edge over a higher BR. */
#define SPI1_AF_SPEED         GPIO_SPEED_FREQ_MEDIUM

/* --------------------------------------------------------------------------
 * [DS §5.2] SPI framing. Command byte: bit7 = RW (1 = read), bits6:0 = address.
 * A single transfer is 16 clock pulses: command byte + one data byte.
 * -------------------------------------------------------------------------- */
#define LIS3DSH_CMD_READ       0x80U  /* [DS §5.2] bit7 RW = 1 -> read                            */
#define LIS3DSH_ADDR_MASK      0x7FU  /* [DS §5.2] AD6..AD0; the byte has no room for an MS bit    */
#define LIS3DSH_CMD_WRITE      0x00U  /* [DS §5.2:303] bit7 RW = 0 -> write; the 7 address bits pass through unchanged */
#define LIS3DSH_REG_WHO_AM_I   0x0FU  /* [DS §6 register map / §7.4] device ID, fixed 0x3F         */
#define LIS3DSH_SPI_FRAME_LEN  2U     /* [DS §5.2] command byte + one data byte = 16 clocks        */

/* [DS §5.2] characterises CS setup = 6 ns and CS hold = 8 ns (10 MHz, 4-wire).
   One 84 MHz cycle is 11.9 ns, and the BSRR store alone already retires in >= 2
   cycles (~24 ns) before any NOP executes, so 4 NOPs (~48 ns) leaves >= 5x margin
   over the datasheet floor without needing a timer. It also makes the CS-to-clock
   gap repeatable while the leads are still flying (§2.1 WARN-5).
   TODO: verify against bench — [DS F8] states no minimum CS-HIGH time between
   frames is given; that floor is a scope measurement, not a code decision. */
#define LIS3DSH_CS_NOPS        4U

/* Transaction guard. The frame is 16 clocks at 5.25 MHz = 3.05 us, so 10 ms is
   ~3000x the worst case and still far below the 8 s IWDG window of step 5.
   HAL_MAX_DELAY is deliberately NOT used: an unpopulated or dead sensor must end
   in a reported halt, never in an unwatched hang (§2.3 "no silent failures").
   SysTick is already running at 84 MHz when this is called, so HAL_GetTick()-based
   HAL timeouts are live. */
#define LIS3DSH_SPI_TIMEOUT_MS  10U

/* Static-only storage (§2.3 rule 4: no malloc, no new, nothing dynamic).
   The frame buffers are file-scope rather than stack so that two callers cannot
   accidentally interleave two frames on one peripheral. */
static SPI_HandleTypeDef s_hspi1;      /* SPI1 handle; zero-initialised, HAL fills State/Lock */
static uint8_t           s_tx[LIS3DSH_SPI_FRAME_LEN];
static uint8_t           s_rx[LIS3DSH_SPI_FRAME_LEN];
static volatile uint32_t s_write_mismatch_cnt; /* WARN-6: writes that completed but whose read-back differed. volatile so -O2 keeps every store; read it over SWD, same role as s_lis3dsh_log[] in main.c */
static uint32_t          s_spi_err_cnt;  /* failed transactions since reset (bench-visible) */

static void lis3dsh_cs_select(void);
static void lis3dsh_cs_release(void);
static void lis3dsh_flush_peripheral(void);

/**
 * @brief  Assert CS (PA4 low) and pad the datasheet CS-setup floor.
 * @param  none.  @return none.  Side effect: PA4 low, then LIS3DSH_CS_NOPS NOPs.
 */
static void lis3dsh_cs_select(void)
{
    HAL_GPIO_WritePin(LIS3DSH_CS_PORT, LIS3DSH_CS_PIN, GPIO_PIN_RESET); /* PA4 low = selected (§2.1) */
    for (uint32_t n = 0U; n < LIS3DSH_CS_NOPS; n++)
    {
        __NOP();   /* >= 6 ns CS setup before the first clock edge ([DS §5.2]) */
    }
}

/**
 * @brief  Release CS (PA4 high) and pad the datasheet CS-hold floor.
 * @param  none.  @return none.  Side effect: PA4 high, then LIS3DSH_CS_NOPS NOPs.
 * @note   CS is released on EVERY exit path, including a HAL timeout: a sensor left
 *         selected with the clock idle high in mode 3 is a half-received frame and
 *         an undefined next byte.
 */
static void lis3dsh_cs_release(void)
{
    HAL_GPIO_WritePin(LIS3DSH_CS_PORT, LIS3DSH_CS_PIN, GPIO_PIN_SET);   /* PA4 high = deselected (§2.1) */
    for (uint32_t n = 0U; n < LIS3DSH_CS_NOPS; n++)
    {
        __NOP();   /* >= 8 ns CS hold after the last clock edge ([DS §5.2]) */
    }
}

/**
 * @brief  Put SPI1 back into a clean state after a FAILED transfer, so the next
 *         attempt measures the bus instead of the wreckage of the previous one.
 * @param  none.  @return none.
 * @note   Side effects: reads SPI1_DR then SPI1_SR, writes SPI1_SR, then clears
 *         and re-sets SPI1_CR1.SPE.
 * @note   This is §3.2.6 step 3 ("flush the peripheral: ... read DR to drain,
 *         clear CRCERR/OVR if set") moved onto the error path. It is not optional
 *         for a multi-attempt gate: a HAL_TIMEOUT leaves RXNE set with the last
 *         byte sitting in SPI1_DR, and that stale byte would otherwise be handed
 *         back as the answer of the NEXT attempt — which is exactly the failure
 *         mode §3.2.6 exists to prevent. Without this flush, "the second read
 *         worked" and "the first read poisoned the second" are indistinguishable.
 * @note   WARN-5 — CS MUST BE HIGH WHILE THIS RUNS, and the first line of this
 *         function makes that true instead of leaving it to the call site.
 *         __HAL_SPI_CLEAR_MODFFLAG ends by writing CR1.SPE = 0
 *         (stm32f4xx_hal_spi.h:417-428) and the SET_BIT at the end of this function
 *         writes it back to 1. Re-arming SPE is exactly the action that moves SCK to
 *         the CPOL = 1 idle level, and per [DS §5.2:290] the rising edge into that
 *         level is the CAPTURE edge: done under a low CS it eats one bit of the next
 *         command byte — the BUG-1 failure, re-created from the error path. With the
 *         PA5 pull-up in place the level no longer actually moves, so this guard is
 *         deliberately redundant today; it exists so the ordering rule survives any
 *         change in the pull configuration (an external pull, NOPULL, or an SPI1
 *         power-down in IDLE per §2.1:1054) and so a future §3.2.6 caller cannot get
 *         the order wrong. The same rule still has to be written into §3.2.6 itself
 *         (audit BUG-2) — Docs/ is not touched by this step.
 */
static void lis3dsh_flush_peripheral(void)
{
    lis3dsh_cs_release();   /* WARN-5 precondition, enforced by doing it: PA4 high, idempotent when already high */
    __HAL_SPI_CLEAR_OVRFLAG(&s_hspi1);     /* SPI1_DR read, then SPI1_SR read: drains the receive register and clears SPI_SR.OVR (RM0090 clear sequence) */
    __HAL_SPI_CLEAR_CRCERRFLAG(&s_hspi1);  /* SPI1_SR := ~SPI_SR_CRCERR; CRC is disabled, so this only guarantees no stale bit */
    __HAL_SPI_CLEAR_MODFFLAG(&s_hspi1);    /* SPI1_SR read, then SPI_CR1.SPE = 0 (MODF cannot set with CR1.SSI = 1, cleared anyway) */

    /* Re-arm the state machine. With SPE = 1 and no transfer in progress, SCK sits
       at the CPOL = 1 level, which is what [DS §5.2] asks for between frames:
       "SPC is held high while CS is high". */
    SET_BIT(SPI1->CR1, SPI_CR1_SPE);       /* SPI_CR1.SPE = 1 */
}

drv_lis3dsh_status_t drv_lis3dsh_spi_init(void)
{
    GPIO_InitTypeDef io = {0};   /* zero-filled: every field this step does not set is a defined 0 */

    /* --- Peripheral clocks (§2.1): GPIOA (bus + CS), GPIOB (INT1), SPI1 --------- */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();   /* SPI1 hangs off APB2 = 84 MHz (§2.2) */

    /* --- CS first, and HIGH before the mode switch (PA4) -----------------------
       GPIOA_ODR bit4 := 1 while the pin is still in its reset state (floating
       input), and only the GPIOA_MODER write below turns it into an output.
       HAL_GPIO_Init writes MODER/OTYPER/SPEED/PUPDR but never ODR, so the level
       set here survives the switch: the pin becomes an output already high and
       never emits a low pulse that would select the sensor. A momentary select is
       not cosmetic — in mode 3 the sensor counts whatever SCK edges follow as part
       of a frame. Register: GPIOA_ODR.ODR4 = 1. Read-modify-write is safe here:
       single core, and no ISR touches GPIOA at this point in bring-up. */
    LIS3DSH_CS_PORT->ODR |= (uint32_t)LIS3DSH_CS_PIN;

    io.Pin   = LIS3DSH_CS_PIN;      /* PA4 (§2.1) */
    io.Mode  = GPIO_MODE_OUTPUT_PP; /* push-pull; SPI1 CR1.SSM = 1, so CS is pure GPIO */
    io.Pull  = GPIO_NOPULL;         /* actively driven in both directions */
    io.Speed = GPIO_SPEED_FREQ_LOW; /* CS moves once per frame, not per bit: slow edge = less EMI (§2.1 WARN-5) */
    HAL_GPIO_Init(LIS3DSH_CS_PORT, &io);
    HAL_GPIO_WritePin(LIS3DSH_CS_PORT, LIS3DSH_CS_PIN, GPIO_PIN_SET);  /* §2.1: initial state = deselected */

    /* --- SCK (PA5) as AF5, with an internal PULL-UP ----------------------------
       BUG-1 fix. [DS §5.2:289]: "SPC is held high while CS is high" — at CPOL = 1
       the idle clock level IS the high level, so a pull-DOWN here held SCK LOW for
       as long as SPI1_CR1.SPE = 0. HAL only raises SPE inside a transfer
       (stm32f4xx_hal_spi.c:1207-1212) and never clears it on the PIO path
       (:1269-1381, SPI_EndRxTxTransaction :3617-3658), and this driver asserts CS
       before calling it: the 0 -> 1 SPE write therefore drove SCK high *under a low
       CS*, which [DS §5.2:290] defines as the CAPTURE edge. The part shifts its
       command byte by one bit, never decodes 0x8F, never drives SDO in the data
       phase, and the PA6 pull-up reads 0xFF — with the master finishing its 16
       clocks, so OVR / MODF / timeout cannot even be reported. That is the BF-1
       signature in full, including "attempt 2 answered".
       A pull-up makes the required idle level hold before the peripheral drives the
       pin. GPIO_PUPDR is written by HAL_GPIO_Init in AF mode too, so the pull is
       live while SPE = 0. The pull-up removes the low state; the SPE pre-arm below
       removes the transition — both are needed.
       TODO: verify against bench — "PUPDR stays active in AF mode" and "SPE = 1
       drives SCK to the CPOL level" are RM0090 statements (precondition P5,
       Docs/03-TEST-PLAN.md:80) argued here from observed behaviour, not from the PDF. */
    io.Pin       = SPI1_SCK_PIN;                  /* PA5 alone (§2.1) — never in the same call as PA7 */
    io.Mode      = GPIO_MODE_AF_PP;               /* alternate-function push-pull */
    io.Pull      = GPIO_PULLUP;                   /* SCK held at the CPOL = 1 idle level even with SPE = 0 ([DS §5.2:289]) */
    io.Speed     = SPI1_AF_SPEED;                 /* 5.25 MHz needs more than LOW's 5.5 MHz ceiling */
    io.Alternate = SPI1_AF;                       /* AF5 = SPI1/I2S1 (RM0090 Table 9) */
    HAL_GPIO_Init(SPI1_PIN_PORT, &io);

    /* --- MOSI / SDI (PA7) as AF5, with a pull-DOWN -----------------------------
       A separate HAL_GPIO_Init call on purpose: SCK and MOSI need OPPOSITE pulls,
       and one call applies one io.Pull to every bit of io.Pin — driving both pins
       from a single call is precisely what made the wrong pull possible. SDI is
       data, not a clock: holding it low while SPE = 0 keeps the sensor's input quiet
       and cannot be mistaken for a clock edge by a part that is not being clocked. */
    io.Pin       = SPI1_MOSI_PIN;                 /* PA7 (§2.1) */
    io.Mode      = GPIO_MODE_AF_PP;               /* alternate-function push-pull */
    io.Pull      = GPIO_PULLDOWN;                 /* SDI quiet before SPE = 1 (intent unchanged) */
    io.Speed     = SPI1_AF_SPEED;                 /* 5.25 MHz needs more than LOW's 5.5 MHz ceiling */
    io.Alternate = SPI1_AF;                       /* AF5 = SPI1/I2S1 (RM0090 Table 9) */
    HAL_GPIO_Init(SPI1_PIN_PORT, &io);

    /* --- MISO (sensor SDO) as AF5, with a pull-up ------------------------------
       [DS §5.2] notes the sensor's SDO carries an internal pull-up when idle. With
       an MCU-side pull-up as well, an UNPOPULATED socket reads a solid 0xFF and the
       presence gate fails loudly; a floating MISO could sample noise and, unlucky
       at best, land on 0x3F. A presence gate must not be able to pass by luck. */
    io.Pin       = SPI1_MISO_PIN;                 /* PA6 (§2.1) */
    io.Pull      = GPIO_PULLUP;
    io.Alternate = SPI1_AF;
    HAL_GPIO_Init(SPI1_PIN_PORT, &io);

    /* --- INT1 (PB5) as a plain input; EXTI arrives with step 10 (§3.2.7) --------
       Deliberately NOPULL: §2.1 makes "scope INT1 idle/active level" an M0 gate and
       [DS E8] leaves the polarity unproven (CTRL_REG3.IEA is only written at §3.1
       step 6), so choosing a pull direction here would encode an assumption the
       strategy has not licensed. This pin feeds no decision in this build. */
    io.Pin       = LIS3DSH_INT1_PIN;              /* PB5 (§2.1) */
    io.Mode      = GPIO_MODE_INPUT;
    io.Pull      = GPIO_NOPULL;
    io.Speed     = GPIO_SPEED_FREQ_LOW;
    io.Alternate = 0U;                            /* unused in INPUT mode; cleared so no stale AF lingers in the struct */
    HAL_GPIO_Init(LIS3DSH_INT1_PORT, &io);

    /* --- SPI1: master, full duplex, 8-bit, MSB first, mode 3, 5.25 MHz (§2.1) ---
       HAL F4 1.28.3 has no NSSPMode / TIFrameDelay member in SPI_InitTypeDef
       (verified in stm32f4xx_hal_spi.h of the pinned package), so there is nothing
       else to disable and no field to invent. */
    s_hspi1.Instance          = SPI1;                       /* §2.1 dedicated SPI1 — never shared with the card */
    s_hspi1.Init.Mode         = SPI_MODE_MASTER | SPI_CR1_SSI; /* master; SSI holds the internal NSS asserted, which is what makes SSM = 1 immune to MODF */
    s_hspi1.Init.Direction    = SPI_DIRECTION_2LINES;       /* full duplex: MOSI out and MISO in at once */
    s_hspi1.Init.DataSize     = SPI_DATASIZE_8BIT;          /* 8-bit frames; 16 clocks per 1-byte read ([DS §5.2]) */
    s_hspi1.Init.CLKPolarity  = SPI_POLARITY_HIGH;          /* SPI_CR1.CPOL = 1 -> SCK idles high */
    s_hspi1.Init.CLKPhase     = SPI_PHASE_2EDGE;            /* SPI_CR1.CPHA = 1 -> SPI mode 3, mandatory ([DS §5.2]) */
    s_hspi1.Init.NSS          = SPI_NSS_SOFT;               /* SPI_CR1.SSM = 1; CS is PA4 GPIO (§2.1) */
    s_hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16; /* APB2 84 MHz / 16 = 5.25 MHz (§2.1, under the 10 MHz limit) */
    s_hspi1.Init.FirstBit     = SPI_FIRSTBIT_MSB;           /* MSB first ([DS §5.2]) */
    s_hspi1.Init.TIMode       = SPI_TIMODE_DISABLE;         /* no TI mode (it would want hardware CS pulsing) */
    s_hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE; /* hardware CRC off: the container CRC (§4, App. A.3) is software */
    s_hspi1.Init.CRCPolynomial = 7U;                        /* SPI_CRCCALCULATION_DISABLE means CRCPR is unused; 7 is its reset value */

    if (HAL_SPI_Init(&s_hspi1) != HAL_OK)
    {
        /* Reachable only through an invalid parameter or a stuck HAL lock — the
           peripheral clock is already enabled above. There is no §3.1 step that can
           be executed against a peripheral that refused to configure, so this is
           reported, not patched over. */
        s_spi_err_cnt++;
        return DRV_LIS3DSH_ERR_SPI;
    }

    /* --- Arm SPE before the first CS edge (BUG-1, second half of the fix) ------
       With CR1.SPE = 1 and no transfer in progress the peripheral drives SCK to its
       CPOL = 1 idle level, and CR1.SSI = 1 (SPI_MODE_MASTER,
       stm32f4xx_hal_spi.h:207) keeps the internal NSS asserted, so an idle enabled
       master cannot raise MODF. The idle-level transition therefore happens HERE,
       while PA4 is HIGH and the part is deselected ([DS §5.2:289]) — and never while
       CS is low, where [DS §5.2:290] would make it a capture edge and cost the next
       command byte one bit.
       HAL_SPI_TransmitReceive() then takes its "already enabled" branch
       (stm32f4xx_hal_spi.c:1207-1212) and writes no SPE transition of its own; its
       PIO 8-bit path never disables SPE (:1269-1381 and SPI_EndRxTxTransaction
       :3617-3658), so this single arm holds for every later BOOT frame. Anything
       that does clear SPE — the flush above, HAL's DMA-abort paths (:3084), a future
       IDLE power-down of SPI1 (§2.1:1054) — must re-arm under the same rule: CS
       high. Register: SPI1_CR1.SPE = 1. */
    SET_BIT(SPI1->CR1, SPI_CR1_SPE);   /* SPI_CR1.SPE = 1 -> SCK at the CPOL idle level before any CS edge */

    return DRV_LIS3DSH_OK;
}

drv_lis3dsh_status_t drv_lis3dsh_read_reg(uint8_t addr, uint8_t *value)
{
    HAL_StatusTypeDef hal_status;

    if (value == NULL)
    {
        /* Defensive: a dropped byte is a lost fact, so refuse rather than read into
           nothing. Counted, because "no silent failures" is a rule and not a
           suggestion (.clinerules/03-execution-discipline.md). */
        s_spi_err_cnt++;
        return DRV_LIS3DSH_ERR_SPI;
    }

    /* §3.2.6 quiesce — NOT executed here, and NOT needed at this step.
       §3.2.6 is normative "before every polled SPI access" once DMA2's SPI1 streams
       are configured, because a polled byte read issued while a DMA stream owns the
       peripheral returns the last byte the DMA put into SPI1->DR. In this build no
       DMA2 stream exists at all and SPI1_CR2.RXDMAEN / TXDMAEN are 0 as they are
       after reset, so DR cannot be holding a stale DMA byte.
       The day SPI1_RX = DMA2 Stream0/CH3 and SPI1_TX = DMA2 Stream3/CH3 are armed
       (§2.1 V3.5 B1, §3.2.2), this function must run §3.2.6 steps 1..3 before
       lis3dsh_cs_select() and step 5 after lis3dsh_cs_release(). */
    s_tx[0] = (uint8_t)(LIS3DSH_CMD_READ | (addr & LIS3DSH_ADDR_MASK)); /* 0x8F for WHO_AM_I 0x0F ([DS §5.2]) */
    s_tx[1] = 0xFFU;   /* dummy: the sensor only needs clocks on the data phase; what we send is irrelevant */

    lis3dsh_cs_select();

    /* One full-duplex call is exactly 16 SCK pulses with CS held low throughout,
       which is the "single transfer = 16 clock pulses" of [DS §5.2]. */
    hal_status = HAL_SPI_TransmitReceive(&s_hspi1, s_tx, s_rx,
                                        (uint16_t)sizeof(s_tx),
                                        LIS3DSH_SPI_TIMEOUT_MS);

    lis3dsh_cs_release();   /* every path, including timeout (see that function's note) */

    if (hal_status != HAL_OK)
    {
        /* Bring-up causes in likelihood order: nothing on the bus (note that a
           missing sensor still completes the electrical transfer, so this branch is
           mostly HAL_TIMEOUT — BSY never clearing, i.e. no SCK activity — or
           HAL_ERROR from OVR/MODF). Counted and returned; the caller makes it an LED. */
        s_spi_err_cnt++;
        lis3dsh_flush_peripheral();   /* §3.2.6 step 3: never let a dead frame decide the next one */
        return DRV_LIS3DSH_ERR_SPI;
    }

    /* s_rx[0] is the reply to the command byte = don't-care. The register content
       arrives during the dummy clock, i.e. in s_rx[1] ([DS §5.2]: second byte is
       DO7..DO0). */
    *value = s_rx[1];
    return DRV_LIS3DSH_OK;
}

/**
 * @brief  Write one register and VERIFY it by reading it back — §3.1 transport for
 *         steps 1 and 3..7, closing audit WARN-6.
 * @param  addr  Register address, 0x00..0x7F (bits above the mask are ignored).
 * @param  data  The byte to store.
 * @return DRV_LIS3DSH_OK only if the write frame completed AND a read of the same
 *         address afterwards returned exactly `data`. DRV_LIS3DSH_ERR_SPI on a failed
 *         transfer, on a failed read-back frame, or on a read-back mismatch.
 * @note   Side effects: 2 x (CS low, 16 clocks, CS high) — one write frame and one
 *         read frame; SPI1_DR/SR traffic, s_tx/s_rx, s_spi_err_cnt and, on a
 *         mismatch, s_write_mismatch_cnt. Nothing else.
 * @note   WHY THE READ-BACK IS NOT OPTIONAL. A command byte is RW(bit7) + AD6..AD0
 *         with no room for anything else ([DS §5.2:296-313]), so there is no
 *         checksum, no complement, no second byte to compare against on the wire. If
 *         a bit of that byte is shifted — the BUG-1 mechanism, or a corrupted frame
 *         later inside RECORDING — a write can be decoded as a READ: the part
 *         discards the value, drives SDO instead of latching it, and the master still
 *         completes its 16 clocks and gets HAL_OK with no OVR/MODF/timeout. A
 *         mis-decoded write is therefore INVISIBLE from the bus, and the only probe
 *         is to read the register back. §3.1 rule 5 (Docs/02-FIRMWARE-DESIGN.md:173)
 *         refuses a file of plausible zeros; an unverified CTRL_REG5 / FIFO_CTRL /
 *         CTRL_REG4 write is exactly how such a file is produced.
 * @note   NOT CALLED IN THIS BUILD. §3.1 steps 1 and 3..7 are the next sensor step;
 *         the function exists now so that step inherits the verification instead of
 *         rediscovering the need for it, and so the presence gate below keeps its
 *         read-only character.
 * @note   TODO: verify against bench — the equality test is valid for plain R/W
 *         registers only. Two §3.1 writes cannot be judged by `readback == data`:
 *         CTRL_REG3 = 0x01 is expected to SELF-CLEAR STRT (§3.1 step 1,
 *         Docs/02-FIRMWARE-DESIGN.md:143) and CTRL_REG6 BOOT clears ADD_INC / FIFO_EN
 *         ([DS F3], Docs/02-FIRMWARE-DESIGN.md:215-217). A masked comparison for those
 *         is a decision for the step that performs them, not a guess here.
 */
drv_lis3dsh_status_t drv_lis3dsh_write_reg(uint8_t addr, uint8_t data)
{
    HAL_StatusTypeDef hal_status;
    uint8_t           readback = 0U;

    /* Command byte: RW = 0, then the 7-bit address ([DS §5.2:303]). s_tx[1] is
       DI7..DI0 = the value being written. s_rx[] receives two don't-care bytes, and
       HAL requires a non-NULL receive buffer in full-duplex. */
    s_tx[0] = (uint8_t)(LIS3DSH_CMD_WRITE | (addr & LIS3DSH_ADDR_MASK));
    s_tx[1] = data;

    lis3dsh_cs_select();

    /* One call = 16 clock pulses with CS held low throughout, which is the "single
       transfer = 16 clock pulses" of [DS §5.2:292]. */
    hal_status = HAL_SPI_TransmitReceive(&s_hspi1, s_tx, s_rx,
                                        (uint16_t)sizeof(s_tx),
                                        LIS3DSH_SPI_TIMEOUT_MS);

    lis3dsh_cs_release();   /* every path, including timeout (see that function's note) */

    if (hal_status != HAL_OK)
    {
        s_spi_err_cnt++;
        lis3dsh_flush_peripheral();   /* CS is already high here; flush re-arms it as its first action anyway (WARN-5) */
        return DRV_LIS3DSH_ERR_SPI;
    }

    /* Second, separate frame: 0x80|addr then one clocked-out byte — the same framing
       as any read, so it inherits the §3.2.6 note written in drv_lis3dsh_read_reg():
       the day DMA2 S0/S3 are armed, that function is the one that has to quiesce. */
    if (drv_lis3dsh_read_reg(addr, &readback) != DRV_LIS3DSH_OK)
    {
        /* drv_lis3dsh_read_reg() has already counted its own failure. Counting the
           same bad frame twice would make s_spi_err_cnt describe a fiction instead of
           a number of frames. The write is unverified, and unverified is reported as
           a failure, never as a success. */
        return DRV_LIS3DSH_ERR_SPI;
    }

    if (readback != data)
    {
        /* The silent-write signature WARN-6 describes: electrically clean frames,
           HAL_OK, wrong register content. Counted in two places on purpose —
           s_spi_err_cnt keeps the transaction total honest, s_write_mismatch_cnt
           separates "a frame failed" from "the frame completed and the part stored
           something else", which is the distinction that points at the command byte
           rather than at the wiring. Read both over SWD. */
        s_spi_err_cnt++;
        s_write_mismatch_cnt++;
        return DRV_LIS3DSH_ERR_SPI;
    }

    return DRV_LIS3DSH_OK;
}

drv_lis3dsh_status_t drv_lis3dsh_who_am_i(uint8_t *value)
{
    return drv_lis3dsh_read_reg(LIS3DSH_REG_WHO_AM_I, value);
}

uint32_t drv_lis3dsh_spi_error_count(void)
{
    return s_spi_err_cnt;
}
