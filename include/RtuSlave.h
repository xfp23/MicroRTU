/**
 * @file RTUSlave.h
 * @author xfp23
 * @brief 
 * @version 0.1
 * @date 2026-03-17
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#ifndef RTUSLAVE_H
#define RTUSLAVE_H

#include "RTUSlave_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the singleton RTU slave.
 *
 * Allocates internal object and buffer using calloc. No parameters needed for single-instance.
 *
 * @return RTU_OK on success, RTU_ERR on failure.
 */
extern RTU_Sta_t RTUSlave_Init(void);

/**
 * @brief Deinitialize the singleton RTU slave.
 *
 * Frees all allocated memory including register lists and internal buffer.
 */
extern void RTUSlave_Deinit(void);

/**
 * @brief Register coils (function 0x01).
 *
 * This copies the provided Map into internal linked-list nodes (uses calloc).
 *
 * @param Map Pointer to array of RTU_RegisterMap_t
 * @param regNum Number of entries in Map
 * @return RTU_OK on success, RTU_ERR on failure.
 */
extern RTU_Sta_t RTUSlave_RegisterCoils(RTU_RegisterMap_t *Map, size_t regNum);

/**
 * @brief Register holding registers (function 0x03).
 *
 * Same behavior as RegisterCoils.
 */
extern RTU_Sta_t RTUSlave_RegisterHoldReg(RTU_RegisterMap_t *Map, size_t regNum);

/**
 * @brief Register writable registers (functions 0x04).
 */
extern RTU_Sta_t RTUSlave_RegisterInputReg(RTU_RegisterMap_t *Map, size_t regNum);

/**
 * @brief Called by lower layers when bytes arrive.
 *
 * IMPORTANT: This function must NOT process frames. It only copies bytes into the internal
 * buffer and marks that a frame is present. The actual parsing & response happens in
 * RTUSlave_TimerHandler(), which must be called periodically (e.g. in a timer / main loop).
 *
 * If multiple calls happen before TimerHandler(), the latest call overwrites the internal buffer.
 *
 * @param data Pointer to incoming bytes (caller-owned)
 * @param len Length of incoming bytes
 */
extern void RTUSlave_ReceiveCallback(uint8_t *data, size_t len);

/**
 * @brief Periodic handler — parse & process any received frame.
 *
 * Should be called periodically (e.g. from a timer ISR or main loop). If a frame has been
 * received (via RTUSlave_ReceiveCallback), this will validate, handle function codes,
 * prepare responses, and call the weak RTU_Transmit() to send responses.
 *
 * @return RTU_Sta_t one of RTU_OK / RTU_ERR / RTU_READ_HOLD_REG / RTU_WRITE_HOLD_REG / RTU_READ_COIL
 */
extern RTU_Sta_t RTUSlave_TimerHandler(void);

/**
 * @brief Modify the slave ID.
 *
 * @param id New id (must be 1..254)
 * @return RTU_OK on success, RTU_ERR on invalid id or not initialized.
 */
extern RTU_Sta_t RTUSlave_Modifyid(uint8_t id);

/**
 * @brief Weak transmit function (user may override in their project).
 *
 * Default implementation does nothing; user should provide a strong symbol with same
 * signature to actually send bytes out.
 *
 * @param data pointer to bytes to send
 * @param size number of bytes to send
 * @return implementation-defined (0 for success by default)
 */
extern int __attribute__((weak)) RTU_Transmit(uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* RTUSLAVE_H */