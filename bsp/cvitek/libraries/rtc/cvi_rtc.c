/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-03-04   ShichengChu     the first version
 */
#include <rtdevice.h>
#include <rtthread.h>

#define DBG_TAG "DRV.RTC"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#include "cvi_rtc.h"
#include "mmio.h"

void hal_cvi_rtc_clk_set(int enable)
{
    rt_uint32_t clk_state;

    clk_state = mmio_read_32((long unsigned int)CLK_EN_0);

    if (enable)
        clk_state |= CLK_RTC_25M_BIT;
    else
        clk_state &= ~(CLK_RTC_25M_BIT);

    mmio_write_32((long unsigned int)CLK_EN_0, clk_state);
}

#if defined(RTC_USING_SW_CALIB)
void hal_cvi_rtc_32k_coarse_value_calib(uintptr_t rtc_base, uintptr_t rtc_ctrl_base)
{
    rt_uint32_t analog_calib_value = 0;
    rt_uint32_t fc_coarse_time1    = 0;
    rt_uint32_t fc_coarse_time2    = 0;
    rt_uint32_t fc_coarse_value    = 0;
    rt_uint32_t offset             = 128;
    rt_uint32_t value              = 0;

    mmio_write_32(rtc_base + CVI_RTC_ANA_CALIB, 0x10100);
    rt_thread_mdelay(1);

    // Select 32K OSC tuning value source from rtc_sys
    value = mmio_read_32(rtc_base + CVI_RTC_SEC_PULSE_GEN) & ~(1 << 31);
    mmio_write_32(rtc_base + CVI_RTC_SEC_PULSE_GEN, value);

    analog_calib_value = mmio_read_32(rtc_base + CVI_RTC_ANA_CALIB);

    mmio_write_32(rtc_ctrl_base + CVI_RTC_FC_COARSE_EN, 1);

    while (1) {
        fc_coarse_time1 = mmio_read_32(rtc_ctrl_base + CVI_RTC_FC_COARSE_CAL);
        fc_coarse_time1 >>= 16;

        while (fc_coarse_time2 <= fc_coarse_time1) {
            fc_coarse_time2 = mmio_read_32(rtc_ctrl_base + CVI_RTC_FC_COARSE_CAL);
            fc_coarse_time2 >>= 16;
        }

        rt_thread_mdelay(1);
        fc_coarse_value = mmio_read_32(rtc_ctrl_base + CVI_RTC_FC_COARSE_CAL);
        fc_coarse_value &= 0xFFFF;

        if (fc_coarse_value > 770) {
            analog_calib_value += offset;
            offset >>= 1;
            mmio_write_32(rtc_base + CVI_RTC_ANA_CALIB, analog_calib_value);
        } else if (fc_coarse_value < 755) {
            analog_calib_value -= offset;
            offset >>= 1;
            mmio_write_32(rtc_base + CVI_RTC_ANA_CALIB, analog_calib_value);
        } else {
            mmio_write_32(rtc_ctrl_base + CVI_RTC_FC_COARSE_EN, 0);
            break;
        }
        if (offset == 0) {
            LOG_E("RTC calib failed\n");
            break;
        }
    }
}

void hal_cvi_rtc_32k_fine_value_calib(uintptr_t rtc_base, uintptr_t rtc_ctrl_base)
{
    rt_uint32_t fc_fine_time1 = 0;
    rt_uint32_t fc_fine_time2 = 0;
    rt_uint32_t fc_fine_value = 0;

    rt_uint64_t freq = 256000000000;
    rt_uint32_t sec_cnt;
    rt_uint32_t frac_ext = 10000;

    mmio_write_32(rtc_ctrl_base + CVI_RTC_FC_FINE_EN, 1);

    fc_fine_time1 = mmio_read_32(rtc_ctrl_base + CVI_RTC_FC_FINE_CAL);
    fc_fine_time1 >>= 24;

    while (fc_fine_time2 <= fc_fine_time1) {
        fc_fine_time2 = mmio_read_32(rtc_ctrl_base + CVI_RTC_FC_FINE_CAL);
        fc_fine_time2 >>= 24;
    }

    fc_fine_value = mmio_read_32(rtc_ctrl_base + CVI_RTC_FC_FINE_CAL);
    fc_fine_value &= 0xFFFFFF;

    // Frequency = 256 / (RTC_FC_FINE_VALUE x 40ns)
    // freq = 256000000000 / 40;
    // freq = (freq * frac_ext) / fc_fine_value;

    // sec_cnt = ((freq / frac_ext) << 8) + (((freq % frac_ext) * 256) / frac_ext & 0xFF);

    /* Fix a data size overflow error.*/
    do_div(freq, 40);
    freq = freq * frac_ext;
    do_div(freq, fc_fine_value);
    sec_cnt = ((do_div(freq, frac_ext) * 256) / frac_ext & 0xFF) + (freq << 8);

    mmio_write_32(rtc_base + CVI_RTC_SEC_PULSE_GEN, sec_cnt);
    mmio_write_32(rtc_ctrl_base + CVI_RTC_FC_FINE_EN, 0);
}
#endif

