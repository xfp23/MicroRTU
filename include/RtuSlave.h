/**
 * @file RTUSlave.h
 * @author xfp23
 * @brief Modbus RTU Slave Interface (User API)
 * @version 0.1
 * @date 2026-03-17
 *
 * @copyright Copyright (c) 2026
 */

#ifndef RTUSLAVE_H
#define RTUSLAVE_H

#include "RTUSlave_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTU_MAP_SIZEOF(x) (sizeof(x) / sizeof((x)[0]))

/**
 * @brief Initialize the Modbus RTU slave instance.
 *
 * This function allocates all required internal resources, including
 * the RTU object and frame buffer. Must be called before using any
 * other API in this module.
 *
 * @note This implementation uses a singleton design (only one instance).
 *
 * @return RTU_OK on success
 * @return RTU_ERR on failure (e.g. memory allocation failed)
 */
extern RTU_Sta_t RTUSlave_Init(void);

/**
 * @brief Deinitialize the Modbus RTU slave instance.
 *
 * Frees all dynamically allocated resources, including:
 * - Register linked lists
 * - Internal frame buffer
 *
 * After calling this function, the module must be re-initialized
 * before reuse.
 */
extern void RTUSlave_Deinit(void);

/**
 * @brief Register coil objects (bit-level, read/write).
 *
 * Used for Modbus function codes:
 * - 0x01 (Read Coils)
 * - 0x05 (Write Single Coil)
 * - 0x0F (Write Multiple Coils)
 *
 * Each entry in the map corresponds to one coil.
 *
 * @param Map Pointer to user-defined register map array
 * @param regNum Number of elements in the map
 *
 * @note
 * - Internal memory will be reallocated (previous registrations will be cleared)
 * - Map data is NOT copied; only pointers are stored
 *
 * @return RTU_OK on success
 * @return RTU_ERR on failure
 */
extern RTU_Sta_t RTUSlave_RegisterCoils(RTU_RegisterMap_t *Map, size_t regNum);

/**
 * @brief Register holding registers (16-bit, read/write).
 *
 * Used for Modbus function codes:
 * - 0x03 (Read Holding Registers)
 * - 0x06 (Write Single Register)
 * - 0x10 (Write Multiple Registers)
 *
 * @param Map Pointer to register map array
 * @param regNum Number of registers
 *
 * @note
 * - Each register must point to a valid 16-bit data variable
 * - Permissions (RO/RW) are respected during write operations
 *
 * @return RTU_OK on success
 * @return RTU_ERR on failure
 */
extern RTU_Sta_t RTUSlave_RegisterHoldReg(RTU_RegisterMap_t *Map, size_t regNum);

/**
 * @brief Register input registers (16-bit, read-only).
 *
 * Used for Modbus function code:
 * - 0x04 (Read Input Registers)
 *
 * @param Map Pointer to register map array
 * @param regNum Number of registers
 *
 * @note
 * - These registers are strictly read-only
 * - Any write attempt will be rejected automatically
 *
 * @return RTU_OK on success
 * @return RTU_ERR on failure
 */
extern RTU_Sta_t RTUSlave_RegisterInputReg(RTU_RegisterMap_t *Map, size_t regNum);

/**
 * @brief Receive raw Modbus RTU data from lower layer.
 *
 * This function should be called by the UART/RS485 driver when
 * a complete frame (or data chunk) is received.
 *
 * @warning
 * - This function DOES NOT parse or process the frame
 * - It only stores the data internally
 *
 * Actual frame parsing and response generation is handled in
 * RTUSlave_TimerHandler().
 *
 * @param data Pointer to received bytes
 * @param len Length of received data
 *
 * @note
 * - If called multiple times before processing, previous data is overwritten
 */
extern void RTUSlave_ReceiveCallback(uint8_t *data, size_t len);

/**
 * @brief Periodic processing handler.
 *
 * This function must be called periodically (e.g. in main loop or timer interrupt).
 *
 * Responsibilities:
 * - Detect complete frame
 * - Validate CRC
 * - Parse function code
 * - Access register map
 * - Generate response frame
 * - Call RTU_Transmit()
 *
 * @return RTU_OK on normal operation
 * @return RTU_ERR on failure
 * @return RTU_READ_HOLD_REG when a read holding register request is processed
 * @return RTU_WRITE_HOLD_REG when a write holding register request is processed
 * @return RTU_READ_COIL when a coil read is processed
 */
extern RTU_Sta_t RTUSlave_TimerHandler(void);

/**
 * @brief Set Modbus slave ID.
 *
 * @param id Slave address (valid range: 1 ~ 254)
 *
 * @return RTU_OK on success
 * @return RTU_ERR if ID is invalid or module not initialized
 */
extern RTU_Sta_t RTUSlave_Modifyid(uint8_t id);

/**
 * @brief Transmit Modbus RTU response (weak function).
 *
 * This function is declared as weak and should be overridden by the user
 * to connect with the actual hardware transmission (UART/RS485).
 *
 * Example:
 * @code
 * int RTU_Transmit(uint8_t *data, size_t size)
 * {
 *     UART_Send(data, size);
 *     return 0;
 * }
 * @endcode
 *
 * @param data Pointer to transmit buffer
 * @param size Number of bytes to send
 *
 * @return Implementation-defined (typically 0 for success)
 */
extern int RTU_Transmit(uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* RTUSLAVE_H */