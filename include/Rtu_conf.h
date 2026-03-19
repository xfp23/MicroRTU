#ifndef __RTU_SLAVE_CONFIG_H__
#define __RTU_SLAVE_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif


#define RTU_VERSION "2.1.3"


/* ============================================================
 * Frame buffer configuration
 * ============================================================
 */

/**
 * @brief Maximum size of a single Modbus RTU frame buffer (in bytes)
 *
 * This buffer is used for both RX and TX.
 * 
 * Notes:
 * - Must be large enough to hold the largest possible response frame.
 * - Example:
 *   Read Holding Registers (125 regs):
 *     1 (ID) + 1 (FUNC) + 1 (BYTE COUNT) + 250 (DATA) + 2 (CRC) = 255 bytes
 *
 * Recommended:
 * - 256 bytes for standard Modbus usage
 * - Increase if you plan custom extensions
 */
#ifndef RTU_DEFAULT_BUF_SIZE
#define RTU_DEFAULT_BUF_SIZE    (256U)
#endif

/* ============================================================
 * Register capacity configuration
 * ============================================================
 */

/**
 * @brief Maximum number of coil registers (bit type)
 *
 * Used for function code:
 * - 0x01 (Read Coils)
 *
 * Each coil is stored as a separate node internally.
 */
#ifndef RTU_MAX_COILS
#define RTU_MAX_COILS           (128U)
#endif


/**
 * @brief Maximum number of holding registers (read-only)
 *
 * Used for function code:
 * - 0x03 (Read Holding Registers)
 *
 * Each register is 16-bit.
 */
#ifndef RTU_MAX_HOLD_REGS
#define RTU_MAX_HOLD_REGS       (128U)
#endif


/**
 * @brief Maximum number of writable registers
 *
 * Used for function codes:
 * - 0x06 (Write Single Register)
 * - 0x10 (Write Multiple Registers)
 *
 * Each register is 16-bit.
 */
#ifndef RTU_MAX_INPUT_REGS
#define RTU_MAX_INPUT_REGS      (128U)
#endif


#ifdef __cplusplus
}
#endif

#endif /* __RTU_SLAVE_CONFIG_H__ */