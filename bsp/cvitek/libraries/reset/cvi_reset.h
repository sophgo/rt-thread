/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-03-21   qiujingbao     the first version
 */
#ifndef __CVI_RESET_H__
#define __CVI_RESET_H__

#include <rtthread.h>
#include "mmio.h"

#define CVI_RTC_CTRL_BASE       0x05025000U
#define CVI_RTC_REG_BASE        0x05026000U
#define RTC_CTRL0_UNLOCKKEY     0x4
#define RTC_CTRL0               0x8
#define RTC_APB_BUSY_SEL        0x3C
#define RTC_EN_WARM_RST_REQ     0xCC
#define RSM_STATE               0xD4
#define ST_ON                   0x3

int cvi_reset_restart(void);

#endif /* __CVI_RESET_H__ */
