/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-02-14     ShichengChu  first version
 */
#include "dw_iic.h"

#include <rtthread.h>

#define DBG_TAG "dw.iic"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

rt_uint32_t dw_iic_wait_for_bb(dw_iic_regs_t *iic_base)
{
    uint16_t timeout = 0;
    while ((iic_base->IC_STATUS & DW_IIC_MST_ACTIVITY_STATE) ||
           !(iic_base->IC_STATUS & DW_IIC_TXFIFO_EMPTY_STATE)) {
        /* Evaluate timeout */
        rt_hw_us_delay(5);
        timeout++;
        if (timeout > 200) {
            /* exceed 1 ms */
            LOG_E("Timed out waiting for bus busy");
            return 1;
        }
    }

    return 0;
}

void dw_iic_set_reg_address(dw_iic_regs_t *iic_base, rt_uint32_t addr, uint8_t addr_len)
{
    while (addr_len) {
        addr_len--;
        /* high byte address going out first */
        dw_iic_transmit_data(iic_base, (addr >> (addr_len * 8)) & 0xff);
    }
}

void dw_iic_set_target_address(dw_iic_regs_t *iic_base, rt_uint32_t address)
{
    rt_uint32_t i2c_status;
    i2c_status = dw_iic_get_iic_status(iic_base);
    dw_iic_disable(iic_base);
    iic_base->IC_TAR = (iic_base->IC_TAR & ~0x3ff) |
                       address; /* this register can be written only when the I2C is disabled*/

    if (i2c_status == DW_IIC_EN) {
        dw_iic_enable(iic_base);
    }
}

void dw_iic_set_slave_mode(dw_iic_regs_t *iic_base)
{
    rt_uint32_t i2c_status;
    i2c_status = dw_iic_get_iic_status(iic_base);
    dw_iic_disable(iic_base);
    rt_uint32_t val = DW_IIC_CON_MASTER_EN | DW_IIC_CON_SLAVE_EN;
    iic_base->IC_CON &= ~val;  ///< set 0 to disabled master mode; set 0 to enabled slave mode

    if (i2c_status == DW_IIC_EN) {
        dw_iic_enable(iic_base);
    }
}

void dw_iic_set_master_mode(dw_iic_regs_t *iic_base)
{
    rt_uint32_t i2c_status;
    i2c_status = dw_iic_get_iic_status(iic_base);
    dw_iic_disable(iic_base);
    rt_uint32_t val =
        DW_IIC_CON_MASTER_EN |
        DW_IIC_CON_SLAVE_EN;  ///< set 1 to enabled master mode; set 1 to disabled slave mode
    iic_base->IC_CON |= val;

    if (i2c_status == DW_IIC_EN) {
        dw_iic_enable(iic_base);
    }
}

void dump_iic_status(dw_iic_regs_t *iic_base)
{
    rt_kprintf("====== dump iic status ======\n");
    rt_kprintf(
        "iic enable: 0x%x,\niic con:0x%x\nintr status: 0x%x\nraw intr status: 0x%x\nintr mask: "
        "0x%x\nstatus:0x%x\n",
        dw_iic_get_iic_status(iic_base), iic_base->IC_CON, dw_iic_get_intrrupt_state(iic_base),
        iic_base->IC_RAW_INTR_STAT, iic_base->IC_INTR_MASK, iic_base->IC_STATUS);
    rt_kprintf("==== dump iic status end ====\n");
}

static void dw_iic_set_transfer_speed_high(dw_iic_regs_t *iic_base)
{
    rt_uint32_t speed_config = iic_base->IC_CON;
    speed_config &= ~(DW_IIC_CON_SPEEDL_EN | DW_IIC_CON_SPEEDH_EN);
    speed_config |= DW_IIC_CON_SPEEDL_EN | DW_IIC_CON_SPEEDH_EN;
    iic_base->IC_CON = speed_config;
}

static void dw_iic_set_transfer_speed_fast(dw_iic_regs_t *iic_base)
{
    rt_uint32_t speed_config = iic_base->IC_CON;
    speed_config &= ~(DW_IIC_CON_SPEEDL_EN | DW_IIC_CON_SPEEDH_EN);
    speed_config |= DW_IIC_CON_SPEEDH_EN;
    iic_base->IC_CON = speed_config;
}

