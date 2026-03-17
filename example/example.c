#include "RTUSlave.h"
#include <stdio.h>

/* ============================================================
 * User data (register backing variables)
 * ============================================================
 */

/* Holding Registers (RW) */
static uint16_t holdReg1 = 100;
static uint16_t holdReg2 = 200;

/* Input Registers (RO) */
static uint16_t inputReg1 = 1234;
static uint16_t inputReg2 = 5678;

/* Coils (RW, bit type) */
static uint8_t coil1 = 0;
static uint8_t coil2 = 1;
static void *Userdata = NULL;

/* ============================================================
 * Register maps
 * ============================================================
 */

static RTU_RegisterMap_t holdRegMap[] =
{
    { .addr = 0x0000, .permiss = RTU_PERMISS_RW, .data = &holdReg1 },
    { .addr = 0x0001, .permiss = RTU_PERMISS_RW, .data = &holdReg2 },
};

static RTU_RegisterMap_t inputRegMap[] =
{
    { .addr = 0x0000, .permiss = RTU_PERMISS_OR, .data = &inputReg1 },
    { .addr = 0x0001, .permiss = RTU_PERMISS_OR, .data = &inputReg2 },
};

static RTU_RegisterMap_t coilMap[] =
{
    { .addr = 0x0000, .permiss = RTU_PERMISS_RW, .data = &coil1 },
    { .addr = 0x0001, .permiss = RTU_PERMISS_RW, .data = &coil2 },
};


/* ============================================================
 * Hardware abstraction (YOU MUST IMPLEMENT THIS)
 * ============================================================
 */

/**
 * @brief Override weak function to send data via UART/RS485
 */
int RTU_Transmit(uint8_t *data, size_t size)
{
    int u = sizeof(RTU_Permiss_t);
    /* Replace with your platform UART send */
    for (size_t i = 0; i < size; i++)
    {
        putchar(data[i]);  // demo: print to console
    }

    return 0;
}


/* ============================================================
 * Simulated receive (replace with UART ISR)
 * ============================================================
 */
void UART_RxCallback(uint8_t *data, size_t len)
{
    /* Feed data into RTU stack */
    RTUSlave_ReceiveCallback(data, len);
}


/* ============================================================
 * Main
 * ============================================================
 */

int main(void)
{
    /* 1. Init RTU */
    if (RTUSlave_Init() != RTU_OK)
    {
        printf("RTU Init failed\n");
        return -1;
    }

    /* 2. Set slave ID */
    RTUSlave_Modifyid(0x01);

    /* 3. Register maps */
    RTUSlave_RegisterHoldReg(holdRegMap, sizeof(holdRegMap)/sizeof(holdRegMap[0]));
    RTUSlave_RegisterInputReg(inputRegMap, sizeof(inputRegMap)/sizeof(inputRegMap[0]));
    RTUSlave_RegisterCoils(coilMap, sizeof(coilMap)/sizeof(coilMap[0]));

    printf("RTU Slave started...\n");

    /* ============================================================
     * Main loop
     * ============================================================
     */
    while (1)
    {
        /* Periodic processing */
        RTU_Sta_t sta = RTUSlave_TimerHandler();

        /* Optional: debug hook */
        switch (sta)
        {
            case RTU_READ_HOLD_REG:
                printf("[RTU] Read Holding Register\n");
                break;

            case RTU_WRITE_HOLD_REG:
                printf("[RTU] Write Holding Register\n");
                break;

            case RTU_READ_COIL:
                printf("[RTU] Read Coil\n");
                break;

            default:
                break;
        }

        /* ====================================================
         * Simulate runtime data update
         * ====================================================
         */
        inputReg1++;   // 模拟传感器数据变化
        inputReg2 += 2;

        /* 简单延时（根据平台替换） */
        for (volatile int i = 0; i < 100000; i++);
    }
}