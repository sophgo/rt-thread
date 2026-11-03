/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024/02/22     flyingcys    first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "cvi_adc.h"
#include "mmio.h"

#define DBG_LEVEL   DBG_LOG
#include <rtdbg.h>
#define LOG_TAG "DRV.ADC"

#define SARADC_EFUSE_TRIM_BASE 0x03050118
#define TOP_SARADC_TRIM_MASK 0xf0000000
#define TOP_SARADC_TRIM_OFFSET 28
#define RTC_SARADC_TRIM_MASK 0x0f000000
#define RTC_SARADC_TRIM_OFFSET 24

void cvi_adc_channel_enable(unsigned long reg_base, rt_int8_t channel, rt_bool_t enable)
{
    rt_uint32_t mask;
    rt_uint32_t value;

    /* sel is a one-hot field: bit5 = ch1, bit6 = ch2, bit7 = ch3 */
    mask = 1 << (SARADC_CTRL_SEL_POS + channel);
    value = mmio_read_32(reg_base + SARADC_CTRL_OFFSET);
    if (enable)
        value |= mask;
    else
        value &= ~mask;
    mmio_write_32(reg_base + SARADC_CTRL_OFFSET, value);
}

void cvi_adc_start(unsigned long reg_base)
{
    mmio_setbits_32(reg_base + SARADC_CTRL_OFFSET, SARADC_CTRL_START);
}

rt_uint32_t cvi_adc_get_status(unsigned long reg_base)
{
    return((rt_uint32_t)mmio_read_32(reg_base + SARADC_STATUS_OFFSET));
}

void cvi_adc_set_cyc(unsigned long reg_base)
{
    rt_uint32_t value;

    value = mmio_read_32(reg_base + SARADC_CYC_SET_OFFSET);

    value &= ~SARADC_CYC_CLKDIV_DIV_16;
    mmio_write_32(reg_base + SARADC_CYC_SET_OFFSET, value);

    value |= SARADC_CYC_CLKDIV_DIV_16;                                                               //set saradc clock cycle=840ns
    mmio_write_32(reg_base + SARADC_CYC_SET_OFFSET, value);
}

rt_err_t cvi_adc_set_trim(unsigned long reg_base)
{
    rt_uint32_t efuse_val;
    rt_uint32_t trim;

    /* The trim value is programmed into efuse at production time; all this does
     * is copy it into the trim register of the TOP or RTC domain controller.
     */
    efuse_val = mmio_read_32(SARADC_EFUSE_TRIM_BASE);

    if (reg_base == SARADC_BASE)
        trim = (efuse_val & TOP_SARADC_TRIM_MASK) >> TOP_SARADC_TRIM_OFFSET;
    else if (reg_base == RTC_ADC_BASE)
        trim = (efuse_val & RTC_SARADC_TRIM_MASK) >> RTC_SARADC_TRIM_OFFSET;
    else
    {
        LOG_E("Wrong saradc base address: 0x%x", reg_base);
        return -RT_EINVAL;
    }

    LOG_D("Setting trim 0x%x for saradc 0x%x", trim, reg_base);
    mmio_write_32(reg_base + SARADC_TRIM_OFFSET, trim);

    return RT_EOK;
}

rt_uint32_t cvi_adc_read_result(unsigned long reg_base, rt_int8_t channel)
{
    return mmio_read_32(reg_base + SARADC_RESULT(channel - 1));
}
