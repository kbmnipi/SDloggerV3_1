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
 * @brief  Write one register, then READ IT BACK and compare (audit WARN-6).
 * @param  addr  Register address, 0x00..0x7F (bits above the mask are ignored).
 * @param  data  The byte to store.
 * @return DRV_LIS3DSH_OK only when the write frame completed AND a following read of
 *         the same address returned exactly `data`. DRV_LIS3DSH_ERR_SPI on a failed
 *         transfer, on a failed read-back frame, or on a mismatch.
 * @note   WHY THE READ-BACK IS NOT OPTIONAL. The command byte is RW(bit7) + AD6..AD0
 *         and has no room for anything else ([DS §5.2:296-313]). If a bit of that
 *         byte is ever shifted — the BUG-1 mechanism, or a corrupted frame inside
 *         RECORDING — the part decodes the frame as a READ: the value is discarded,
 *         the part drives SDO instead of latching, HAL returns HAL_OK because the
 *         master completed its 16 clocks, and no error flag is set. A mis-decoded
 *         write is invisible from the bus, so verification is the only probe. In
 *         §3.1 steps 1/3..7 an unverified write means a campaign logged plausible
 *         zeros, which is exactly what §3.1 rule 5 refuses.
 * @note   Equality is meaningful for plain R/W registers only. CTRL_REG3 = 0x01 is
 *         expected to SELF-CLEAR STRT (§3.1 step 1, Docs/02-FIRMWARE-DESIGN.md:143)
 *         and CTRL_REG6.BOOT clears ADD_INC / FIFO_EN ([DS F3],
 *         Docs/02-FIRMWARE-DESIGN.md:215-217): those writes need a masked comparison,
 *         decided with the step that performs them. TODO: verify against bench.
 * @note   Driver prohibitions still apply to the CALLER: never write an address absent
 *         from the register map, never touch factory-calibration registers
 *         ([DS §6/§11], Docs/02-FIRMWARE-DESIGN.md:225-232). This function polices the
 *         frame, not the address map.
 * @note   Single-threaded BOOT / super-loop use only: it shares the static frame
 *         buffers with drv_lis3dsh_read_reg() and no DMA stream is armed — see the
 *         §3.2.6 note inside drv_lis3dsh_read_reg().
 */
drv_lis3dsh_status_t drv_lis3dsh_write_reg(uint8_t addr, uint8_t data);

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
