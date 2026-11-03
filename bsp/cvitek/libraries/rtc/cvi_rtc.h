/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-03-04   ShichengChu     the first version
 */
#ifndef __CVI_RTC_H__
#define __CVI_RTC_H__

#include <rtthread.h>
#include "mmio.h"

#define CVI_RTC_BASE         0x05026000U
#define RTCSYS_F32KLESS_BASE 0x0502A000U
#define RTC_ALARM_O          17
#define CVI_RTC_CTRL_BASE    0x05025000U
#define CLK_EN_0             0x03002000U
#define CLK_RTC_25M_BIT      (1 << 8)

/* CVITEK RTC registers */
#define CVI_RTC_ANA_CALIB          0x0
#define CVI_RTC_SEC_PULSE_GEN      0x4
#define CVI_RTC_ALARM_TIME         0x8
#define CVI_RTC_ALARM_ENABLE       0xC
#define CVI_RTC_SET_SEC_CNTR_VALUE 0x10
#define CVI_RTC_SET_SEC_CNTR_TRIG  0x14
#define CVI_RTC_SEC_CNTR_VALUE     0x18
#define CVI_RTC_APB_RDATA_SEL      0x3C
#define CVI_RTC_POR_DB_MAGIC_KEY   0x68
#define CVI_RTC_EN_PWR_WAKEUP      0xBC
#define CVI_RTC_PWR_DET_SEL        0x140

/* CVITEK RTC MACRO registers */
#define RTC_MACRO_DA_CLEAR_ALL 0x480
#define RTC_MACRO_DA_SOC_READY 0x48C
#define RTC_MACRO_RO_T         0x4A8
#define RTC_MACRO_RG_SET_T     0x498

/* CVITEK RTC CTRL registers */
#define CVI_RTC_FC_COARSE_EN  0x40
#define CVI_RTC_FC_COARSE_CAL 0x44
#define CVI_RTC_FC_FINE_EN    0x48
#define CVI_RTC_FC_FINE_CAL   0x50

#define RTC_SEC_MAX_VAL 0xFFFFFFFF

#define RTC_OFFSET_SN      0x5201800
#define RTC_ALARM_IRQ_NUM  0x11
#define REG_SECTICK_CONFIG 0x0
#define REG_PERIOD_ENABLE  0x38
#define REG_PERIOD_WIDTH   0x40
#define REG_CLK25M_STABLE  0x44

#ifndef RTC_USING_HW_CALIB
#define RTC_USING_SW_CALIB
#endif

#define do_div(n, base)                   \
    ({                                    \
        uint32_t __base = (base);         \
        uint32_t __rem;                   \
        __rem = ((uint64_t)(n)) % __base; \
        (n)   = ((uint64_t)(n)) / __base; \
        __rem;                            \
    })

void hal_cvi_rtc_clk_set(int enable);
void hal_cvi_rtc_enable_sec_counter(uintptr_t rtc_base);
void hal_cvi_rtc_set_time(uintptr_t rtc_base, unsigned long sec);
int hal_cvi_rtc_get_time_sec(uintptr_t rtc_base, unsigned long *ret_sec);
void hal_cvi_rtc_alarm_enable(uintptr_t rtc_base, rt_bool_t enable);
void hal_cvi_rtc_set_alarm_time(uintptr_t rtc_base, unsigned long sec);
unsigned long hal_cvi_rtc_get_alarm_time(uintptr_t rtc_base);
void hal_cvi_rtc_32k_coarse_value_calib(uintptr_t rtc_base, uintptr_t rtc_ctrl_base);
void hal_cvi_rtc_32k_fine_value_calib(uintptr_t rtc_base, uintptr_t rtc_ctrl_base);

#endif /* __CVI_RTC_H__ */
