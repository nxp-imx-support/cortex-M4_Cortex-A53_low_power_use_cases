/*
 * Copyright 2018-2020 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LOW_POWER_USECASES_H_
#define _LOW_POWER_USECASES_H_

#define USE_CASE_WITH_TIMER             1
#if (USE_CASE_WITH_TIMER == 1)
    #define USE_CASE_WITH_I2C           1
    #define USE_CASE_WITH_DUMMY_LOAD    1
    #define USE_CASE_WITH_ACORE_WAKEUP  1
#endif


// RDC defines
#define RDC_DISABLE_A53_ACCESS  0xFC
#define RDC_DISABLE_m4_ACCESS   0xF3
#define PERIPH_CONSOLE          kRDC_Periph_UART4
#define PERIPH_TIMER            kRDC_Periph_GPT1
#define PERIPH_I2C              kRDC_Periph_I2C3

// Timer defines
#define TIMER           GPT1
#define TIMER_CLOCK     kCLOCK_RootGpt1
#define TIMER_CLK_FREQ  (OSC24M_CLK_FREQ) / (CLOCK_GetRootPreDivider(TIMER_CLOCK)) / \
                        (CLOCK_GetRootPostDivider(TIMER_CLOCK))
#define TIMER_IRQ       GPT1_IRQn

// I2C defines
#define I2C_MASTER                      ((I2C_Type *)I2C3)
#define I2C_CLOCK                       kCLOCK_RootI2c3
#define I2C_MASTER_CLK_FREQ             (OSC24M_CLK_FREQ) / (CLOCK_GetRootPreDivider(I2C_CLOCK)) / \
                                        (CLOCK_GetRootPostDivider(I2C_CLOCK))
#define I2C_IRQ                         I2C3_IRQn
#define I2C_IRQ_PRIO                    3
#define I2C_BAUDRATE                    (100000) /* 100K */
//#define I2C_MASTER_SLAVE_ADDR_7BIT    (0x0U)
#define I2C_MASTER_SLAVE_ADDR_7BIT      (0x20U)
#define I2C_DATA_LENGTH                 (1U)     /* MAX is 256 */
#define I2C_DATA_BUFF_LENGTH            (8U)

// LPM defines
#define WAKEUP_COUNTER_MAX      200
#define WAKEUP_TIMEOUT          20

#define DUMMY_ITERATIONS        18000


#define EXAMPLE_LED_GPIO     GPIO5
#define EXAMPLE_LED_GPIO_PIN 11U

#endif //_LOW_POWER_USECASES_H_
