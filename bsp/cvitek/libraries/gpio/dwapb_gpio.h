/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023/10/19     xiunian      first version
 */
#ifndef __DWAPB_GPIO_H__
#define __DWAPB_GPIO_H__

#include <rtthread.h>
#include <rthw.h>

#define GPIO_SWPORTA_DR  0x00
#define GPIO_SWPORTA_DDR 0x04

#define GPIO_INTEN         0x30
#define GPIO_INTTYPE_LEVEL 0x38
#define GPIO_INT_POLARITY  0x3c
#define GPIO_INTSTATUS     0x40
#define GPIO_PORTA_EOI     0x4c
#define GPIO_EXT_PORTA     0x50

#define DWAPB_GPIOA_BASE 0x03020000
#define DWAPB_GPIOE_BASE 0x05021000
#define DWAPB_GPIO_SIZE  0x1000

#define DWAPB_GPIO_PORT_NR 5
#define DWAPB_GPIO_NR      32

#define DWAPB_GPIO_PORT_E 4

/*
 * rt_pin_* pin numbers equal the Linux gpio numbers: A(port 0)=480, B=448, C=416, D=384,
 * E(port 4, PWR_GPIO)=352, plus the bit inside the port.
 */
#define DWAPB_PIN_BASE          480
#define DWAPB_PIN_NUM(port, no) (DWAPB_PIN_BASE - (int)(port) * DWAPB_GPIO_NR + (int)(no))
#define DWAPB_PIN_PORT(pin)     (DWAPB_PIN_BASE / DWAPB_GPIO_NR - ((pin) >> 5))
#define DWAPB_PIN_BIT(pin)      ((pin) & (DWAPB_GPIO_NR - 1))

#ifndef BIT
#define BIT(x) (1UL << (x))
#endif

rt_inline rt_uint32_t dwapb_read32(rt_ubase_t addr)
{
    return HWREG32(addr);
}

rt_inline void dwapb_write32(rt_ubase_t addr, rt_uint32_t value)
{
    HWREG32(addr) = value;
}

rt_inline rt_ubase_t dwapb_gpio_port_base(rt_uint8_t port)
{
    extern rt_ubase_t dwapb_gpio_base;
    extern rt_ubase_t dwapb_gpio_base_e;

    return (port == DWAPB_GPIO_PORT_E ? dwapb_gpio_base_e
                                      : (dwapb_gpio_base + DWAPB_GPIO_SIZE * port));
}

void dwapb_gpio_set_pin_mode(rt_uint8_t port, rt_uint8_t bit, rt_uint8_t output);
void dwapb_gpio_write_pin(rt_uint8_t port, rt_uint8_t bit, rt_uint8_t value);
rt_uint32_t dwapb_gpio_read_pin(rt_uint8_t port, rt_uint8_t bit);
void dwapb_gpio_toggle_trigger(rt_uint8_t port, rt_uint8_t bit);
void dwapb_gpio_irq_mode(rt_uint8_t port, rt_uint8_t bit, rt_uint8_t level, rt_uint8_t rising,
                         rt_uint8_t both_edge);
void dwapb_gpio_irq_enable(rt_uint8_t port, rt_uint8_t bit, rt_uint8_t enabled);
rt_uint32_t dwapb_gpio_irq_status(rt_uint8_t port);
void dwapb_gpio_irq_eoi(rt_uint8_t port, rt_uint32_t mask);

#endif /* __DWAPB_GPIO_H__ */
