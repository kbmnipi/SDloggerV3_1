/**
 ******************************************************************************
 * @file    drv_lis3dsh.h
 * @brief   Public surface of the LIS3DSH driver — currently ONLY the M0 SPI1
 *          presence gate (Docs/02-FIRMWARE-DESIGN.md §2.1 pins/mode, §3.1 step 2,
 *          Docs/DS_lis3dsh.md §5.2 framing, §7.4 device ID).
 *
 * WHY A .c/.h PAIR (stated as the task asked).
 *   §00-ARCHITECTURE.md's layering table makes `drv_lis3dsh` a real module and the
 *   "sole owner of SPI1", so the bus and the register framing live in the .c and
 *   main() keeps only the policy (halt or continue). A single-file driver would
 *   push SPI1/GPIO details into main.c and leave main.c without a place for the
 *   §3.1 sequence that arrives next step.
 *
 * WHAT IS DELIBERATELY NOT HERE YET.
 *   §3.1 steps 1, 3..7 (STRT reset, CTRL_REG5, FIFO_CTRL, CTRL_REG6, CTRL_REG3,
 *   CTRL_REG4/ODR), self-test (§3.3), FIFO drain / mode B (§3.2), quiesce (§3.2.6),
 *   liveness poll (§3.2.7). The sensor is left in power-down: ODR is never started,
 *   so it cannot produce samples or INT1 edges during the bus probe.
 ******************************************************************************
 */

#ifndef DRV_LIS3DSH_H
#define DRV_LIS3DSH_H

#include <stdint.h>   /* uint8_t / uint32_t — C99, no C++ (§2.3 coding rules) */

/* --------------------------------------------------------------------------
 * §3.1 step 2 / [DS §7.4]: WHO_AM_I (0x0F) is a fixed 0x3F = 0011 1111.
 * It is the presence gate: any other value means the bus, the power, the
 * wiring or the part itself is wrong — there is no "close enough".
 * -------------------------------------------------------------------------- */
#define LIS3DSH_ID_WHO_AM_I       0x3FU

/* --------------------------------------------------------------------------
 * Return codes. Two, because at this step only two outcomes exist and each
 * needs a different bench reaction; a register value is data, not status, so
 * it is returned through a pointer and can never be confused with an error.
 * -------------------------------------------------------------------------- */
typedef enum
{
    DRV_LIS3DSH_OK      = 0,    /* transaction completed; *value is the register */
    DRV_LIS3DSH_ERR_SPI = 1     /* HAL rejected/timed out the transfer; no byte */
} drv_lis3dsh_status_t;

/**
 * @brief  Own and bring up SPI1 + the sensor's CS/INT1 pins (§2.1).
 * @return DRV_LIS3DSH_OK, or DRV_LIS3DSH_ERR_SPI if the peripheral refused to
 *         initialise. Never halts and never prints — reporting is the caller's
 *         policy (§00-ARCHITECTURE: `drv_*` knows nothing about files or LEDs).
 * @note   Side effects: enables GPIOA/GPIOB/SPI1 clocks, reconfigures PA4 (CS out,
 *         HIGH), PA5/PA6/PA7 (AF5), PB5 (input), writes SPI1 CR1/CR2.
 *         Idempotent: safe to call again before further traffic.
 */
drv_lis3dsh_status_t drv_lis3dsh_spi_init(void);

/**
 * @brief  Polled single-register read ([DS §5.2]: 0x80|addr then one data byte).
 * @param  addr   Register address, 0x00..0x7F (bits above the mask are ignored).
 * @param  value  Receives the byte read; only written when this returns OK.
 * @return DRV_LIS3DSH_OK or DRV_LIS3DSH_ERR_SPI (also on value == NULL).
 * @note   Single-threaded BOOT/super-loop use only: the transfer buffer is
 *         static (no heap, §2.3 rule 4), and no DMA stream is armed, so there is
 *         nothing to race with. That changes the day DMA2 S0/S3 are configured —
 *         see the §3.2.6 note inside drv_lis3dsh_read_reg().
 */
drv_lis3dsh_status_t drv_lis3dsh_read_reg(uint8_t addr, uint8_t *value);

/**
 * @brief  The M0 presence gate: read WHO_AM_I (0x0F), expect 0x3F.
 * @param  value  Receives the device ID byte.
 * @return DRV_LIS3DSH_OK / DRV_LIS3DSH_ERR_SPI (caller compares against
 *         LIS3DSH_ID_WHO_AM_I — the driver does not judge, it reports).
 */
drv_lis3dsh_status_t drv_lis3dsh_who_am_i(uint8_t *value);

/**
 * @brief  Number of failed SPI1 transactions since reset.
 * @return counter, 0 while the bus is healthy.
 * @note   "No silent failures" (.clinerules/03-execution-discipline.md): every
 *         error path at least counts. Read it over SWD when the LED says halt.
 */
uint32_t drv_lis3dsh_spi_error_count(void);

#endif /* DRV_LIS3DSH_H */
