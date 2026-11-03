/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date         Author          Notes
 * 2024-03-21   qiujingbao     the first version
 */

#include <rtthread.h>
#include <rtdevice.h>


#define DBG_TAG "DRV.RESET"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#include "cvi_reset.h"

void rt_hw_cpu_reset(void)
{
    cvi_reset_restart();

    while (1);
}

MSH_CMD_EXPORT_ALIAS(rt_hw_cpu_reset, reboot, reset machine);
