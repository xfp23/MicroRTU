
# MicroRTU 快速使用指南

[English](./readme.md)

这是一个轻量级的 Modbus RTU 从机协议栈（单实例）。本手册将告诉你**如何使用 API**（调用什么、传递什么、如何安排寄存器表）。它**不是**实现过程的拆解 —— 你不需要阅读库的内部代码即可使用。

---

## 1 — 快速概览（工作原理）

1. 在启动时调用一次 `RTUSlave_Init()`。
2. 使用提供的 `RTUSlave_Register*` API 注册你的设备寄存器（线圈、保持寄存器、输入寄存器）。每次注册都会创建指向你数据指针的内部节点。
3. 通过在你的项目中重写（Override）**弱函数** `RTU_Transmit()` 来提供传输层支持（即你的 UART 发送函数）。
4. 当从机收到主机发来的字节时，调用 `RTUSlave_ReceiveCallback(data, len)` —— 此操作**仅将最新的帧复制**到内部缓冲区（不进行解析）。
5. 周期性地调用 `RTUSlave_TimerHandler()`（在主循环或定时器任务中）。它负责解析帧并调用 `RTU_Transmit()` 发送响应。
6. 完成后，调用 `RTUSlave_Deinit()` 释放已注册的节点。

---

## 2 — 公共 API（你调用的函数）

```c
// 初始化 / 反初始化
RTU_Sta_t RTUSlave_Init(void);
void      RTUSlave_Deinit(void);

// 注册映射表
RTU_Sta_t RTUSlave_RegisterCoils(RTU_RegisterMap_t *Map, size_t regNum);
RTU_Sta_t RTUSlave_RegisterHoldReg(RTU_RegisterMap_t *Map, size_t regNum);
RTU_Sta_t RTUSlave_RegisterInputReg(RTU_RegisterMap_t *Map, size_t regNum);

// 运行时处理
void      RTUSlave_ReceiveCallback(uint8_t *data, size_t len);
RTU_Sta_t RTUSlave_TimerHandler(void);

// 工具函数
RTU_Sta_t RTUSlave_Modifyid(uint8_t id);

// 在你的项目中重写此函数以实现实际的字节发送：
int __attribute__((weak)) RTU_Transmit(uint8_t *data, size_t size);

```

返回值类型为 `RTU_Sta_t`（例如 `RTU_OK`, `RTU_ERR`, `RTU_PERMISS_ERR` 等）。这些供你的代码检查状态；库会自动调用 `RTU_Transmit()` 发送协议响应。

---

## 3 — 寄存器映射类型与结构

使用统一的映射条目 `RTU_RegisterMap_t`：

```c
typedef struct
{
    uint16_t addr;          // Modbus 寄存器/线圈地址 (16位)
    RTU_Permiss_t permiss;  // RTU_PERMISS_OR (只读) 或 RTU_PERMISS_RW (读写)
    void *data;             // 指向变量值的指针 (线圈用 uint8_t，寄存器用 uint16_t)
} RTU_RegisterMap_t;

```

由库创建的 `RTU_Register_t` 节点包含：

```c
typedef struct RTU_Register
{
    uint16_t address;
    uint8_t permiss;   // RTU_PERMISS_OR / RTU_PERMISS_RW
    void *value;       // 指向用户数据的指针
    struct RTU_Register *next;
} RTU_Register_t;

```

### 数据类型规范

* **线圈 (Coils)**：用户 `data` 应指向 `uint8_t`（0 或 1）。
* **保持/输入寄存器 (Holding / Input registers)**：用户 `data` 应指向 `uint16_t`。

---

## 4 — 如何创建并注册映射表（示例）

示例变量与映射表：

```c
// 应用层变量
uint8_t coil_start = 0;
uint8_t coil_stop  = 0;
uint16_t holding_param = 1234;
uint16_t input_temp   = 250; // 例如 25.0 °C 放大后的值

// 映射表数组 (每种类型一个数组)
RTU_RegisterMap_t coils[] = {
    { .addr = 0x0001, .permiss = RTU_PERMISS_RW, .data = &coil_a,.callback = NULL }
};
RTU_RegisterMap_t holds[] = {
    { .addr = 0x4000, .permiss = RTU_PERMISS_RW, .data = &hold_p,.callback = NULL }
};
RTU_RegisterMap_t inputs[] = {
    { .addr = 0x3000, .permiss = RTU_PERMISS_OR, .data = &input_temp ,.callback = NULL}
};

```

在 `RTUSlave_Init()` 之后注册它们：

```c
RTUSlave_Init();

RTUSlave_RegisterCoils(coils_map,RTU_MAP_SIZEOF(coils_map));
RTUSlave_RegisterHoldReg(hold_map,  RTU_MAP_SIZEOF(hold_map));
RTUSlave_RegisterInputReg(input_map, RTU_MAP_SIZEOF(input_map));

```

