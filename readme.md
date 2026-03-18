# MicroRTU — Quick User Guide

[中文](./readme.zh.md)

Lightweight Modbus RTU slave (single-instance). This document tells you **how to use the API** (what you call, what to pass, how to arrange your register tables). It is *not* an implementation walkthrough — you do not need to read the library internals to use it.

---

## 1 — Quick overview (how it works)

1. Call `RTUSlave_Init()` once at startup.
2. Register your device registers (coils, holding regs, input regs) with the provided `RTUSlave_Register*` APIs. Each registration creates internal nodes that point to your data pointers.
3. Provide transport by overriding the weak `RTU_Transmit()` with your UART/serial send function.
4. When bytes arrive from the master, call `RTUSlave_ReceiveCallback(data, len)` — this **only copies the latest frame** into the internal buffer (no parsing).
5. Periodically call `RTUSlave_TimerHandler()` (from a main loop or a timer task). It parses the frame and calls `RTU_Transmit()` with the response.
6. When done, call `RTUSlave_Deinit()` to free registered nodes.

---

## 2 — Public API (what you call)

```c
// init / deinit
RTU_Sta_t RTUSlave_Init(void);
void      RTUSlave_Deinit(void);

// register maps
RTU_Sta_t RTUSlave_RegisterCoils(RTU_RegisterMap_t *Map, size_t regNum);
RTU_Sta_t RTUSlave_RegisterHoldReg(RTU_RegisterMap_t *Map, size_t regNum);
RTU_Sta_t RTUSlave_RegisterInputReg(RTU_RegisterMap_t *Map, size_t regNum);

// runtime
void      RTUSlave_ReceiveCallback(uint8_t *data, size_t len);
RTU_Sta_t RTUSlave_TimerHandler(void);

// utilities
RTU_Sta_t RTUSlave_Modifyid(uint8_t id);

// override this in your project to actually send bytes:
int __attribute__((weak)) RTU_Transmit(uint8_t *data, size_t size);
```

Return values are `RTU_Sta_t` (e.g. `RTU_OK`, `RTU_ERR`, `RTU_PERMISS_ERR`, `RTU_READ_HOLD_REG`, ...). These are for your code to inspect; the library will call `RTU_Transmit()` to send protocol responses.

---

## 3 — Register map types & map structure

Use the unified map item `RTU_RegisterMap_t`:

```c
typedef struct
{
    uint16_t addr;            // Modbus register/coil address (16-bit)
    RTU_Permiss_t permiss;    // RTU_PERMISS_OR (read only) or RTU_PERMISS_RW (read/write)
    RTUSlave_Func_t callback; // Usercallback Triggered on access
    void *data;               // pointer to variable holding the value (uint8_t for coils, uint16_t for registers)
} RTU_RegisterMap_t;
```

`RTU_Register_t` nodes created by the library contain:

```c
typedef struct RTU_Register
{
    uint16_t address;
    uint8_t permiss;   // RTU_PERMISS_OR / RTU_PERMISS_RW
    void *value;       // pointer to user data
    struct RTU_Register *next;
} RTU_Register_t;
```

### Data types

* **Coils**: user `data` should point to a `uint8_t` (0 or 1).
* **Holding / Input registers**: user `data` should point to a `uint16_t`.

---

## 4 — How to create & register a map (examples)

Example variables and maps:

```c
// application variables
uint8_t coil_start = 0;
uint8_t coil_stop  = 0;
uint16_t holding_param = 1234;
uint16_t input_temp   = 250; // e.g. 25.0 °C scaled

// map array (one array for each type)
RTU_RegisterMap_t coils_map[] = {
    { .addr = 0x0001, .callback = NULL, .permiss = RTU_PERMISS_RW, .data = &coil_start },
    { .addr = 0x0002, .callback = NULL, .permiss = RTU_PERMISS_RW, .data = &coil_stop  },
};

RTU_RegisterMap_t hold_map[] = {
    { .addr = 0x4000, .callback = NULL, .permiss = RTU_PERMISS_RW, .data = &holding_param },
};

RTU_RegisterMap_t input_map[] = {
    { .addr = 0x3000, .callback = NULL, .permiss = RTU_PERMISS_OR, .data = &input_temp }, // No matter what permissions you set, it is read-only.
};
```

Register them after `RTUSlave_Init()`:

