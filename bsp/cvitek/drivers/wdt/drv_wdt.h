/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2024/03/02     ShichengChu    first version
 */
#ifndef __DRV_WDT_H__
#define __DRV_WDT_H__

#include <rtdevice.h>
#include "cvi_wdt.h"

/* CVITEK extension: SET takes a uint32_t timeout in ms; GET returns that value. */
#define CVI_WDT_CTRL_SET_TIMEOUT_MS    (RT_DEVICE_CTRL_BASE(WDT) + 7)
#define CVI_WDT_CTRL_GET_TIMEOUT_MS    (RT_DEVICE_CTRL_BASE(WDT) + 8)
#define CVI_WDT_CTRL_GET_TIMELEFT_MS  (RT_DEVICE_CTRL_BASE(WDT) + 9)
/* Maximum integer millisecond request representable by 25 MHz, TOP=15, TOC=0xffff. */
#define CVI_WDT_MAX_TIMEOUT_MS         171796U

int rt_hw_wdt_init(void);

#endif /* __DRV_WDT_H__ */
