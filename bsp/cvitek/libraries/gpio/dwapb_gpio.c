/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023/10/19     xiunian      first version
 */
#include <rtthread.h>
#include <rthw.h>
#include "dwapb_gpio.h"

rt_ubase_t dwapb_gpio_base   = DWAPB_GPIOA_BASE;
rt_ubase_t dwapb_gpio_base_e = DWAPB_GPIOE_BASE;

void dwapb_gpio_set_pin_mode(rt_uint8_t port, rt_uint8_t bit, rt_uint8_t output)
{
    rt_ubase_t  base_addr = dwapb_gpio_port_base(port);
    rt_uint32_t reg_val;

    reg_val = dwapb_read32(base_addr + GPIO_SWPORTA_DDR);
    if (output)
        reg_val |= BIT(bit);
    else
        reg_val &= ~BIT(bit);
    dwapb_write32(base_addr + GPIO_SWPORTA_DDR, reg_val);
}

void dwapb_gpio_write_pin(rt_uint8_t port, rt_uint8_t bit, rt_uint8_t value)
{
    rt_ubase_t  base_addr = dwapb_gpio_port_base(port);
    rt_uint32_t reg_val;

    reg_val = dwapb_read32(base_addr + GPIO_SWPORTA_DR);
    reg_val = (value ? (reg_val | BIT(bit)) : (reg_val & (~BIT(bit))));
    dwapb_write32(base_addr + GPIO_SWPORTA_DR, reg_val);
}

rt_uint32_t dwapb_gpio_read_pin(rt_uint8_t port, rt_uint8_t bit)
{
    rt_ubase_t base_addr = dwapb_gpio_port_base(port);

    rt_uint32_t reg_val = dwapb_read32(GPIO_EXT_PORTA + base_addr);
    return ((reg_val >> (bit)) & 1);
}

void dwapb_gpio_toggle_trigger(rt_uint8_t port, rt_uint8_t bit)
{
    rt_uint8_t  val;
    rt_ubase_t  base_addr;
    rt_uint32_t pol;

    base_addr = dwapb_gpio_port_base(port);

    pol = dwapb_read32(base_addr + GPIO_INT_POLARITY);
    /* Just read the current value right out of the data register */
    val = (rt_uint8_t)((dwapb_read32(base_addr + GPIO_EXT_PORTA) >> (bit)) & 1);

    if (val)
        pol &= ~BIT(bit);
    else
        pol |= BIT(bit);

    dwapb_write32(base_addr + GPIO_INT_POLARITY, pol);
}

void dwapb_gpio_irq_mode(rt_uint8_t port, rt_uint8_t bit, rt_uint8_t level, rt_uint8_t rising,
                         rt_uint8_t both_edge)
{
    rt_ubase_t  base_addr = dwapb_gpio_port_base(port);
    rt_uint32_t it_val, ip_val;

    it_val = dwapb_read32(base_addr + GPIO_INTTYPE_LEVEL);
    ip_val = dwapb_read32(base_addr + GPIO_INT_POLARITY);

    if (level)
    {
        /* Enable level detection and select polarity */
        it_val = (it_val & (~BIT(bit)));
        ip_val = (rising ? (ip_val | BIT(bit)) : (ip_val & (~BIT(bit))));
    }
    else if (both_edge)
    {
        /* Disable level detection, select both edges */
        it_val = (it_val | BIT(bit));
    }
    else
    {
        /* Disable level detection, select single edge */
        it_val = (it_val | BIT(bit));
        ip_val = (rising ? (ip_val | BIT(bit)) : (ip_val & (~BIT(bit))));
    }

    dwapb_write32(base_addr + GPIO_INTTYPE_LEVEL, it_val);
    if (!both_edge)
        dwapb_write32(base_addr + GPIO_INT_POLARITY, ip_val);

    if (both_edge)
        dwapb_gpio_toggle_trigger(port, bit);
}

void dwapb_gpio_irq_enable(rt_uint8_t port, rt_uint8_t bit, rt_uint8_t enabled)
{
    rt_ubase_t  base_addr = dwapb_gpio_port_base(port);
    rt_uint32_t reg_val;

    reg_val = dwapb_read32(base_addr + GPIO_INTEN);
    reg_val = (enabled ? (reg_val | BIT(bit)) : (reg_val & (~BIT(bit))));
    dwapb_write32(base_addr + GPIO_INTEN, reg_val);
}

rt_uint32_t dwapb_gpio_irq_status(rt_uint8_t port)
{
    rt_ubase_t base_addr = dwapb_gpio_port_base(port);

    return dwapb_read32(base_addr + GPIO_INTSTATUS);
}

void dwapb_gpio_irq_eoi(rt_uint8_t port, rt_uint32_t mask)
{
    rt_ubase_t base_addr = dwapb_gpio_port_base(port);

    dwapb_write32(base_addr + GPIO_PORTA_EOI, mask);
}
