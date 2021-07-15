/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* Freescale includes. */
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"

#include "fsl_rdc.h"
#include "fsl_gpt.h"
#include "fsl_gpc.h"
#include "fsl_mu.h"
#include "fsl_i2c.h"
#include "fsl_i2c_freertos.h"
#include "fsl_gpio.h"
#include "fsl_clock.h"

#include "low_power_usecases.h"
//#include "rsc_table.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* Task priorities. */
#define low_power_task_PRIORITY (configMAX_PRIORITIES - 1)
#define I2C_task_PRIORITY (configMAX_PRIORITIES - 1)

#define APP_PowerUpSlot (5U)
#define APP_PowerDnSlot (6U)

/* LPM state of m4 core */
typedef enum lpm_power_status_m4
{
    LPM_M4_STATE_RUN,
    LPM_M4_STATE_WAIT,
    LPM_M4_STATE_STOP,
} LPM_POWER_STATUS_M4;

/* Clock Speed of m4 core */
typedef enum lpm_m4_clock_speed
{
    LPM_M4_HIGH_FREQ,
    LPM_M4_LOW_FREQ
} LPM_M4_CLOCK_SPEED;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void low_power_task(void *pvParameters);
#if (USE_CASE_WITH_I2C == 1) || (USE_CASE_WITH_DUMMY_LOAD == 1)
static void I2C_task(void *pvParameters);
#endif //USE_CASE_WITH_I2C || USE_CASE_WITH_DUMMY_LOAD
static void Peripheral_RdcSetting(void);
#if (USE_CASE_WITH_TIMER == 1)
static void InitWakeupGPT(void);
static void SetWakeupConfig(uint8_t wakeup_sec);
#endif //USE_CASE_WITH_TIMER
#if (USE_CASE_WITH_I2C == 1)
static void I2CInit(i2c_rtos_handle_t *rtos_handle);
static void I2CReceive(i2c_rtos_handle_t *rtos_handle, uint8_t *buffer);
#endif //USE_CASE_WITH_I2C
const char *LPM_MCORE_GetPowerStatusString(void);
void LPM_MCORE_ChangeM4Clock(LPM_M4_CLOCK_SPEED target);
void LPM_MCORE_SetPowerStatus(GPC_Type *base, LPM_POWER_STATUS_M4 targetPowerMode);

/*******************************************************************************
 * Global Variables
 ******************************************************************************/
static LPM_POWER_STATUS_M4 m4_lpm_state = LPM_M4_STATE_RUN;
TaskHandle_t low_power_task_handle = NULL;
#if (USE_CASE_WITH_I2C == 1) || (USE_CASE_WITH_DUMMY_LOAD == 1)
TaskHandle_t I2C_task_handle = NULL;
#endif //USE_CASE_WITH_I2C || USE_CASE_WITH_DUMMY_LOAD
#if (USE_CASE_WITH_I2C == 1)
static i2c_rtos_handle_t master_rtos_handle;
#endif //USE_CASE_WITH_I2C
/*******************************************************************************
 * Code
 ******************************************************************************/
/********** RDC helper functions **********************************************/
static void Peripheral_RdcSetting(void)
{
    rdc_periph_access_config_t periphConfig;

    RDC_GetDefaultPeriphAccessConfig(&periphConfig);
    /* Do not allow the A53 domain(domain0) to access the following peripherals. */
    periphConfig.policy = RDC_DISABLE_A53_ACCESS;
    periphConfig.periph = PERIPH_CONSOLE;
    RDC_SetPeriphAccessConfig(RDC, &periphConfig);

    periphConfig.periph = PERIPH_TIMER;
    RDC_SetPeriphAccessConfig(RDC, &periphConfig);

    periphConfig.periph = PERIPH_I2C;
    RDC_SetPeriphAccessConfig(RDC, &periphConfig);
}

/********** GPT helper functions **********************************************/
#if (USE_CASE_WITH_TIMER == 1)
static void InitWakeupGPT(void)
{
    gpt_config_t gptConfig;

    // GPT clock already configured in clock_config.c
    GPT_GetDefaultConfig(&gptConfig);
    gptConfig.enableRunInDoze = true;
    gptConfig.enableRunInStop = true;
    gptConfig.enableRunInWait = true;
    gptConfig.enableFreeRun = true;
    gptConfig.clockSource = kGPT_ClockSource_Osc;

    /* Initialize timer */
    GPT_Init(TIMER, &gptConfig);
    /* Divide GPT clock source frequency */
    GPT_SetOscClockDivider(TIMER, 2);
}