void hal_cvi_rtc_enable_sec_counter(uintptr_t rtc_base)
{
#if defined(RTC_USING_SW_CALIB)
    rt_uint32_t value = 0;

    value = mmio_read_32(rtc_base + CVI_RTC_SEC_PULSE_GEN) & ~(1 << 31);
    mmio_write_32(rtc_base + CVI_RTC_SEC_PULSE_GEN, value);

    value = mmio_read_32(rtc_base + CVI_RTC_ANA_CALIB) & ~(1 << 31);
    mmio_write_32(rtc_base + CVI_RTC_ANA_CALIB, value);

    mmio_read_32(rtc_base + CVI_RTC_SEC_CNTR_VALUE);
    mmio_write_32(rtc_base + CVI_RTC_ALARM_ENABLE, 0x0);
#endif
#if defined(RTC_USING_HW_CALIB)
    mmio_write_32(RTCSYS_F32KLESS_BASE + REG_PERIOD_ENABLE, 0x1);
    mmio_write_32(RTCSYS_F32KLESS_BASE + REG_CLK25M_STABLE, 0x40);
    mmio_write_32(RTCSYS_F32KLESS_BASE + REG_PERIOD_WIDTH, 0x400);
    mmio_write_32(RTCSYS_F32KLESS_BASE + REG_SECTICK_CONFIG, 0x100);
    mmio_write_32(CVI_RTC_BASE + CVI_RTC_ANA_CALIB, 0x80030100);
    mmio_write_32(CVI_RTC_BASE + CVI_RTC_SEC_PULSE_GEN, 0x80800000);
#endif
}

void hal_cvi_rtc_set_time(uintptr_t rtc_base, unsigned long sec)
{
    mmio_write_32(rtc_base + CVI_RTC_SET_SEC_CNTR_VALUE, sec);
    mmio_write_32(rtc_base + CVI_RTC_SET_SEC_CNTR_TRIG, 1);
    mmio_write_32(rtc_base + RTC_MACRO_RG_SET_T, sec);
    mmio_write_32(rtc_base + RTC_MACRO_DA_CLEAR_ALL, 1);
    mmio_write_32(rtc_base + RTC_MACRO_DA_SOC_READY, 1);
    mmio_write_32(rtc_base + RTC_MACRO_DA_CLEAR_ALL, 0);
    mmio_write_32(rtc_base + RTC_MACRO_RG_SET_T, 0);
    mmio_write_32(rtc_base + RTC_MACRO_DA_SOC_READY, 0);
}

int hal_cvi_rtc_get_time_sec(uintptr_t rtc_base, unsigned long *ret_sec)
{
    *ret_sec = mmio_read_32(rtc_base + CVI_RTC_SEC_CNTR_VALUE);

    return RT_EOK;
}

void hal_cvi_rtc_alarm_enable(uintptr_t rtc_base, rt_bool_t enable)
{
    mmio_write_32(rtc_base + CVI_RTC_ALARM_ENABLE, enable);
}

void hal_cvi_rtc_set_alarm_time(uintptr_t rtc_base, unsigned long sec)
{
    mmio_write_32(rtc_base + CVI_RTC_ALARM_TIME, sec);
}

unsigned long hal_cvi_rtc_get_alarm_time(uintptr_t rtc_base)
{
    return mmio_read_32(rtc_base + CVI_RTC_ALARM_TIME);
}
