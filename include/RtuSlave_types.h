#ifndef MODBUS_RTU_TYPES_H
#define MODBUS_RTU_TYPES_H

#include "Rtu_conf.h"
#include "stdint.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    RTU_OK,
    RTU_ERR,
    RTU_PERMISS_ERR, // Insufficient permissions
    RTU_READ_HOLD_REG,
    RTU_WRITE_HOLD_REG, // Write Hold Register
    RTU_READ_COIL,      // Read Ciol
    RTU_WRITE_COIL,     // 写线圈
    RTU_READ_INPUT_REG,
    RTU_NOACTIVE,
} RTU_Sta_t;

typedef enum
{
    RTU_PERMISS_OR, // only read
    RTU_PERMISS_RW, // read and write
} RTU_Permiss_t;

typedef enum
{
    RTU_EX_NONE = 0x00,
    RTU_EX_ILLEGAL_FUNC = 0x01,  // 不支持该功能
    RTU_EX_ILLEGAL_ADDR = 0x02,  // 地址不在范围内
    RTU_EX_ILLEGAL_VALUE = 0x03, // 数量或数值非法
    RTU_EX_SLAVE_FAILURE = 0x04, // 内部处理出错
    RTU_EX_SLAVE_BUSY = 0x06,    // 设备正忙
} RTU_ExceptionCode_t;

typedef enum
{
    /** Def Coils */
    RTU_FUNC_READ_COILS = 0x01,           // 读线圈
    RTU_FUNC_MULTIPLE_WRITE_COILS = 0x0F, // 写多个线圈
    RTU_FUNC_WRITE_SINGLE_COILS = 0x05,   // 写单个线圈

    /** Def Hold Register */
    RTU_FUNC_READ_HOLD_REGS = 0x03,     // 读保持寄存器
    RTU_FUNC_WRITE_SINGLE_REG = 0x06,   // 写单个保持寄存器
    RTU_FUNC_MULTIPLE_WRITE_REG = 0x10, // 写多个保持寄存器

    /** Def Input Register */
    RTU_FUNC_READ_INPUT_REG = 0x04,

} RTU_FunctionCode_t;

typedef struct RTU_Register
{
    uint16_t address;
    uint8_t permiss;

    void *value;
    struct RTU_Register *next;
} RTU_Register_t;

typedef struct
{
    uint16_t addr;
    RTU_Permiss_t permiss;

    void *data;
} RTU_RegisterMap_t;

typedef struct
{
    bool ready;
    size_t len;
} RTU_GFrame_t;

typedef struct
{
    uint8_t id;
    uint8_t buf[RTU_DEFAULT_BUF_SIZE];
    uint16_t buf_size;

    volatile RTU_GFrame_t g_frame;

    RTU_Register_t *coils;       // coils / read and write
    RTU_Register_t *holdingRegs; // holding / read and write
    RTU_Register_t *inputRegs;   // input register / read only

} RTU_SlaveObj_t;

#ifdef __cplusplus
}
#endif

#endif