static void SetWakeupConfig(uint8_t wakeup_sec)
{
    uint32_t gpt_clk_freq = TIMER_CLK_FREQ;

    /* Set timer period */
    gpt_clk_freq /= GPT_GetOscClockDivider(TIMER);
    GPT_SetOutputCompareValue(TIMER, kGPT_OutputCompare_Channel1, gpt_clk_freq / wakeup_sec);
    /* Enable timer interrupt */
    GPT_EnableInterrupts(TIMER, kGPT_OutputCompare1InterruptEnable);

    /* Enable interrupt */
    EnableIRQ(TIMER_IRQ);
    /* Start counting */
    GPT_StartTimer(TIMER);
}

void GPT1_IRQHandler(void)
{
    /* Clear interrupt flag.*/
    GPT_ClearStatusFlags(TIMER, kGPT_OutputCompare1Flag);
    GPT_StopTimer(TIMER);

//    PRINTF("GPT IRQ triggered\r\n");

/* Add for ARM errata 838869, affects Cortex-m7, Cortex-m7F, Cortex-M7, Cortex-M7F Store immediate overlapping
  exception return operation might vector to incorrect interrupt */
#if defined __CORTEX_M && (__CORTEX_M == 4U || __CORTEX_M == 7U)
    __DSB();
#endif
}
#endif //USE_CASE_WITH_TIMER

/********** I2C helper functions **********************************************/
#if (USE_CASE_WITH_I2C == 1)
static void I2CInit(i2c_rtos_handle_t *rtos_handle)
{
    i2c_master_config_t masterConfig;
    status_t status;
    PRINTF("%s\r\n", __func__);

    // I2C clock already configured in clock_config.c
    NVIC_SetPriority(I2C_IRQ, I2C_IRQ_PRIO);

    I2C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Bps = I2C_BAUDRATE;

    status = I2C_RTOS_Init(rtos_handle, I2C_MASTER, &masterConfig, I2C_MASTER_CLK_FREQ);
    if (status != kStatus_Success)
    {
        PRINTF("I2C master: error during init, %d\r\n", status);
    }
}

static void I2CReceive(i2c_rtos_handle_t *rtos_handle, uint8_t *buffer)
{
    i2c_master_transfer_t masterXfer;
    status_t status;
    PRINTF("%s\r\n", __func__);

    /* Set up i2c master to receive data from slave */
    for (uint32_t i = 0; i < I2C_DATA_BUFF_LENGTH; i++)
    {
        buffer[i] = 0;
    }

    for (uint32_t i = 0; i < I2C_DATA_BUFF_LENGTH; i++) {
        memset(&masterXfer, 0, sizeof(masterXfer));
        masterXfer.slaveAddress   = I2C_MASTER_SLAVE_ADDR_7BIT;
        masterXfer.direction      = kI2C_Read;
        masterXfer.subaddress     = i;
        masterXfer.subaddressSize = 1;
        masterXfer.data           = &buffer[i];
        masterXfer.dataSize       = I2C_DATA_LENGTH;
        masterXfer.flags          = kI2C_TransferDefaultFlag;
        status = I2C_RTOS_Transfer(rtos_handle, &masterXfer);
        if (status != kStatus_Success)
        {
            PRINTF("I2C master: error during read transaction, %d\r\n", status);
        }
    }
}
#endif //USE_CASE_WITH_I2C

/********** LPM helper functions **********************************************/
const char *LPM_MCORE_GetPowerStatusString(void)
{
    switch (m4_lpm_state)
    {
        case LPM_M4_STATE_RUN:
            return "RUN";
        case LPM_M4_STATE_WAIT:
            return "WAIT";
        case LPM_M4_STATE_STOP:
            return "STOP";
        default:
            return "UNKNOWN";
    }
}

void LPM_MCORE_ChangeM4Clock(LPM_M4_CLOCK_SPEED target)
{
    /* Change CCM Root to change m4 clock*/
    switch (target)
    {
        case LPM_M4_LOW_FREQ:
            if (CLOCK_GetRootMux(kCLOCK_RootM4) != kCLOCK_M4RootmuxOsc24M)
            {
                CLOCK_SetRootMux(kCLOCK_RootM4, kCLOCK_M4RootmuxOsc24M);
                CLOCK_SetRootDivider(kCLOCK_RootM4, 1U, 1U);
            }
            break;
        case LPM_M4_HIGH_FREQ:
            if (CLOCK_GetRootMux(kCLOCK_RootM4) != kCLOCK_M4RootmuxSysPll1)
            {
                CLOCK_SetRootDivider(kCLOCK_RootM4, 1U, 2U);
                CLOCK_SetRootMux(kCLOCK_RootM4, kCLOCK_M4RootmuxSysPll1); /* switch cortex-m4 to SYSTEM PLL1 */
            }
            break;
        default:
            break;
    }
}

