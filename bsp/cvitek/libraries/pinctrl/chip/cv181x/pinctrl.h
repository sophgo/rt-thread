/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2020. All rights reserved.
 *
 * File Name: pinctrl.h
 * Description:
 */

#ifndef __PINCTRL_CV181X_H__
#define __PINCTRL_CV181X_H__

//#include "../core.h"
#include "cv181x_pinlist_swconfig.h"
#include "cv181x_reg_fmux_gpio.h"
#include "cv181x_reg_ioblk.h"
#include "mmio.h"

#define  PAD_MIPI_TXM4__MIPI_TXM4 0
#define  PAD_MIPI_TXP4__MIPI_TXP4 0
#define  PAD_MIPI_TXM3__MIPI_TXM3 0
#define  PAD_MIPI_TXP3__MIPI_TXP3 0
#define  PAD_MIPI_TXM2__MIPI_TXM2 0
#define  PAD_MIPI_TXP2__MIPI_TXP2 0
#define  PAD_MIPI_TXM1__MIPI_TXM1 0
#define  PAD_MIPI_TXP1__MIPI_TXP1 0
#define  PAD_MIPI_TXM0__MIPI_TXM0 0
#define  PAD_MIPI_TXP0__MIPI_TXP0 0

#if defined(ARCH_ARM)
extern rt_ubase_t pinmux_base_ioremap(void);
#define PINMUX_BASE pinmux_base_ioremap()
#else
#define PINMUX_BASE 0x03001000
#endif /* defined(ARCH_ARM) */
#define PINMUX_MASK(PIN_NAME) FMUX_GPIO_FUNCSEL_##PIN_NAME##_MASK
#define PINMUX_OFFSET(PIN_NAME) FMUX_GPIO_FUNCSEL_##PIN_NAME##_OFFSET
#define PINMUX_VALUE(PIN_NAME, FUNC_NAME) PIN_NAME##__##FUNC_NAME
#define PINMUX_CONFIG(PIN_NAME, FUNC_NAME) \
        mmio_clrsetbits_32(PINMUX_BASE + FMUX_GPIO_FUNCSEL_##PIN_NAME, \
        PINMUX_MASK(PIN_NAME) << PINMUX_OFFSET(PIN_NAME), \
        PINMUX_VALUE(PIN_NAME, FUNC_NAME))

/* IOBLK pad control. Pass the pin name; which group (G1/G7/G10/G11/G12/GRTC)
 * the pin belongs to is resolved by <chip>_reg_ioblk.h and never has to be
 * known here.
 *
 *   IOBLK_PAD_ADDR(ADC1)                 -> address of the ADC1 pad register
 *   mmio_write_32(IOBLK_PAD_ADDR(ADC1), 0);          // clear PU and PD
 *   mmio_setbits_32(IOBLK_PAD_ADDR(UART0_RX), IOBLK_PAD_PU_MASK(UART0_RX));
 *
 * A pin without the requested field fails to compile, which is intended: the
 * two chips do not expose the same pins.
 */
#define IOBLK_PAD_ADDR(PIN)     IOBLK_PAD_##PIN##_ADDR
#define IOBLK_PAD_PULL_MASK(PIN) IOBLK_PAD_##PIN##_PULL_MASK
#define IOBLK_PAD_PU_MASK(PIN)  IOBLK_PAD_##PIN##_PU_MASK
#define IOBLK_PAD_PD_MASK(PIN)  IOBLK_PAD_##PIN##_PD_MASK
#endif /* __PINCTRL_CV181X_H__ */
