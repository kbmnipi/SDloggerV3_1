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
 */
static void lis3dsh_flush_peripheral(void)
{
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

    /* --- SCK + MOSI as AF5, held low while SPI1 is still disabled -------------- */
    io.Pin       = SPI1_SCK_PIN | SPI1_MOSI_PIN;  /* PA5 + PA7 (§2.1) */
    io.Mode      = GPIO_MODE_AF_PP;               /* alternate-function push-pull */
    io.Pull      = GPIO_PULLDOWN;                 /* pulls stay active in AF mode: SCK/SDI quiet before SPE = 1 */
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

drv_lis3dsh_status_t drv_lis3dsh_who_am_i(uint8_t *value)
{
    return drv_lis3dsh_read_reg(LIS3DSH_REG_WHO_AM_I, value);
}

uint32_t drv_lis3dsh_spi_error_count(void)
{
    return s_spi_err_cnt;
}