```c
RTUSlave_Init();

RTUSlave_RegisterCoils(coils_map,RTU_MAP_SIZEOF(coils_map));
RTUSlave_RegisterHoldReg(hold_map,  RTU_MAP_SIZEOF(hold_map));
RTUSlave_RegisterInputReg(input_map, RTU_MAP_SIZEOF(input_map));
```

> **Important:** The library expects your registered nodes for multi-read/write to be **in ascending address order and contiguous** when you want block read/write behavior (the implementation assumes `node->next` is the next address). If you register sparse/unsorted maps, multi-register/coil operations (`0x03`, `0x10`, `0x01`, `0x0F`) may fail.

---

## 5 — Overriding transport: implement `RTU_Transmit`

The library declares a **weak** `RTU_Transmit()` — override it in your project:

```c
int RTU_Transmit(uint8_t *data, size_t size)
{
    // send data over UART/RS485 driver; blocking or buffered is fine
    uart_write(data, size);
    return 0; // user-defined return value
}
```

---

## 6 — Receiving frames & dispatching

When bytes arrive from the serial driver, call:

```c
RTUSlave_ReceiveCallback(rx_buf, rx_len);
```

This **copies** bytes into the internal single-frame buffer and sets a ready flag. The library does **not** parse in the callback. Then, periodically (main loop or timer task), call:

```c
RTUSlave_TimerHandler();
```

`RTUSlave_TimerHandler()` will:

* Validate frame size and CRC (Modbus CRC16 — CRC placed low byte then high byte at the end of the frame).
* Parse function code and addresses/quantities.
* Read/write the registered nodes, respecting `permiss`.
* Build the Modbus response and call `RTU_Transmit()`.

**Concurrency note:** `RTUSlave_ReceiveCallback()` may be called from an ISR. If so, ensure `RTUSlave_TimerHandler()` is called in a safe context; you may want to disable interrupts briefly around `RTUSlave_ReceiveCallback()` or use a minimal critical section depending on your platform.

---

## 7 — Function codes and frame formats (request & response)

> CRC bytes are **low byte then high byte** (little-endian) and are the final two bytes in a frame.

### 0x03 — Read Holding Registers (request)

```
Request:
[ id ][ 0x03 ][ addr_hi ][ addr_lo ][ qty_hi ][ qty_lo ][ CRC_lo ][ CRC_hi ]

Response:
[ id ][ 0x03 ][ byte_count ][ data_hi ][ data_lo ] ... [ CRC_lo ][ CRC_hi ]
byte_count = qty * 2
```

### 0x04 — Read Input Registers (request)

```
Request:
[ id ][ 0x04 ][ addr_hi ][ addr_lo ][ qty_hi ][ qty_lo ][ CRC_lo ][ CRC_hi ]

Response:
[ id ][ 0x04 ][ byte_count ][ data_hi ][ data_lo ] ... [ CRC_lo ][ CRC_hi ]
```

### 0x06 — Write Single Holding Register (request)

```
Request:
[ id ][ 0x06 ][ addr_hi ][ addr_lo ][ value_hi ][ value_lo ][ CRC_lo ][ CRC_hi ]

Response (echo):
[ id ][ 0x06 ][ addr_hi ][ addr_lo ][ value_hi ][ value_lo ][ CRC_lo ][ CRC_hi ]
```

### 0x10 — Write Multiple Holding Registers (request)

```
Request:
[ id ][ 0x10 ][ addr_hi ][ addr_lo ][ qty_hi ][ qty_lo ][ byte_count ][ data... ][ CRC_lo ][ CRC_hi ]
byte_count = qty * 2

Response:
[ id ][ 0x10 ][ addr_hi ][ addr_lo ][ qty_hi ][ qty_lo ][ CRC_lo ][ CRC_hi ]
```

### 0x01 — Read Coils (request)

```
Request:
[ id ][ 0x01 ][ addr_hi ][ addr_lo ][ qty_hi ][ qty_lo ][ CRC_lo ][ CRC_hi ]

Response:
[ id ][ 0x01 ][ byte_count ][ coil_bytes... ][ CRC_lo ][ CRC_hi ]
byte_count = (qty + 7) / 8
coils packed LSB first within each data byte
```

### 0x05 — Write Single Coil