static void dw_iic_set_transfer_speed_standard(dw_iic_regs_t *iic_base)
{
    rt_uint32_t speed_config = iic_base->IC_CON;
    speed_config &= ~(DW_IIC_CON_SPEEDL_EN | DW_IIC_CON_SPEEDH_EN);
    speed_config |= DW_IIC_CON_SPEEDL_EN;
    iic_base->IC_CON = speed_config;
}

rt_err_t dw_iic_set_speed(dw_iic_regs_t *iic_base, rt_uint32_t speed)
{
    rt_uint32_t tf = 300;  // falling time = 300ns

    if (speed == 100 * 1000) {
        dw_iic_set_transfer_speed_standard(iic_base);
        dw_iic_set_standard_scl_hcnt(iic_base, ((IC_CLK * (4000U + tf) + 500U) / 1000U) - 3U);
        dw_iic_set_standard_scl_lcnt(iic_base, ((IC_CLK * (4700U + tf) + 500U) / 1000U) - 1U);
    } else if (speed == 400 * 1000) {
        dw_iic_set_transfer_speed_fast(iic_base);
        dw_iic_set_fast_scl_hcnt(iic_base, ((IC_CLK * (600U + tf) + 500U) / 1000U) - 3U);
        dw_iic_set_fast_scl_lcnt(iic_base, ((IC_CLK * (1300U + tf) + 500U) / 1000U) - 1U);
    } else if (speed == 4 * 1000 * 1000) {
        dw_iic_set_transfer_speed_high(iic_base);
        dw_iic_set_high_scl_hcnt(iic_base, ((IC_CLK * (160U + tf) + 500U) / 1000U) - 3U);
        dw_iic_set_high_scl_lcnt(iic_base, ((IC_CLK * (320U + tf) + 500U) / 1000U) - 1U);
    } else
        return -RT_ERROR;
    return RT_EOK;
}

rt_uint32_t dw_iic_get_speed(dw_iic_regs_t *iic_base)
{
    rt_uint32_t speed_config = iic_base->IC_CON;
    if (speed_config & (DW_IIC_CON_SPEEDH_EN | DW_IIC_CON_SPEEDL_EN))
        return 4 * 1000 * 1000;
    else if (speed_config & DW_IIC_CON_SPEEDH_EN)
        return 400 * 1000;
    else if (speed_config & DW_IIC_CON_SPEEDL_EN)
        return 100 * 1000;
    else {
        LOG_E("dw_iic_get_speed: unknown speed config %x\n", speed_config);
        return -1;
    }
}

void dw_iic_init(dw_iic_regs_t *iic_base, rt_uint32_t speed)
{
    dw_iic_disable(iic_base);
    dw_iic_clear_all_irq(iic_base);
    dw_iic_disable_all_irq(iic_base);

    iic_base->IC_SAR = 0;

    dw_iic_set_receive_fifo_threshold(iic_base, 0x1);
    dw_iic_set_transmit_fifo_threshold(iic_base, 0x0);
    dw_iic_set_sda_hold_time(iic_base, 0x1e);

    dw_iic_set_master_mode(iic_base);
    dw_iic_enable_restart(iic_base);

    dw_iic_set_speed(iic_base, speed);
}

void dw_iic_slave_init(dw_iic_regs_t *iic_base, uint8_t slave_addr)
{
    dw_iic_disable(iic_base);
    dw_iic_clear_all_irq(iic_base);
    dw_iic_disable_all_irq(iic_base);

    iic_base->IC_SAR = slave_addr;

    dw_iic_set_receive_fifo_threshold(iic_base, 0x1);
    dw_iic_set_transmit_fifo_threshold(iic_base, 0x0);
    dw_iic_set_sda_hold_time(iic_base, 0x1e);

    dw_iic_set_slave_mode(iic_base);
    dw_iic_set_slave_7bit_addr_mode(iic_base);
    dw_iic_enable(iic_base);
}
