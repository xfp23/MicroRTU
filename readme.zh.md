# Modbus RTU 从机库

一个轻量级的Modbus RTU从机实现库，适用于嵌入式系统。

[English](readme.md) | 中文版

## 功能特性

- **Modbus RTU协议**: 完整的从机实现
- **支持的功能码**:
  - `0x01` - 读线圈
  - `0x03` - 读保持寄存器  
  - `0x06` - 写单个寄存器
  - `0x16` - 寄存器按位写
- **自动CRC校验**
- **灵活的寄存器映射**
- **内存安全**

## 快速开始

### 1. 包含库文件

```c
#include "RtuSlave.h"
```

### 2. 定义数据和寄存器映射

```c
// 数据存储
uint8_t coil_data[10] = {0};
uint16_t holding_reg_data[5] = {100, 200, 300, 400, 500};
uint16_t write_reg_data[3] = {0};

// 寄存器映射
RTU_RegisterMap_t coil_map[] = {
    {0x0000, &coil_data[0]},
    {0x0001, &coil_data[1]},
    // ... 更多线圈
};

RTU_RegisterMap_t holding_map[] = {
    {0x0000, &holding_reg_data[0]},
    {0x0001, &holding_reg_data[1]},
    // ... 更多寄存器
};
```

### 3. 实现发送函数

```c
int transmit_data(uint8_t *data, size_t size)
{
    // 通过UART、SPI等方式发送数据
    // 成功返回0，失败返回-1
    return uart_send(data, size);
}
```

### 4. 初始化和使用

```c
int main()
{
    RTUSlave_handle_t slave_handle = NULL;
    
    // 配置
    RtuSlave_Conf_t config = {
        .id = 0x01,                    // 设备ID
        .buf_size = 256,               // 缓冲区大小
        .transmit = transmit_data,     // 发送函数
        .coils = {.map = coil_map, .count = 10},
        .holdingRegs = {.map = holding_map, .count = 5},
        .writeRegs = {.map = write_map, .count = 3}
    };
    
    // 初始化
    if(RTUSlave_Init(&slave_handle, &config) != RTU_OK) {
        return -1;
    }
    
    // 处理接收到的数据帧
    RTU_Sta_t result = RTUSlave_TimerHandler(slave_handle, frame, size);
    
    // 清理资源
    RTU_Deinit(&slave_handle);
    return 0;
}
```

## API 参考

### 函数

| 函数 | 描述 |
|------|------|
| `RTUSlave_Init()` | 初始化从机实例 |
| `RTUSlave_TimerHandler()` | 处理接收到的Modbus数据帧 |
| `RTUSlave_Modifyid()` | 动态修改设备ID |
| `RTU_Deinit()` | 清理资源 |

### 返回值

`RTUSlave_TimerHandler()` 返回以下状态码：

| 返回值 | 描述 | 建议操作 |
|--------|------|----------|
| `RTU_READ_HOLD_REG` | 执行了读寄存器操作 | 数据已从保持寄存器读取 |
| `RTU_WRITE_HOLD_REG` | 执行了写寄存器操作 | 数据已写入寄存器 |
| `RTU_READ_COIL` | 执行了读线圈操作 | 线圈状态已读取 |
| `RTU_ERR` | 处理失败 | 检查数据帧格式、CRC或寄存器映射 |

### RTUSlave_Modifyid 函数

```c
RTU_Sta_t RTUSlave_Modifyid(RTUSlave_handle_t handle, uint8_t id);
```

动态修改已初始化从机实例的设备ID。

**参数：**
- `handle`: 从机句柄
- `id`: 新的设备ID (1-254，0和255无效)

**返回值：**
- `RTU_OK`: ID修改成功
- `RTU_ERR`: 无效的句柄或ID

**示例：**
```c
// 将设备ID从0x01改为0x02
if(RTUSlave_Modifyid(slave_handle, 0x02) == RTU_OK) {
    printf("设备ID已更改为0x02\n");
}
```

### 返回值使用示例

```c
RTU_Sta_t result = RTUSlave_TimerHandler(slave_handle, frame, size);

switch(result) {
    case RTU_READ_HOLD_REG:
        // 读寄存器操作完成
        // 可以更新UI、记录数据等
        break;
    case RTU_WRITE_HOLD_REG:
        // 写寄存器操作完成
        // 可以保存数据、触发事件等
        break;
    case RTU_READ_COIL:
        // 读线圈操作完成
        // 可以更新线圈状态显示等
        break;
    case RTU_ERR:
        // 处理错误（无效数据帧、CRC错误等）
        break;
}
```

### 数据结构

| 类型 | 描述 |
|------|------|
| `RTUSlave_handle_t` | 从机句柄类型 |
| `RtuSlave_Conf_t` | 配置结构体 |
| `RTU_RegisterMap_t` | 寄存器映射结构体 |

## 编译

```bash
gcc -o example example.c src/RtuSlave.c -Iinclude
```

## 系统要求

- C99编译器
- 标准C库 (stdlib.h, string.h, stdint.h)

## 重要说明

1. **寄存器地址必须连续** - 不允许有间隔
2. **线程安全** - 非线程安全，多线程环境需要添加同步机制
3. **缓冲区大小** - 确保缓冲区足够大以处理最大的Modbus帧
4. **发送函数** - 必须由用户实现

## 示例

查看 `example/example.c` 获取完整的工作示例。

## 许可证

详细信息请查看LICENSE文件。

## 版本

v0.1 - 初始版本，支持基本的Modbus RTU从机功能
