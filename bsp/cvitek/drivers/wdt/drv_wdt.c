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
#include <rtdevice.h>
#include "drv_wdt.h"

#define DBG_LEVEL   DBG_LOG
#include <rtdbg.h>
#define LOG_TAG "DRV.WDT"

#define WDT_FREQ_DEFAULT 25000000UL
#define CVI_WDT_MAX_TOP     15
#define CVI_WDT_TOC_MAX     0xffffU

struct _cvi_wdt_dev
{
    struct rt_watchdog_device device;
    const char *name;
    rt_uint32_t base;
    rt_uint32_t timeout;
    rt_uint32_t timeout_ms;
};

static struct _cvi_wdt_dev _wdt_dev[] =
{
#ifdef BSP_USING_WDT0
    {
        .name = "wdt0",
        .base = CVI_WDT0_BASE
    },
#endif /* BSP_USING_WDT0 */
#ifdef BSP_USING_WDT1
    {
        .name = "wdt1",
        .base = CVI_WDT1_BASE
    },
#endif /* BSP_USING_WDT1 */
#ifdef BSP_USING_WDT2
    {
        .name = "wdt2",
        .base = CVI_WDT2_BASE
    },
#endif /* BSP_USING_WDT2 */
};
struct rt_watchdog_device wdt_device;

static rt_err_t _wdt_init(rt_watchdog_t *wdt_device)
{
    cvi_wdt_top_setting();

    return RT_EOK;
}

/**
 * @brief Set a watchdog timeout using the TORR prescaler and TOC counter.
 *
 * @param reg_base base address of the watchdog controller
 * @param timeout_ms timeout period in milliseconds
 *
 * @return RT_EOK on success, otherwise an error.
 */
static rt_err_t cvi_wdt_set_timeout_ms(unsigned long reg_base, uint64_t timeout_ms)
{
    uint64_t timeout_cycles;
    uint64_t scale;
    uint64_t toc;
    rt_uint32_t top;

    if (timeout_ms == 0)
        return -RT_EINVAL;

    /* Reject before multiplying by the clock rate to keep the math bounded. */
    if (timeout_ms > CVI_WDT_MAX_TIMEOUT_MS)
        return -RT_EINVAL;

    timeout_cycles = timeout_ms * WDT_FREQ_DEFAULT;
    for (top = 0; top <= CVI_WDT_MAX_TOP; ++top)
    {
        scale = 1000ULL << (top + 1);
        toc = (timeout_cycles + scale - 1) / scale;
        if (toc <= CVI_WDT_TOC_MAX)
            break;
    }

    if (top > CVI_WDT_MAX_TOP || toc == 0)
        return -RT_EINVAL;

    cvi_wdt_set_timeout(reg_base, top);
    cvi_wdt_set_timeout_counter(reg_base, (uint32_t)toc);
    cvi_wdt_start_en(reg_base, 1);

    return RT_EOK;
}

static rt_uint32_t cvi_wdt_get_timeleft_ms(unsigned long reg_base)
{
    rt_uint64_t timeleft_cycles = cvi_wdt_get_counter_value(reg_base);
    rt_uint64_t timeleft_ms = (timeleft_cycles * 1000ULL +
                               WDT_FREQ_DEFAULT - 1) / WDT_FREQ_DEFAULT;

    return (rt_uint32_t)timeleft_ms;
}


static rt_err_t _wdt_control(rt_watchdog_t *wdt_device, int cmd, void *arg)
{
    rt_err_t ret = RT_EOK;
    RT_ASSERT(wdt_device != RT_NULL);

    struct _cvi_wdt_dev *wdt = rt_container_of(wdt_device, struct _cvi_wdt_dev, device);
    rt_uint32_t reg_base = wdt->base;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_WDT_KEEPALIVE:
        cvi_wdt_feed_en(reg_base);
        break;
    case RT_DEVICE_CTRL_WDT_SET_TIMEOUT:
        if (arg == RT_NULL)
            return -RT_EINVAL;
        ret = cvi_wdt_set_timeout_ms(reg_base, (uint64_t)*(rt_uint32_t *)arg * 1000);
        if (ret == RT_EOK)
        {
            wdt->timeout = *(rt_uint32_t *)arg;
            wdt->timeout_ms = wdt->timeout * 1000;
        }
        break;
    case RT_DEVICE_CTRL_WDT_GET_TIMEOUT:
        if (arg == RT_NULL)
            return -RT_EINVAL;
        *(rt_uint32_t *)arg = wdt->timeout;
        break;
    case CVI_WDT_CTRL_SET_TIMEOUT_MS:
        if (arg == RT_NULL)
            return -RT_EINVAL;
        ret = cvi_wdt_set_timeout_ms(reg_base, *(rt_uint32_t *)arg);
        if (ret == RT_EOK)
        {
            wdt->timeout_ms = *(rt_uint32_t *)arg;
            wdt->timeout = (wdt->timeout_ms + 999) / 1000;
        }
        break;
    case CVI_WDT_CTRL_GET_TIMEOUT_MS:
        if (arg == RT_NULL)
            return -RT_EINVAL;
        *(rt_uint32_t *)arg = wdt->timeout_ms;
        break;
    case CVI_WDT_CTRL_GET_TIMELEFT_MS:
        if (arg == RT_NULL)
            return -RT_EINVAL;
        *(rt_uint32_t *)arg = cvi_wdt_get_timeleft_ms(reg_base);
        break;
    case RT_DEVICE_CTRL_WDT_GET_TIMELEFT:
        if (arg == RT_NULL)
            return -RT_EINVAL;
        *(rt_uint32_t *)arg = (cvi_wdt_get_counter_value(reg_base) / WDT_FREQ_DEFAULT);
        break;
    case RT_DEVICE_CTRL_WDT_START:
        cvi_wdt_set_respond_system_reset(reg_base);
        /* Preserve the hardware's legacy default until a timeout was set. */
        cvi_wdt_start_en(reg_base, wdt->timeout_ms != 0);
        break;
    case RT_DEVICE_CTRL_WDT_STOP:
        ret = cvi_wdt_reset(reg_base);
        break;
    default:
        LOG_W("This command is not supported.");
        return -RT_EINVAL;
    }

    return ret;
}

static const struct rt_watchdog_ops _wdt_ops =
{
    .init = _wdt_init,
    .control = _wdt_control
};

int rt_hw_wdt_init(void)
{
    rt_uint8_t i;
    for (i = 0; i < sizeof(_wdt_dev) / sizeof(_wdt_dev[0]); i ++)
    {
        _wdt_dev[i].device.ops = &_wdt_ops;
        if (rt_hw_watchdog_register(&_wdt_dev[i].device, _wdt_dev[i].name, RT_DEVICE_FLAG_RDWR, RT_NULL) != RT_EOK)
        {
            LOG_E("%s register failed!", _wdt_dev[i].name);
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_wdt_init);