```
Request:
[ id ][ 0x05 ][ addr_hi ][ addr_lo ][ value_hi ][ value_lo ][ CRC_lo ][ CRC_hi ]
value: 0xFF00 = ON, 0x0000 = OFF

Response (echo request)
```

### 0x0F — Write Multiple Coils

```
Request:
[ id ][ 0x0F ][ addr_hi ][ addr_lo ][ qty_hi ][ qty_lo ][ byte_count ][ data... ][ CRC_lo ][ CRC_hi ]
byte_count = (qty + 7) / 8

Response:
[ id ][ 0x0F ][ addr_hi ][ addr_lo ][ qty_hi ][ qty_lo ][ CRC_lo ][ CRC_hi ]
```

---

## 8 — Permissions & write semantics

* `RTU_PERMISS_OR` — **read-only** (writes will be rejected with `RTU_PERMISS_ERR` in the library).
* `RTU_PERMISS_RW` — **read/write**.

**Multi-write semantics:** The library checks **all** target addresses for permissions and address continuity first. If any entry is invalid or read-only, the whole operation fails (no partial writes). This follows Modbus all-or-nothing behavior.

---

## 9 — Configurable macros (from `RTUSlave_config.h`)

* `RTU_DEFAULT_BUF_SIZE` — frame buffer (default 256).
* `RTU_MAX_COILS` — max coils allowed by registration (default 128).
* `RTU_MAX_HOLD_REGS` — max holding regs (default 128).
* `RTU_MAX_INPUT_REGS` — max input regs (default 128).

The register registration functions check `regNum` against these macros and return `RTU_ERR` if the provided count exceeds the macro.

---

## 10 — Example usage (minimal)

```c
// --- application globals ---
uint8_t coil_a = 0;
uint16_t hold_p = 100;
uint16_t input_temp = 300;

// --- register maps ---
RTU_RegisterMap_t coils[] = {
    { .addr = 0x0001, .permiss = RTU_PERMISS_RW, .data = &coil_a }
};
RTU_RegisterMap_t holds[] = {
    { .addr = 0x4000, .permiss = RTU_PERMISS_RW, .data = &hold_p }
};
RTU_RegisterMap_t inputs[] = {
    { .addr = 0x3000, .permiss = RTU_PERMISS_OR, .data = &input_temp }
};

// --- user overrides transmit ---
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
    RTUSlave_RegisterInputReg(inputs, 1); // or proper input registration API

    // main loop
    for (;;)
    {
        // when UART receives a frame:
        // RTUSlave_ReceiveCallback(rx_buf, rx_len);

        // periodically:
        RTUSlave_TimerHandler();

        // app tasks...
    }

    RTUSlave_Deinit();
}
```

---

## 11 — Error handling & exceptions

* Library return codes: `RTU_OK`, `RTU_ERR`, `RTU_PERMISS_ERR`, `RTU_READ_HOLD_REG`, etc. Use these if you want to log or decide further actions.
* **Protocol exceptions** (Modbus exception responses, e.g. function | 0x80 + exception code) are recommended to be returned to master. If you need standard Modbus exception behavior, you should implement it in the code-path where `RTU_PERMISS_ERR` or `RTU_ERR` is returned — i.e. build an exception response and `RTU_Transmit()` it. (If you want, I can add a small helper to build/send standard exception frames.)

---

## 12 — Gotchas & recommendations

* **Map ordering & continuity:** For multi-read/write to work, provide maps in ascending address order and contiguous for the requested ranges. The implementation expects `node->next` to be the next address.
* **Single-frame buffer:** `RTUSlave_ReceiveCallback()` stores only the latest frame. If your serial driver splits frames, collect bytes into a frame buffer in your driver and call the callback when a full Modbus frame is assembled.
* **ISR safety:** If `ReceiveCallback()` is called from an ISR, keep it short. `TimerHandler()` should run in normal task context.
* **Protect critical device state:** Use `RTU_PERMISS_OR` for status registers and those that must never be remotely modified.
* **Test with a Modbus master tool** (e.g. Modbus Poll, QModMaster) to verify register mapping and responses.

---

If you want, I can:

* add a small helper function to **send Modbus exception responses**; or
* produce a **PATCH/diff** that injects standard exception replies automatically when the handler encounters `RTU_PERMISS_ERR` / illegal address / CRC error.

Which would you prefer next?
