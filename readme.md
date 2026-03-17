# Modbus RTU Slave Library

A lightweight Modbus RTU slave implementation library for embedded systems.

[中文版](readme.zh.md) | English

## Features

- **Modbus RTU Protocol**: Full slave implementation
- **Supported Function Codes**:
  - `0x01` - Read Coils
  - `0x03` - Read Holding Registers  
  - `0x06` - Write Single Register
  - `0x16` - Mask Write Register
- **Automatic CRC validation**
- **Flexible register mapping**
- **Memory safe**

## Quick Start

### 1. Include the library

```c
#include "RtuSlave.h"
```

### 2. Define your data and register mapping

```c
// Your data storage
uint8_t coil_data[10] = {0};
uint16_t holding_reg_data[5] = {100, 200, 300, 400, 500};
uint16_t write_reg_data[3] = {0};

// Register mapping
RTU_RegisterMap_t coil_map[] = {
    {0x0000, &coil_data[0]},
    {0x0001, &coil_data[1]},
    // ... more coils
};

RTU_RegisterMap_t holding_map[] = {
    {0x0000, &holding_reg_data[0]},
    {0x0001, &holding_reg_data[1]},
    // ... more registers
};
```

### 3. Implement transmit function

```c
int transmit_data(uint8_t *data, size_t size)
{
    // Send data via UART, SPI, etc.
    // Return 0 on success, -1 on error
    return uart_send(data, size);
}
```

### 4. Initialize and use

```c
int main()
{
    RTUSlave_handle_t slave_handle = NULL;
    
    // Configuration
    RtuSlave_Conf_t config = {
        .id = 0x01,                    // Device ID
        .buf_size = 256,               // Buffer size
        .transmit = transmit_data,     // Transmit function
        .coils = {.map = coil_map, .count = 10},
        .holdingRegs = {.map = holding_map, .count = 5},
        .writeRegs = {.map = write_map, .count = 3}
    };
    
    // Initialize
    if(RTUSlave_Init(&slave_handle, &config) != RTU_OK) {
        return -1;
    }
    
    // Process received frames
    RTU_Sta_t result = RTUSlave_TimerHandler(slave_handle, frame, size);
    
    // Cleanup
    RTU_Deinit(&slave_handle);
    return 0;
}
```

## API Reference

### Functions

| Function | Description |
|----------|-------------|
| `RTUSlave_Init()` | Initialize slave instance |
| `RTUSlave_TimerHandler()` | Process received Modbus frames |
| `RTUSlave_Modifyid()` | Modify device ID dynamically |
| `RTU_Deinit()` | Cleanup resources |

### Return Values

`RTUSlave_TimerHandler()` returns the following status codes:

| Return Value | Description | Action |
|--------------|-------------|---------|
| `RTU_READ_HOLD_REG` | Read register operation executed | Data was read from holding registers |
| `RTU_WRITE_HOLD_REG` | Write register operation executed | Data was written to registers |
| `RTU_READ_COIL` | Read coil operation executed | Coil states were read |
| `RTU_ERR` | Processing failed | Check frame format, CRC, or register mapping |

### RTUSlave_Modifyid Function

```c
RTU_Sta_t RTUSlave_Modifyid(RTUSlave_handle_t handle, uint8_t id);
```

Dynamically modify the device ID of an initialized slave instance.

**Parameters:**
- `handle`: Slave handle
- `id`: New device ID (1-254, 0 and 255 are invalid)

**Returns:**
- `RTU_OK`: ID modified successfully
- `RTU_ERR`: Invalid handle or ID

**Example:**
```c
// Change device ID from 0x01 to 0x02
if(RTUSlave_Modifyid(slave_handle, 0x02) == RTU_OK) {
    printf("Device ID changed to 0x02\n");
}
```

### Usage Example with Return Values

```c
RTU_Sta_t result = RTUSlave_TimerHandler(slave_handle, frame, size);

switch(result) {
    case RTU_READ_HOLD_REG:
        // Read register operation completed
        // You can update UI, log data, etc.
        break;
    case RTU_WRITE_HOLD_REG:
        // Write register operation completed
        // You can save data, trigger events, etc.
        break;
    case RTU_READ_COIL:
        // Read coil operation completed
        // You can update coil status display, etc.
        break;
    case RTU_ERR:
        // Handle error (invalid frame, CRC error, etc.)
        break;
}
```

### Data Structures

| Type | Description |
|------|-------------|
| `RTUSlave_handle_t` | Slave handle type |
| `RtuSlave_Conf_t` | Configuration structure |
| `RTU_RegisterMap_t` | Register mapping structure |

## Compilation

```bash
gcc -o example example.c src/RtuSlave.c -Iinclude
```

## Requirements

- C99 compiler
- Standard C library (stdlib.h, string.h, stdint.h)

## Important Notes

1. **Register addresses must be consecutive** - no gaps allowed
2. **Thread safety** - Not thread-safe, add synchronization if needed
3. **Buffer size** - Ensure sufficient buffer size for largest Modbus frame
4. **Transmit function** - Must be implemented by user

## Example

See `example/example.c` for a complete working example.

## License

See LICENSE file for details.

## Version

v0.1 - Initial release with basic Modbus RTU slave functionality