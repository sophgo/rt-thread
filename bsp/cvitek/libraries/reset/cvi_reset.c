/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-03-21   qiujingbao     the first version
 */
#include <rtthread.h>
#include "cvi_reset.h"
#include "mmio.h"

int cvi_reset_restart(void)
{
    /* Enable power suspend wakeup source mask */
    mmio_write_32(CVI_RTC_REG_BASE + RTC_APB_BUSY_SEL,0x1);

    /* unlock register */
    mmio_write_32(CVI_RTC_CTRL_BASE + RTC_CTRL0_UNLOCKKEY, 0xAB18);

    mmio_write_32(CVI_RTC_REG_BASE + RTC_EN_WARM_RST_REQ, 0x1);

    while (mmio_read_32(CVI_RTC_REG_BASE + RTC_EN_WARM_RST_REQ) != 0x01);

    while (mmio_read_32(CVI_RTC_REG_BASE + RSM_STATE) != ST_ON);

    mmio_write_32( CVI_RTC_CTRL_BASE + RTC_CTRL0,0xFFFF0800 | (0x1 << 4));

    return 0;
}
