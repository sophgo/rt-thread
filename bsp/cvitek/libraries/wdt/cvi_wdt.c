/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2024/03/02     ShichengChu    first version
 */
#include <rtthread.h>
#include <rthw.h>
#include "cvi_wdt.h"
#include "mmio.h"

struct cvi_wdt_regs_t cv182x_wdt_reg = {
    .CR = 0x0,
    .TORR = 0x4,
    .CCVR = 0x8,
    .CRR = 0xc,
    .STAT = 0x10,
    .EOI = 0x14,
    .TOC = 0x1c,
};

struct cvi_wdt_regs_t *cvi_wdt_reg = &cv182x_wdt_reg;
static struct rt_spinlock cvi_wdt_reset_lock = RT_SPINLOCK_INIT;

static void cvi_wdt_reset_bit(rt_bool_t assert)
{
    rt_base_t level;
    uint32_t val;

    level = rt_spin_lock_irqsave(&cvi_wdt_reset_lock);
    val = mmio_read_32(CV_RST_REG);
    if (assert)
        val &= ~CV_RST_WDT;
    else
        val |= CV_RST_WDT;
    mmio_write_32(CV_RST_REG, val);
    rt_spin_unlock_irqrestore(&cvi_wdt_reset_lock, level);
}

static void cvi_wdt_reset_pulse(void)
{
    cvi_wdt_reset_bit(RT_TRUE);
    rt_hw_us_delay(10);
    cvi_wdt_reset_bit(RT_FALSE);
}

void cvi_wdt_top_setting(void)
{
    mmio_write_32(CV_TOP + CV_TOP_WDT_OFFSET, CV_TOP_WDT_VAL);

    cvi_wdt_reset_pulse();
}

rt_err_t cvi_wdt_reset(unsigned long reg_base)
{
    /* Linux DTS only maps RST_WDT to watchdog0 at 0x03010000. */
    if (reg_base != CVI_WDT0_BASE)
        return -RT_ENOSYS;

    cvi_wdt_reset_pulse();
    if (cvi_wdt_get_start(reg_base))
        return -RT_ERROR;

    return RT_EOK;
}

void cvi_wdt_start_en(unsigned long reg_base, int tor_mode)
{
    uint32_t control = WDT_CR(reg_base);

    if (tor_mode)
        control |= CVI_WDT_CR_LINE_MODE_Msk;
    else
        control &= ~CVI_WDT_CR_LINE_MODE_Msk;

    /* Caller programs TORR/TOC before requesting TOR mode. */
    WDT_CR(reg_base) = control | CVI_WDT_CR_WDT_ENABLE_En;
}

void cvi_wdt_start_dis(unsigned long reg_base)
{
    WDT_CR(reg_base) &= ~CVI_WDT_CR_WDT_ENABLE_En;
}

uint32_t cvi_wdt_get_start(unsigned long reg_base)
{
    return (WDT_CR(reg_base) & CVI_WDT_CR_WDT_ENABLE_Msk);
}

void cvi_wdt_set_timeout(unsigned long reg_base, uint32_t value)
{
    WDT_TORR(reg_base) &= ~CVI_WDT_TORR_WDT_TORR_Pos;
    WDT_TORR(reg_base) = ((value << CVI_WDT_TORR_WDT_ITORR_Pos) | (value << CVI_WDT_TORR_WDT_TORR_Pos));
}

void cvi_wdt_set_timeout_counter(unsigned long reg_base, uint32_t value)
{
    WDT_TOC(reg_base) = value;
}

void cvi_wdt_set_respond_system_reset(unsigned long reg_base)
{
    WDT_CR(reg_base) &= ~CVI_WDT_CR_WDT_RESPOND_IRQ_THEN_RST;
}

void cvi_wdt_set_respond_irq_then_reset(unsigned long reg_base)
{
    WDT_CR(reg_base) |= CVI_WDT_CR_WDT_RESPOND_IRQ_THEN_RST;
}

void cvi_wdt_set_reset_pulse_width(unsigned long reg_base, uint32_t value)
{
    WDT_CR(reg_base) &= ~CVI_WDT_CR_WDT_RESET_PULSE_WIDTH_Msk;
    WDT_CR(reg_base) |= (value << CVI_WDT_CR_WDT_RESET_PULSE_WIDTH_Pos);
}

void cvi_wdt_feed_en(unsigned long reg_base)
{
    WDT_CRR(reg_base) = CVI_WDT_CRR_FEED_En;
}

uint32_t cvi_wdt_get_counter_value(unsigned long reg_base)
{
    return (WDT_CCVR(reg_base) & CVI_WDT_CCVR_COUNTER_Msk);
}

uint32_t cvi_wdt_get_irq_stat(unsigned long reg_base)
{
    return (WDT_STAT(reg_base) & CVI_WDT_STAT_IRQ_STAT_Msk);
}

void cvi_wdt_clr_irq_en(unsigned long reg_base)
{
    WDT_EOI(reg_base);
}
