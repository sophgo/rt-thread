/*
 * Copyright (C) 2017-2020 Alibaba Group Holding Limited
 */

/******************************************************************************
 * @file     soc.h
 * @brief    CSI Core Peripheral Access Layer Header File for
 *           CSKYSOC Device Series
 * @version  V1.0
 * @date     7. April 2020
 ******************************************************************************/

#ifndef __SOC_CV180X_H__
#define __SOC_CV180X_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EHS_VALUE
#define EHS_VALUE 20000000U
#endif

#ifndef ELS_VALUE
#define ELS_VALUE 32768U
#endif

#ifndef IHS_VALUE
#define IHS_VALUE 24000000U
#endif

#ifndef ILS_VALUE
#define ILS_VALUE 32768U
#endif

#define CONFIG_IRQ_NUM      1023U
#define CONFIG_REAL_IRQ_CNT 105

#define PIN_MUX_BASE     0x03001000UL
#define PIN_CFG_BASE     0x03001000UL
#define PWR_PIN_CFG_BASE 0x05027000UL

#define DW_GPIO0_BASE    0x03020000UL
#define DW_GPIO1_BASE    0x03021000UL
#define DW_GPIO2_BASE    0x03022000UL
#define DW_GPIO3_BASE    0x03023000UL
#define DW_RTC_GPIO_BASE 0x05021000UL

#define CVI_WDT0_BASE    0x03010000
#define CVI_WDT1_BASE    0x03011000
#define CVI_WDT2_BASE    0x03012000
#define CVI_RTC_WDT_BASE 0x0502D000
#define CVI_WDT_SIZE     0x1000

#define CVI_TEMPSEN_BASE 0x030E0000
#define CVI_TEMPSEN_SIZE 0x10000

#define DW_UART0_BASE 0x04140000UL
#define DW_UART0_SIZE 0x1000U

#define DW_UART1_BASE 0x04150000UL
#define DW_UART1_SIZE 0x1000U

#define DW_UART2_BASE 0x04160000UL
#define DW_UART2_SIZE 0x1000U

#define DW_UART3_BASE 0x04170000UL
#define DW_UART3_SIZE 0x1000U

#define DW_UART4_BASE 0x041C0000UL
#define DW_UART4_SIZE 0x1000U

#define DW_IIC0_BASE 0x04000000UL
#define DW_IIC0_SIZE 0x1000U

#define DW_IIC1_BASE 0x04010000UL
#define DW_IIC1_SIZE 0x1000U

#define DW_IIC2_BASE 0x04020000UL
#define DW_IIC2_SIZE 0x1000U

#define DW_IIC3_BASE 0x04030000UL
#define DW_IIC3_SIZE 0x1000U

#define DW_IIC4_BASE 0x04040000UL
#define DW_IIC4_SIZE 0x1000U

#define DW_IIC5_BASE 0x0502B000UL
#define DW_IIC5_SIZE 0x1000U

#define DW_SPI_REG_SIZE (0x10000UL)
#define DW_SPI0_BASE    (0x04180000UL)
#define DW_SPI1_BASE    (DW_SPI0_BASE + 1 * DW_SPI_REG_SIZE)
#define DW_SPI2_BASE    (DW_SPI0_BASE + 2 * DW_SPI_REG_SIZE)
#define DW_SPI3_BASE    (DW_SPI0_BASE + 3 * DW_SPI_REG_SIZE)

#define DW_MAC_BASE 0x04070000UL
#define DW_MAC_SIZE 0x10000U

#define ADC_BASE     0x030F0000
#define RTC_ADC_BASE 0x0502C000
#define ADC_SIZE     0x1000

#define CVI_PWM0_BASE 0x03060000
#define CVI_PWM1_BASE 0x03061000
#define CVI_PWM2_BASE 0x03062000
#define CVI_PWM3_BASE 0x03063000

#define CONFIG_PLIC_BASE (0x70000000UL)
#define PLIC_BASE        (0x70000000UL)
#define CORET_BASE       (PLIC_BASE + 0x4000000UL) /*!< CORET Base Address */
#define PLIC             ((PLIC_Type *)PLIC_BASE)

#define DW_SDIO0_BASE 0x04320000
#define DW_SDIO1_BASE 0x04310000
#define DW_SDIO2_BASE 0x04300000

#define BSP_PLIC_PHY_ADDR (0x70000000UL)

/* ================================================================================ */
/* ================                  otp declaration               ================ */
/* ================================================================================ */
#define CONFIG_OTP_BASE_ADDR 0U
#define CONFIG_OTP_BANK_SIZE 256U  // Bytes

#ifdef __cplusplus
}
#endif

#endif /* __SOC_CV180X_H__ */