> **重要提示：** 为了支持批量读/写操作，库要求注册的节点在地址上必须是**升序排列且连续的**（实现逻辑假设 `node->next` 就是下一个地址）。如果你注册的是稀疏或无序的映射表，批量操作功能（`0x03`, `0x10`, `0x01`, `0x0F`）可能会失败。

---

## 5 — 重写传输层：实现 `RTU_Transmit`

库中声明了一个 **weak（弱）** 函数 `RTU_Transmit()` —— 你需要在项目中重新定义它：

```c
int RTU_Transmit(uint8_t *data, size_t size)
{
    // 通过 UART/RS485 驱动发送数据；阻塞或带缓冲的发送均可
    uart_write(data, size);
    return 0; // 用户自定义返回值
}

```

---

## 6 — 接收帧与分发逻辑

当串口驱动收到字节时，调用：

```c
RTUSlave_ReceiveCallback(rx_buf, rx_len);

```

此函数将字节**复制**到内部的单帧缓冲区并设置就绪标志。库**不会**在回调函数中进行解析。之后，在主循环或定时器任务中周期性调用：

```c
RTUSlave_TimerHandler();

```

`RTUSlave_TimerHandler()` 将执行以下操作：

* 校验帧大小和 CRC（Modbus CRC16 —— 低字节在前，高字节在后的格式）。
* 解析功能码、地址和数量。
* 读/写已注册的节点，并遵循 `permiss` 权限设置。
* 构建 Modbus 响应并调用 `RTU_Transmit()`。

**并发注意：** `RTUSlave_ReceiveCallback()` 可能会在中断服务程序（ISR）中被调用。如果是这种情况，请确保 `RTUSlave_TimerHandler()` 在安全的环境中运行；你可能需要在调用 `ReceiveCallback` 时暂时关闭中断，或者根据平台使用简单的临界区保护。

---

## 7 — 功能码与帧格式（请求与响应）

> CRC 字节为**低字节在前，高字节在后**（小端序），位于帧的最后两个字节。

### 0x03 — 读保持寄存器 (Read Holding Registers)

```
请求：
[ ID ][ 0x03 ][ 起始地址高 ][ 起始地址低 ][ 寄存器数量高 ][ 数量低 ][ CRC低 ][ CRC高 ]

响应：
[ ID ][ 0x03 ][ 字节计数 ][ 数据1高 ][ 数据1低 ] ... [ CRC低 ][ CRC高 ]
字节计数 = 寄存器数量 * 2

```

### 0x04 — 读输入寄存器 (Read Input Registers)

```
请求：
[ ID ][ 0x04 ][ 起始地址高 ][ 起始地址低 ][ 寄存器数量高 ][ 数量低 ][ CRC低 ][ CRC高 ]

响应：
[ ID ][ 0x04 ][ 字节计数 ][ 数据1高 ][ 数据1低 ] ... [ CRC低 ][ CRC高 ]

```

### 0x06 — 写单个保持寄存器 (Write Single Register)

```
请求：
[ ID ][ 0x06 ][ 寄存器地址高 ][ 地址低 ][ 数值高 ][ 数值低 ][ CRC低 ][ CRC高 ]

响应 (原样返回请求帧)：
[ ID ][ 0x06 ][ 寄存器地址高 ][ 地址低 ][ 数值高 ][ 数值低 ][ CRC低 ][ CRC高 ]

```

### 0x10 — 写多个保持寄存器 (Write Multiple Registers)

```
请求：
[ ID ][ 0x10 ][ 起始地址高 ][ 地址低 ][ 数量高 ][ 数量低 ][ 字节计数 ][ 数据... ][ CRC低 ][ CRC高 ]
字节计数 = 数量 * 2

响应：
[ ID ][ 0x10 ][ 起始地址高 ][ 地址低 ][ 数量高 ][ 数量低 ][ CRC低 ][ CRC高 ]

```

### 0x01 — 读线圈 (Read Coils)

```
请求：
[ ID ][ 0x01 ][ 起始地址高 ][ 地址低 ][ 数量高 ][ 数量低 ][ CRC低 ][ CRC高 ]

响应：
[ ID ][ 0x01 ][ 字节计数 ][ 线圈数据... ][ CRC低 ][ CRC高 ]
字节计数 = (数量 + 7) / 8
线圈在每个字节内按 LSB（最低有效位）优先排列

```

### 0x05 — 写单个线圈 (Write Single Coil)