void LPM_MCORE_SetPowerStatus(GPC_Type *base, LPM_POWER_STATUS_M4 targetPowerMode)
{
    gpc_lpm_config_t config;
    config.enCpuClk = false;
    config.enFastWakeUp = false;
    config.enDsmMask = false;
    config.enWfiMask = false;
    config.enVirtualPGCPowerdown = true;
    config.enVirtualPGCPowerup = true;
    switch (targetPowerMode)
    {
        case LPM_M4_STATE_RUN:
            GPC->LPCR_M4 = GPC->LPCR_M4 & (~GPC_LPCR_M4_LPM0_MASK);
            break;
        case LPM_M4_STATE_WAIT:
            GPC_EnterWaitMode(GPC, &config);
            break;
        case LPM_M4_STATE_STOP:
            GPC_EnterStopMode(GPC, &config);
            break;
        default:
            break;
    }

    m4_lpm_state = targetPowerMode;
}

void PreSleepProcessing(void)
{
    DbgConsole_Deinit();

#if (USE_CASE_WITH_I2C == 1)
    I2C_RTOS_Deinit(&master_rtos_handle);
#endif //USE_CASE_WITH_I2C
}

void PostSleepProcessing(void)
{
    DbgConsole_Init(BOARD_DEBUG_UART_INSTANCE, BOARD_DEBUG_UART_BAUDRATE, BOARD_DEBUG_UART_TYPE,
                    BOARD_DEBUG_UART_CLK_FREQ);

#if (USE_CASE_WITH_I2C == 1)
    I2CInit(&master_rtos_handle);
#endif //USE_CASE_WITH_I2C
}


/********** Main functions **********************************************/
/*!
 * @brief Application entry point.
 */
int main(void)
{
    uint32_t i = 0;

    /* Init board hardware. */
    /* M4 has its local cache and enabled by default,
     * need to set smart subsystems (0x28000000 ~ 0x3FFFFFFF)
     * non-cacheable before accessing this address region */
    BOARD_InitMemory();

    /* Board specific RDC settings */
    BOARD_RdcInit();

    BOARD_InitBootPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();

    Peripheral_RdcSetting();
    MU_Init(MUB);

    /*
     * In order to wakeup M4 from LPM, all PLLCTRLs need to be set to "NeededRun"
     */
    for (i = 0; i != 39; i++)
    {
        CCM->PLL_CTRL[i].PLL_CTRL = kCLOCK_ClockNeededRun;
    }

#if (USE_CASE_WITH_TIMER == 1)
    InitWakeupGPT();
#endif //USE_CASE_WITH_TIMER
#if (USE_CASE_WITH_I2C == 1)
    I2CInit(&master_rtos_handle);
#endif //USE_CASE_WITH_I2C

    /* Configure GPC */
    GPC_Init(BOARD_GPC_BASEADDR, APP_PowerUpSlot, APP_PowerDnSlot);
    GPC_EnableIRQ(BOARD_GPC_BASEADDR, SNVS_IRQn);
#if (USE_CASE_WITH_TIMER == 1)
    GPC_EnableIRQ(BOARD_GPC_BASEADDR, TIMER_IRQ);
#endif
#if (USE_CASE_WITH_I2C == 1)
    GPC_EnableIRQ(BOARD_GPC_BASEADDR, I2C_IRQ);
#endif //USE_CASE_WITH_I2C

    PRINTF("\r\n####################  LOW POWER DEMO ####################\n\r\n");
    PRINTF("    Build Time: %s--%s \r\n", __DATE__, __TIME__);
    PRINTF("Press ENTER to start the demo\r\n");
    GETCHAR();
    PRINTF("Starting...\r\n");
 
#if (USE_CASE_WITH_I2C == 1) || (USE_CASE_WITH_DUMMY_LOAD == 1)
    if (xTaskCreate(I2C_task, "I2C_task", configMINIMAL_STACK_SIZE + 100, NULL, I2C_task_PRIORITY, &I2C_task_handle) !=
        pdPASS)
    {
        PRINTF("Task creation failed!.\r\n");
        while (1)
            ;
    }
#endif //USE_CASE_WITH_I2C || USE_CASE_WITH_DUMMY_LOAD


    if (xTaskCreate(low_power_task, "Low_Power_task", configMINIMAL_STACK_SIZE + 100, NULL, low_power_task_PRIORITY, &low_power_task_handle) !=
        pdPASS)
    {
        PRINTF("Task creation failed!.\r\n");
        while (1)
            ;
    }
     vTaskStartScheduler();
    for (;;)
        ;
}

