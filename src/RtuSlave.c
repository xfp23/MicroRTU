/**
 * @file RTUSlave.c
 * @author xfp23
 * @brief Single-instance Modbus RTU slave implementation
 * @version 0.1
 * @date 2026-03-16
 *
 * - 使用静态单例 rtu_obj
 * - 接收回调只写入缓冲并标记 ready
 * - 定时处理函数解析并响应（调用弱 RTU_Transmit）
 */

#include "RtuSlave.h"
#include "stdlib.h"
// #include "MicroKVTable.h"

/* Internal singleton */
static RTU_SlaveObj_t rtu_obj = {0};
static RTU_SlaveObj_t *const this = &rtu_obj;

/* --- CRC16 (Modbus) --- */
static uint16_t CRC16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t pos = 0; pos < len; pos++)
    {
        crc ^= (uint16_t)buf[pos];
        for (int i = 0; i < 8; i++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* Free a linked list and set head pointer to NULL */
static void rtufree_register_list(RTU_Register_t **headp)
{
    if (headp == NULL || *headp == NULL)
        return;

    RTU_Register_t *node = *headp;
    while (node)
    {
        RTU_Register_t *next = node->next;
        free(node);
        node = next;
    }
    *headp = NULL;
}

/* Build a linked list from Map and set *headp to first node.
 * Returns 0 on success, -1 on failure.
 *
 * NOTE: node->value points to the original map[i].data (no deep copy).
 */
static int rtubuild_register_list(RTU_Register_t **headp, RTU_RegisterMap_t *map, size_t count)
{
    if (headp == NULL)
        return -1;

    *headp = NULL;

    if (map == NULL || count == 0)
        return 0; // nothing to build

    RTU_Register_t *prev = NULL;
    for (size_t i = 0; i < count; ++i)
    {
        RTU_Register_t *node = (RTU_Register_t *)calloc(1, sizeof(RTU_Register_t));
        if (node == NULL)
        {
            /* free already allocated nodes */
            if (*headp)
                rtufree_register_list(headp);
            *headp = NULL;
            return -1;
        }

        node->address = map[i].addr;
        node->value = map[i].data;
        node->permiss = (uint8_t)map[i].permiss;
        node->next = NULL;

        if (prev)
            prev->next = node;
        else
            *headp = node;

        prev = node;
    }

    return 0;
}

/**
 * @brief 发送 Modbus 异常响应
 * @param func 原始请求的功能码
 * @param ex_code 异常码 (0x01 - 0x04)
 */
static void rtu_send_exception(uint8_t func, RTU_ExceptionCode_t ex_code)
{
    uint8_t *resp = this->buf; // 复用内部缓冲区
    uint16_t crc;

    resp[0] = this->id;
    resp[1] = func | 0x80;      // 功能码最高位置 1
    resp[2] = (uint8_t)ex_code; // 填充异常码

    // 计算这 3 个字节的 CRC
    crc = CRC16(resp, 3);
    resp[3] = (uint8_t)(crc & 0xFF);
    resp[4] = (uint8_t)(crc >> 8);

    // 异常帧长度固定为 5 字节
    RTUSlave_Transmit(resp, 5);
}

/* Initialize singleton */
RTU_Sta_t RTUSlave_Init(void)
{
    /* buf is static array; set buf_size accordingly */
    this->buf_size = (uint16_t)sizeof(this->buf);

    /* default id 1 */
    this->id = 1;

    /* ensure heads are empty (they are zeroed by static init, but be explicit) */
    this->coils = NULL;
    this->holdingRegs = NULL;
    this->inputRegs = NULL;

    this->g_frame.ready = false;
    this->g_frame.len = 0;

    return RTU_OK;
}

/* Deinitialize, free everything allocated */
void RTUSlave_Deinit(void)
{
    if (this->coils)
        rtufree_register_list(&this->coils);
    if (this->holdingRegs)
        rtufree_register_list(&this->holdingRegs);
    if (this->inputRegs)
        rtufree_register_list(&this->inputRegs);

    this->g_frame.ready = false;
    this->g_frame.len = 0;
}

/* Registration APIs */
RTU_Sta_t RTUSlave_RegisterCoils(RTU_RegisterMap_t *Map, size_t regNum)
{
    if (Map == NULL || regNum == 0 || regNum > RTU_MAX_COILS)
        return RTU_ERR;

    /* Free existing */
    if (this->coils)
        rtufree_register_list(&this->coils);

    if (rtubuild_register_list(&this->coils, Map, regNum) < 0)
        return RTU_ERR;

    return RTU_OK;
}

RTU_Sta_t RTUSlave_RegisterHoldReg(RTU_RegisterMap_t *Map, size_t regNum)
{
    if (Map == NULL || regNum == 0 || regNum > RTU_MAX_HOLD_REGS)
        return RTU_ERR;

    if (this->holdingRegs)
        rtufree_register_list(&this->holdingRegs);

    if (rtubuild_register_list(&this->holdingRegs, Map, regNum) < 0)
        return RTU_ERR;

    return RTU_OK;
}

RTU_Sta_t RTUSlave_RegisterWriteReg(RTU_RegisterMap_t *Map, size_t regNum)
{
    if (Map == NULL || regNum == 0 || regNum > RTU_MAX_INPUT_REGS)
        return RTU_ERR;

    for (size_t i = 0; i < regNum; i++)
    {
        Map[i].permiss = RTU_PERMISS_OR; // only supply read permission
    }

    if (this->inputRegs)
        rtufree_register_list(&this->inputRegs);

    if (rtubuild_register_list(&this->inputRegs, Map, regNum) < 0)
        return RTU_ERR;

    return RTU_OK;
}

/* Receive callback: only copy bytes into internal buffer and mark ready.
 * IMPORTANT: This function does NOT parse or respond; parsing happens in TimerHandler().
 *
 * If incoming len > buf_size, the data is truncated and frame_ready becomes true.
 *
 * Concurrency note:
 * - Current implementation uses volatile g_frame flags. If ReceiveCallback is called
 *   from ISR and TimerHandler runs in task, it's acceptable in many embedded targets.
 * - For strict atomicity you may disable interrupts around ReceiveCallback or use
 *   a small mutex/critical section.
 */
void RTUSlave_ReceiveCallback(uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || len > RTU_DEFAULT_BUF_SIZE)
        return;

    size_t copy_len = len;
    if (copy_len > sizeof(this->buf))
        copy_len = sizeof(this->buf);

    /* copy first, then publish length+ready */
    memcpy(this->buf, data, copy_len);
    this->g_frame.len = copy_len;
    this->g_frame.ready = true;
}

/* Helper: find node with address in list head (head is first-node pointer, or NULL) */
static RTU_Register_t *rtu_find_node(RTU_Register_t *head, uint16_t addr)
{
    RTU_Register_t *cur = head;
    while (cur)
    {
        if (cur->address == addr)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/* The periodic handler: when a frame is ready, process it. */
RTU_Sta_t RTUSlave_TimerHandler(void)
{
    if (!this->g_frame.ready || this->g_frame.len < 8)
        return RTU_NOACTIVE;

    /* snapshot frame pointer and len, then clear ready to allow next receive */
    uint8_t *frame = this->buf;
    size_t size = this->g_frame.len;

    /* clear ready early so ReceiveCallback can overwrite buffer while we process */
    this->g_frame.ready = false;
    this->g_frame.len = 0;

    /* Basic validation */
    if (size < 8)
        return RTU_ERR;

    if (frame[0] != this->id)
        return RTU_ERR;

    uint16_t recv_crc = (uint16_t)frame[size - 2] | ((uint16_t)frame[size - 1] << 8);
    if (recv_crc != CRC16(frame, size - 2))
    {
        return RTU_ERR;
    }

    uint8_t func = frame[1];
    uint16_t regAddr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t reqNum = ((uint16_t)frame[4] << 8) | frame[5];
    RTU_Sta_t ret = RTU_ERR;

    size_t resp_len = 0;

    switch (func)
    {
    case RTU_FUNC_READ_COILS: // red coils
    {
        if (reqNum == 0 || reqNum > 2000)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        size_t byte_count = (reqNum + 7) / 8;
        size_t needed = 1 + 1 + 1 + byte_count + 2; /* id + func + bytecount + data + crc */
        if (needed > sizeof(this->buf))
        {
            rtu_send_exception(func, RTU_EX_SLAVE_FAILURE);
            return RTU_ERR;
        }

        memset(this->buf, 0, sizeof(this->buf));

        this->buf[0] = this->id;
        this->buf[1] = RTU_FUNC_READ_COILS;
        this->buf[2] = (uint8_t)byte_count;

        RTU_Register_t *node = rtu_find_node(this->coils, regAddr);
        if (node == NULL)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_ADDR);
            return RTU_ERR;
        }

        for (uint16_t i = 0; i < reqNum; ++i)
        {
            uint16_t expect_addr = regAddr + i;
            if (node == NULL || node->address != expect_addr)
            {
                rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
                return RTU_ERR;
            }

            uint8_t bit = 0;
            if (node->value)
            {
                bit = ((*((uint8_t *)node->value)) != 0) ? 1u : 0u;
            }

            size_t byte_index = 3 + (i >> 3);
            size_t bit_index = i & 0x07;
            this->buf[byte_index] |= (uint8_t)(bit << bit_index);

            node = node->next;
        }

        size_t crc_pos = 3 + byte_count;
        uint16_t crc = CRC16(this->buf, crc_pos);
        this->buf[crc_pos + 0] = (uint8_t)(crc & 0x00FF);
        this->buf[crc_pos + 1] = (uint8_t)((crc >> 8) & 0x00FF);

        resp_len = crc_pos + 2;
        RTU_Transmit(this->buf, resp_len);

        ret = RTU_READ_COIL;
        break;
    }

    case RTU_FUNC_READ_HOLD_REGS:
    {
        if (reqNum == 0 || reqNum > 125)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        size_t byte_count = reqNum * 2;
        size_t needed = 1 + 1 + 1 + byte_count + 2;
        if (needed > sizeof(this->buf))
            return RTU_ERR;

        memset(this->buf, 0, sizeof(this->buf));

        this->buf[0] = this->id;
        this->buf[1] = RTU_FUNC_READ_HOLD_REGS;
        this->buf[2] = (uint8_t)byte_count;

        RTU_Register_t *node = rtu_find_node(this->holdingRegs, regAddr);
        if (node == NULL)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_ADDR);
            return RTU_ERR;
        }

        for (uint16_t i = 0; i < reqNum; ++i)
        {
            uint16_t expect_addr = regAddr + i;
            if (node == NULL || node->address != expect_addr)
            {
                rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
                return RTU_ERR;
            }

            uint16_t val = 0;
            if (node->value)
            {
                val = *((uint16_t *)node->value);
            }

            size_t off = 3 + i * 2;
            this->buf[off + 0] = (uint8_t)((val >> 8) & 0xFF);
            this->buf[off + 1] = (uint8_t)(val & 0xFF);

            node = node->next;
        }

        size_t crc_pos = 3 + byte_count;
        uint16_t crc = CRC16(this->buf, crc_pos);
        this->buf[crc_pos + 0] = (uint8_t)(crc & 0x00FF);
        this->buf[crc_pos + 1] = (uint8_t)((crc >> 8) & 0x00FF);

        resp_len = crc_pos + 2;
        RTU_Transmit(this->buf, resp_len);
        ret = RTU_READ_HOLD_REG;
        break;
    }

    case RTU_FUNC_WRITE_SINGLE_REG: // write single register
    {
        RTU_Register_t *node = rtu_find_node(this->holdingRegs, regAddr);
        if (node == NULL)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_ADDR);
            return RTU_ERR;
        }

        if (size < 8)
            return RTU_ERR;

        if (node->permiss == RTU_PERMISS_OR)
        {
            return RTU_PERMISS_ERR;
        }
        uint16_t value = ((uint16_t)frame[4] << 8) | frame[5];
        if (node->value)
        {
            *((uint16_t *)node->value) = value;
        }

        /* echo back request as response (per Modbus) */
        RTU_Transmit(frame, size);
        resp_len = size;
        ret = RTU_WRITE_HOLD_REG;
        break;
    }

    case RTU_FUNC_MULTIPLE_WRITE_REG: // write mulitple hold register
    {
        RTU_Register_t *node = rtu_find_node(this->holdingRegs, regAddr);
        RTU_Register_t *permiss = rtu_find_node(this->holdingRegs, regAddr);
        if (node == NULL)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_ADDR);
            return RTU_ERR;
        }

        /* ensure payload length present in frame: byte count at frame[6] and all data */
        if (size < 9)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        /* full length check before accessing data */
        if (size < (9 + (size_t)reqNum * 2))
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        /* check register's read write permiss */
        for (int i = 0; i < reqNum; i++)
        {
            if (node->permiss == RTU_PERMISS_OR)
            {
                rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
                return RTU_PERMISS_ERR;
            }

            permiss = permiss->next;
        }

        for (uint16_t i = 0; i < reqNum; ++i)
        {
            uint16_t expect_addr = regAddr + i;
            if (node == NULL || node->address != expect_addr)
            {
                rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
                return RTU_ERR;
            }

            uint16_t value = ((uint16_t)frame[7 + i * 2] << 8) | frame[8 + i * 2];

            if (node->value)
            {
                *((uint16_t *)node->value) = value;
            }

            node = node->next;
        }

        /* build response: address + qty written (8 bytes total) */
        resp_len = 8;
        memset(this->buf, 0, sizeof(this->buf));
        this->buf[0] = this->id;
        this->buf[1] = RTU_FUNC_MULTIPLE_WRITE_REG;
        this->buf[2] = (uint8_t)((regAddr & 0XFF00) >> 8);
        this->buf[3] = (uint8_t)(regAddr & 0x00FF);
        this->buf[4] = (uint8_t)((reqNum & 0XFF00) >> 8);
        this->buf[5] = (uint8_t)(reqNum & 0x00FF);
        uint16_t crc = CRC16(this->buf, resp_len - 2);
        this->buf[6] = (uint8_t)(crc & 0x00FF);
        this->buf[7] = (uint8_t)((crc & 0XFF00) >> 8);
        RTU_Transmit(this->buf, resp_len);

        ret = RTU_WRITE_HOLD_REG;
        break;
    }

    case RTU_FUNC_MULTIPLE_WRITE_COILS:
    {
        /*
        请求帧结构：
        [id][func=0x0F][addr_hi][addr_lo][qty_hi][qty_lo][byte_count][data...][crc_lo][crc_hi]

        响应帧结构：
        [id][func=0x0F][addr_hi][addr_lo][qty_hi][qty_lo][crc_lo][crc_hi]
        */

        if (reqNum == 0 || reqNum > 1968) // Modbus标准最大1968 bits
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        if (size < 9)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        uint8_t byte_count = frame[6];
        if (byte_count != (reqNum + 7) / 8)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        if (size < (7 + byte_count + 2))
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        RTU_Register_t *node = rtu_find_node(this->coils, regAddr);
        if (node == NULL)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_ADDR);
            return RTU_ERR;
        }

        /* ---------- 第一阶段：权限检查 ---------- */
        RTU_Register_t *check = node;
        for (uint16_t i = 0; i < reqNum; i++)
        {
            uint16_t expect_addr = regAddr + i;

            if (check == NULL || check->address != expect_addr)
            {
                rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
                return RTU_PERMISS_ERR;
            }

            if (check->permiss == RTU_PERMISS_OR)
            {
                rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
                return RTU_PERMISS_ERR;
            }

            check = check->next;
        }

        /* ---------- 第二阶段：执行写入 ---------- */
        for (uint16_t i = 0; i < reqNum; i++)
        {
            uint8_t byte_index = i >> 3;
            uint8_t bit_index = i & 0x07;

            uint8_t bit = (frame[7 + byte_index] >> bit_index) & 0x01;

            if (node->value)
            {
                *((uint8_t *)node->value) = bit ? 1 : 0;
            }

            node = node->next;
        }

        /* ---------- 构造响应帧 ---------- */
        resp_len = 8;
        memset(this->buf, 0, sizeof(this->buf));

        this->buf[0] = this->id;
        this->buf[1] = RTU_FUNC_MULTIPLE_WRITE_COILS;
        this->buf[2] = (uint8_t)(regAddr >> 8);
        this->buf[3] = (uint8_t)(regAddr & 0xFF);
        this->buf[4] = (uint8_t)(reqNum >> 8);
        this->buf[5] = (uint8_t)(reqNum & 0xFF);

        uint16_t crc = CRC16(this->buf, 6);
        this->buf[6] = (uint8_t)(crc & 0xFF);
        this->buf[7] = (uint8_t)(crc >> 8);

        RTU_Transmit(this->buf, resp_len);

        ret = RTU_WRITE_COIL;
        break;
    }

    case RTU_FUNC_WRITE_SINGLE_COILS:
    {
        /*
        请求帧：
        [id][func=0x05][addr_hi][addr_lo][value_hi][value_lo][crc]

        value:
        0xFF00 = ON
        0x0000 = OFF

        响应帧：
        完全回显请求帧
        */

        if (size < 8)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        RTU_Register_t *node = rtu_find_node(this->coils, regAddr);
        if (node == NULL)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_ADDR);
            return RTU_ERR;
        }

        if (node->permiss == RTU_PERMISS_OR)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_PERMISS_ERR;
        }

        uint16_t value = ((uint16_t)frame[4] << 8) | frame[5];

        uint8_t bit = (value == 0xFF00) ? 1 : 0;

        if (node->value)
        {
            *((uint8_t *)node->value) = bit;
        }

        /* 回显 */
        RTU_Transmit(frame, size);
        resp_len = size;

        ret = RTU_WRITE_COIL;
        break;
    }

    case RTU_FUNC_READ_INPUT_REG:
    {
        /*
        请求帧：
        [id][func=0x04][addr_hi][addr_lo][qty_hi][qty_lo][crc]

        响应帧：
        [id][func=0x04][byte_count][data_hi][data_lo]...[crc]
        */

        if (reqNum == 0 || reqNum > 125)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        size_t byte_count = reqNum * 2;
        size_t needed = 1 + 1 + 1 + byte_count + 2;

        if (needed > sizeof(this->buf))
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        memset(this->buf, 0, sizeof(this->buf));

        this->buf[0] = this->id;
        this->buf[1] = RTU_FUNC_READ_INPUT_REG;
        this->buf[2] = (uint8_t)byte_count;

        RTU_Register_t *node = rtu_find_node(this->inputRegs, regAddr);

        if (node == NULL)
        {
            rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
            return RTU_ERR;
        }

        for (uint16_t i = 0; i < reqNum; i++)
        {
            uint16_t expect_addr = regAddr + i;

            if (node == NULL || node->address != expect_addr)
            {
                rtu_send_exception(func, RTU_EX_ILLEGAL_VALUE);
                return RTU_ERR;
            }

            uint16_t val = 0;

            if (node->value)
            {
                val = *((uint16_t *)node->value);
            }

            size_t off = 3 + i * 2;
            this->buf[off] = (uint8_t)(val >> 8);
            this->buf[off + 1] = (uint8_t)(val & 0xFF);

            node = node->next;
        }

        size_t crc_pos = 3 + byte_count;
        uint16_t crc = CRC16(this->buf, crc_pos);

        this->buf[crc_pos] = (uint8_t)(crc & 0xFF);
        this->buf[crc_pos + 1] = (uint8_t)(crc >> 8);

        resp_len = crc_pos + 2;
        RTU_Transmit(this->buf, resp_len);

        ret = RTU_READ_INPUT_REG;
        break;
    }

    default:
        rtu_send_exception(func, RTU_EX_ILLEGAL_FUNC);
        return RTU_ERR;
    }

    /* clear internal buffer after processing (optional) */
    memset(this->buf, 0, sizeof(this->buf));
    return ret;
}

/* Modify id (single param) */
RTU_Sta_t RTUSlave_Modifyid(uint8_t id)
{
    if (id == 0 || id >= 0xFF)
        return RTU_ERR;

    this->id = id;
    return RTU_OK;
}

/* Default weak transmit function (user may override) */
int __attribute__((weak)) RTU_Transmit(uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    /* default: nothing (user should provide a strong implementation in their project) */
    return 0;
}