```
请求：
[ ID ][ 0x05 ][ 寄存器地址高 ][ 地址低 ][ 数值高 ][ 数值低 ][ CRC低 ][ CRC高 ]
数值：0xFF00 = 开启 (ON), 0x0000 = 关闭 (OFF)

响应 (原样返回请求帧)

```

### 0x0F — 写多个线圈 (Write Multiple Coils)

```
请求：
[ ID ][ 0x0F ][ 起始地址高 ][ 地址低 ][ 数量高 ][ 数量低 ][ 字节计数 ][ 数据... ][ CRC低 ][ CRC高 ]
字节计数 = (数量 + 7) / 8

响应：
[ ID ][ 0x0F ][ 起始地址高 ][ 地址低 ][ 数量高 ][ 数量低 ][ CRC低 ][ CRC高 ]

```

---

## 8 — 权限与写入语义

* `RTU_PERMISS_OR` — **只读**（写入操作将被库拒绝并返回 `RTU_PERMISS_ERR`）。
* `RTU_PERMISS_RW` — **可读写**。

**批量写入语义：** 库会首先检查目标地址范围内**所有**条目的权限和地址连续性。如果其中任何一个条目无效或为只读，则整个操作将失败（不会执行部分写入）。这遵循 Modbus 的“全有或全无”原则。

---

## 9 — 可配置宏 (位于 `RTUSlave_config.h`)

* `RTU_DEFAULT_BUF_SIZE` — 帧缓冲区大小（默认 256）。
* `RTU_MAX_COILS` — 注册允许的最大线圈数量（默认 128）。
* `RTU_MAX_HOLD_REGS` — 最大保持寄存器数量（默认 128）。
* `RTU_MAX_INPUT_REGS` — 最大输入寄存器数量（默认 128）。

寄存器注册函数会检查 `regNum` 是否超过这些宏定义的限制，若超过则返回 `RTU_ERR`。

---

## 10 — 最小化使用示例

```c
// --- 应用层全局变量 ---
uint8_t coil_a = 0;
uint16_t hold_p = 100;
uint16_t input_temp = 300;

// --- 寄存器映射表 ---
RTU_RegisterMap_t coils[] = {
    { .addr = 0x0001, .permiss = RTU_PERMISS_RW, .data = &coil_a }
};
RTU_RegisterMap_t holds[] = {
    { .addr = 0x4000, .permiss = RTU_PERMISS_RW, .data = &hold_p }
};
RTU_RegisterMap_t inputs[] = {
    { .addr = 0x3000, .permiss = RTU_PERMISS_OR, .data = &input_temp }
};

// --- 用户重写发送函数 ---
int RTU_Transmit(uint8_t *data, size_t size)
{
    uart_send(data, size);
    return 0;
}

int main(void)
{
    RTUSlave_Init();
    RTUSlave_RegisterCoils(coils, 1);
    RTUSlave_RegisterHoldReg(holds, 1);
    RTUSlave_RegisterInputReg(inputs, 1);

    // 主循环
    for (;;)
    {
        // 当串口收到完整帧时调用：
        // RTUSlave_ReceiveCallback(rx_buf, rx_len);

        // 周期性调用：
        RTUSlave_TimerHandler();

        // 其他应用任务...
    }

    RTUSlave_Deinit();
}

```

---

## 11 — 错误处理与异常

* 库的返回值：`RTU_OK`, `RTU_ERR`, `RTU_PERMISS_ERR`, `RTU_READ_HOLD_REG` 等。如果你需要记录日志或做进一步决策，可以使用这些代码。
* **协议异常**（Modbus 异常响应，例如 功能码 | 0x80 + 异常码）：建议返回给主机。如果你需要标准的 Modbus 异常行为，应当在处理 `RTU_PERMISS_ERR`、非法地址或 CRC 错误的代码路径中实现 —— 即构建一个异常帧并调用 `RTU_Transmit()` 发送它。

---

## 12 — 常见坑点与建议

* **映射表排序与连续性**：为了使批量读/写正常工作，映射表必须按地址**升序**排列且对于请求范围是**连续**的。实现逻辑依赖 `node->next` 指向下一个地址。
* **单帧缓冲区**：`RTUSlave_ReceiveCallback()` 仅存储最新的帧。如果你的串口驱动会拆分帧，请在驱动层收集字节，直到拼凑成一个完整的 Modbus 帧后再调用回调。
* **ISR 安全性**：如果在中断（ISR）中调用 `ReceiveCallback`，请保持其简短。`TimerHandler` 应在正常的任务上下文中运行。
* **保护关键设备状态**：对于状态寄存器或绝不能被远程修改的配置，务必使用 `RTU_PERMISS_OR`。
* **使用测试工具**：建议使用 Modbus 主机工具（如 Modbus Poll, QModMaster）来验证寄存器映射和响应是否正确。

---