static void low_power_task(void *pvParameters)
{
    uint8_t wakeup_counter = 0;
    char stats[250];
    PRINTF("%s\r\n", __func__);

    for (;;)
    {
        if (wakeup_counter >= WAKEUP_COUNTER_MAX) {
#if (USE_CASE_WITH_ACORE_WAKEUP == 1)
            PRINTF("Waking up A core...\r\n");
            MU_SendMsg(MUB, 1, 2);
#endif //USE_CASE_WITH_ACORE_WAKEUP

            vTaskGetRunTimeStats(stats);
            PRINTF("%s\r\n", stats);
            wakeup_counter = 0;
        }

#if (USE_CASE_WITH_TIMER == 1)
        SetWakeupConfig(WAKEUP_TIMEOUT);
#endif //USE_CASE_WITH_TIMER

        LPM_MCORE_ChangeM4Clock(LPM_M4_LOW_FREQ);
        LPM_MCORE_SetPowerStatus(BOARD_GPC_BASEADDR, LPM_M4_STATE_STOP);
        PRINTF("Mode:%s\r\n", LPM_MCORE_GetPowerStatusString());
        PreSleepProcessing();
        __DSB();
        __ISB();
        __WFI();
        PostSleepProcessing();
        LPM_MCORE_SetPowerStatus(BOARD_GPC_BASEADDR, LPM_M4_STATE_RUN);
        LPM_MCORE_ChangeM4Clock(LPM_M4_HIGH_FREQ);
        PRINTF("Mode:%s\r\n", LPM_MCORE_GetPowerStatusString());

#if (USE_CASE_WITH_I2C == 1) || (USE_CASE_WITH_DUMMY_LOAD == 1)
        xTaskNotifyGive(I2C_task_handle);

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#endif //USE_CASE_WITH_I2C || USE_CASE_WITH_DUMMY_LOAD

        wakeup_counter++;
    }
}

#if (USE_CASE_WITH_I2C == 1) || (USE_CASE_WITH_DUMMY_LOAD == 1)
static void I2C_task(void *pvParameters)
{
    uint8_t master_buff[I2C_DATA_BUFF_LENGTH];
#if (USE_CASE_WITH_DUMMY_LOAD == 1)
    uint8_t tmp_buff[I2C_DATA_BUFF_LENGTH];
    int iter, i;
#endif //USE_CASE_WITH_DUMMY_LOAD
    PRINTF("%s\r\n", __func__);
    
    while(1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

#if (USE_CASE_WITH_I2C == 1)
        I2CReceive(&master_rtos_handle, master_buff);

        /* Set up i2c master to receive data from slave */
        for (uint32_t i = 0; i < I2C_DATA_BUFF_LENGTH; i++)
        {
            PRINTF("%d ", master_buff[i]);
        }
        PRINTF("\r\n");
#endif //USE_CASE_WITH_I2C

#if (USE_CASE_WITH_DUMMY_LOAD == 1)
        for (iter = 0; iter < DUMMY_ITERATIONS; iter++) {
            memcpy(tmp_buff, master_buff, sizeof(char) * I2C_DATA_BUFF_LENGTH);
    
            for (i = 0; i < I2C_DATA_BUFF_LENGTH - 1; i++) {
                tmp_buff[i] = tmp_buff[i+1] * tmp_buff[i] - tmp_buff[0] + tmp_buff[1];
                master_buff[i] = master_buff[i+1] * master_buff[i] - master_buff[0] + master_buff[1];
            }
    
            memcpy(master_buff, tmp_buff, sizeof(char) * I2C_DATA_BUFF_LENGTH);
        }
        PRINTF("End iterations\r\n\r\n");
#endif //USE_CASE_WITH_DUMMY_LOAD
        xTaskNotifyGive(low_power_task_handle);
    }
}
#endif //USE_CASE_WITH_I2C || USE_CASE_WITH_DUMMY_LOAD
