/*
 * Copyright (c) 2018-2025, Sophgo Technologies Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author          Notes
 * 2025-07-09     shuhan.zhang    first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>
#include "drv_wdt.h"
#include "mmio.h"

#define DEFAULT_WDT_DEVICE "wdt0"
static rt_device_t wdt_dev = RT_NULL;
static rt_timer_t feed_timer = RT_NULL;
static rt_bool_t wdt_active = RT_FALSE;
static rt_bool_t wdt_need_feed = RT_TRUE;
static rt_bool_t wdt_stop_message_printed = RT_FALSE;
static rt_uint32_t wdt_active_timeout_ms;
static rt_uint32_t wdt_active_feed_period_ms;
static rt_uint32_t wdt_config_timeout_ms = 60000;
static rt_uint32_t wdt_config_feed_period_ms = 30000;
static rt_device_t wdt_config_dev = RT_NULL;

/* feeding callback */
static void wdt_feed_callback(void *param)
{
    if (wdt_need_feed == RT_FALSE) {
        if (!wdt_stop_message_printed) {
            rt_kprintf("Watchdog feeding stopped.\n");
            wdt_stop_message_printed = RT_TRUE;
        }
        return;
    }
    else {
        rt_device_control(wdt_dev, RT_DEVICE_CTRL_WDT_KEEPALIVE, RT_NULL);
        if (wdt_active_feed_period_ms >= 1000)
            rt_kprintf("Dog fed!\n");
    }
}

/* Stop feeding and request a hardware watchdog shutdown. */
static rt_err_t stop_wdt_feeding(void)
{
    rt_err_t ret = RT_EOK;

    if (feed_timer) {
        rt_timer_stop(feed_timer);
    }
    
    if (wdt_dev) {
        ret = rt_device_control(wdt_dev, RT_DEVICE_CTRL_WDT_STOP, RT_NULL);
        if (ret != RT_EOK) {
            /* Keep the watchdog serviced when hardware shutdown failed. */
            wdt_need_feed = RT_TRUE;
            wdt_active = RT_TRUE;
            if (feed_timer)
                rt_timer_start(feed_timer);
            return ret;
        }

        if (feed_timer) {
            rt_timer_delete(feed_timer);
            feed_timer = RT_NULL;
        }
        rt_device_close(wdt_dev);
        wdt_dev = RT_NULL;
    }
    
    wdt_active = RT_FALSE;
    wdt_active_timeout_ms = 0;
    wdt_active_feed_period_ms = 0;

    return RT_EOK;
}

static void cleanup_wdt_start_failure(void)
{
    if (stop_wdt_feeding() != RT_EOK)
        rt_kprintf("[WARN] WDT shutdown failed during cleanup; feeding was resumed.\n");
}

enum wdt_option_id
{
    WDT_OPT_PERIOD,
    WDT_OPT_TIMEOUT,
    WDT_OPT_STOP_FEEDING,
    WDT_OPT_DISABLE,
    WDT_OPT_ENABLE,
    WDT_OPT_GET_TIMEOUT,
    WDT_OPT_GET_TIMELEFT,
    WDT_OPT_HELP,
};

struct wdt_option
{
    const char *long_name;
    char short_name;
    rt_bool_t takes_value;
    enum wdt_option_id id;
};

static const struct wdt_option wdt_options[] =
{
    {"period",        't', RT_TRUE,  WDT_OPT_PERIOD},
    {"timeout",       'T', RT_TRUE,  WDT_OPT_TIMEOUT},
    {"stop-feeding",  's', RT_FALSE, WDT_OPT_STOP_FEEDING},
    {"expire",         0,  RT_FALSE, WDT_OPT_STOP_FEEDING},
    {"disable",       'd', RT_FALSE, WDT_OPT_DISABLE},
    {"enable",        'e', RT_FALSE, WDT_OPT_ENABLE},
    {"gettimeout",     0,  RT_FALSE, WDT_OPT_GET_TIMEOUT},
    {"gettimeleft",   'L', RT_FALSE, WDT_OPT_GET_TIMELEFT},
    {"help",          'h', RT_FALSE, WDT_OPT_HELP},
};

static void wdt_print_usage(void)
{
    rt_kprintf("Usage: watchdog [-t period] [-T timeout] [device]\n");
    rt_kprintf("       watchdog -d | -e | -s | --gettimeout | -L [device]\n");
    rt_kprintf("  -t, --period N[ms]      Feed interval (default: 30s)\n");
    rt_kprintf("                          N is seconds; append ms for milliseconds\n");
    rt_kprintf("  -T, --timeout N[ms]     Timeout (default: 60s)\n");
    rt_kprintf("                          N is seconds; append ms for milliseconds\n");
    rt_kprintf("  device                  Watchdog device name (default: wdt0)\n");
    rt_kprintf("  -d, --disable           Disable the hardware watchdog\n");
    rt_kprintf("  -e, --enable            Enable with the last period/timeout/device\n");
    rt_kprintf("  -s, --stop-feeding, --expire\n");
    rt_kprintf("                          Stop feeding and wait for watchdog reset\n");
    rt_kprintf("      --gettimeout        Print the configured timeout in milliseconds\n");
    rt_kprintf("  -L, --gettimeleft       Print the remaining timeout in milliseconds\n");
    rt_kprintf("  -h, --help              Show this help\n");
}

static rt_bool_t wdt_parse_duration_ms(const char *text, rt_uint32_t *value)
{
    char *end;
    unsigned long parsed;
    rt_uint64_t duration_ms;

    if (text == RT_NULL || text[0] == '\0' || text[0] == '-')
        return RT_FALSE;

    parsed = strtoul(text, &end, 10);
    if (end == text || parsed == 0 || parsed > 0xffffffffUL)
        return RT_FALSE;

    if (*end == '\0')
        duration_ms = (rt_uint64_t)parsed * 1000;
    else if (strcmp(end, "ms") == 0)
        duration_ms = parsed;
    else
        return RT_FALSE;

    if (duration_ms == 0 || duration_ms > CVI_WDT_MAX_TIMEOUT_MS)
        return RT_FALSE;

    *value = (rt_uint32_t)duration_ms;
    return RT_TRUE;
}

static rt_device_t wdt_select_device(rt_device_t requested_dev)
{
    if (requested_dev != RT_NULL)
        return requested_dev;
    if (wdt_active && wdt_dev != RT_NULL)
        return wdt_dev;
    if (wdt_config_dev != RT_NULL)
        return wdt_config_dev;
    return rt_device_find(DEFAULT_WDT_DEVICE);
}

static rt_err_t wdt_get_device(rt_device_t requested_dev, rt_device_t *dev,
                               rt_bool_t *opened_here)
{
    *opened_here = RT_FALSE;
    *dev = wdt_select_device(requested_dev);
    if (*dev == RT_NULL)
    {
        rt_kprintf("[FAIL] Find watchdog device failed (default: %s)!\n", DEFAULT_WDT_DEVICE);
        return -RT_ERROR;
    }
    if (*dev == wdt_dev && wdt_active)
        return RT_EOK;

    if (rt_device_open(*dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("[FAIL] Open watchdog device failed!\n");
        *dev = RT_NULL;
        return -RT_ERROR;
    }

    *opened_here = RT_TRUE;
    return RT_EOK;
}

static void wdt_close_temporary_device(rt_device_t dev, rt_bool_t opened_here)
{
    if (opened_here && dev != RT_NULL)
        rt_device_close(dev);
}

static rt_err_t wdt_disable(rt_device_t requested_dev)
{
    rt_device_t dev;
    rt_bool_t opened_here;
    rt_err_t ret;

    if (wdt_active && wdt_dev != RT_NULL)
    {
        if (requested_dev != RT_NULL && requested_dev != wdt_dev)
        {
            rt_kprintf("[FAIL] Watchdog is active on another device.\n");
            return -RT_ERROR;
        }
        return stop_wdt_feeding();
    }

    ret = wdt_get_device(requested_dev, &dev, &opened_here);
    if (ret != RT_EOK)
        return ret;

    ret = rt_device_control(dev, RT_DEVICE_CTRL_WDT_STOP, RT_NULL);
    wdt_close_temporary_device(dev, opened_here);
    return ret;
}

static rt_err_t wdt_show_timeout(rt_device_t requested_dev)
{
    rt_device_t dev;
    rt_bool_t opened_here;
    rt_uint32_t timeout_ms = 0;
    rt_err_t ret;

    ret = wdt_get_device(requested_dev, &dev, &opened_here);
    if (ret != RT_EOK)
        return ret;

    ret = rt_device_control(dev, CVI_WDT_CTRL_GET_TIMEOUT_MS, &timeout_ms);
    if (ret == RT_EOK)
        rt_kprintf("Watchdog timeout: %u ms\n", timeout_ms);
    else
        rt_kprintf("[FAIL] Get watchdog timeout failed!\n");

    wdt_close_temporary_device(dev, opened_here);
    return ret;
}

static rt_err_t wdt_show_timeleft(rt_device_t requested_dev)
{
    rt_device_t dev;
    rt_bool_t opened_here;
    rt_uint32_t remain_ms = 0;
    rt_err_t ret;

    ret = wdt_get_device(requested_dev, &dev, &opened_here);
    if (ret != RT_EOK)
        return ret;

    ret = rt_device_control(dev, CVI_WDT_CTRL_GET_TIMELEFT_MS, &remain_ms);
    if (ret == RT_EOK)
        rt_kprintf("Watchdog time left: %u ms\n", remain_ms);
    else
        rt_kprintf("[FAIL] Get watchdog time left failed!\n");

    wdt_close_temporary_device(dev, opened_here);
    return ret;
}

static rt_err_t wdt_start(rt_uint32_t timeout_ms, rt_uint32_t feed_period_ms,
                          rt_device_t requested_dev)
{
    rt_tick_t feed_period_ticks;
    rt_uint32_t readback_ms = 0;
    rt_device_t selected_dev = wdt_select_device(requested_dev);

    if (timeout_ms == 0 || timeout_ms > CVI_WDT_MAX_TIMEOUT_MS ||
        feed_period_ms == 0 || feed_period_ms > CVI_WDT_MAX_TIMEOUT_MS)
    {
        rt_kprintf("[FAIL] Timeout or feed interval is out of range!\n");
        return -RT_EINVAL;
    }

    feed_period_ticks = rt_tick_from_millisecond(feed_period_ms);
    if (feed_period_ticks == 0)
        feed_period_ticks = 1;
    if ((rt_uint64_t)feed_period_ticks * 1000 >=
        (rt_uint64_t)timeout_ms * RT_TICK_PER_SECOND)
    {
        rt_kprintf("[FAIL] Feed interval must be shorter than timeout at this tick rate.\n");
        return -RT_EINVAL;
    }

    if (selected_dev == RT_NULL)
    {
        rt_kprintf("[FAIL] Find watchdog device failed (default: %s)!\n", DEFAULT_WDT_DEVICE);
        return -RT_ERROR;
    }

    if (wdt_active)
    {
        rt_kprintf("Stopping existing WDT...\n");
        if (stop_wdt_feeding() != RT_EOK)
        {
            rt_kprintf("[FAIL] Stop existing watchdog failed; keeping it active.\n");
            return -RT_ERROR;
        }
    }

    wdt_dev = selected_dev;

    if (rt_device_open(wdt_dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("[FAIL] Open %s device failed!\n", wdt_dev->parent.name);
        wdt_dev = RT_NULL;
        return -RT_ERROR;
    }

    /* Create the timer before programming the timeout, which starts the WDT. */
    feed_timer = rt_timer_create("wdt_feed", wdt_feed_callback, RT_NULL,
                                 feed_period_ticks, RT_TIMER_FLAG_PERIODIC);
    if (feed_timer == RT_NULL)
    {
        rt_kprintf("[FAIL] Create feed timer failed!\n");
        rt_device_close(wdt_dev);
        wdt_dev = RT_NULL;
        return -RT_ERROR;
    }

    wdt_need_feed = RT_TRUE;
    wdt_active_timeout_ms = timeout_ms;

    if (rt_device_control(wdt_dev, CVI_WDT_CTRL_SET_TIMEOUT_MS,
                          &timeout_ms) != RT_EOK ||
        rt_device_control(wdt_dev, CVI_WDT_CTRL_GET_TIMEOUT_MS,
                          &readback_ms) != RT_EOK || readback_ms != timeout_ms)
    {
        rt_kprintf("[FAIL] Set or read back watchdog timeout failed!\n");
        cleanup_wdt_start_failure();
        return -RT_ERROR;
    }
    rt_kprintf("Watchdog timeout set to %u ms (readback %u ms).\n",
               timeout_ms, readback_ms);

    if (rt_device_control(wdt_dev, RT_DEVICE_CTRL_WDT_START, RT_NULL) != RT_EOK)
    {
        rt_kprintf("[FAIL] Start watchdog failed!\n");
        cleanup_wdt_start_failure();
        return -RT_ERROR;
    }

    wdt_need_feed = RT_TRUE;
    wdt_stop_message_printed = RT_FALSE;
    wdt_active = RT_TRUE;
    wdt_active_feed_period_ms = feed_period_ms;
    rt_timer_start(feed_timer);

    wdt_config_timeout_ms = timeout_ms;
    wdt_config_feed_period_ms = feed_period_ms;
    wdt_config_dev = wdt_dev;

    rt_kprintf("Watchdog %s enabled; feed interval: %u ms.\n",
               wdt_dev->parent.name, feed_period_ms);
    return RT_EOK;
}

int watchdog(int argc, char **argv)
{
    rt_uint32_t timeout_ms = wdt_config_timeout_ms;
    rt_uint32_t feed_period_ms = wdt_config_feed_period_ms;
    rt_bool_t timeout_set = RT_FALSE;
    rt_bool_t period_set = RT_FALSE;
    rt_bool_t action_set = RT_FALSE;
    enum wdt_option_id action = WDT_OPT_PERIOD;
    rt_device_t selected_dev = RT_NULL;
    int i;

    for (i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        const char *value = RT_NULL;
        enum wdt_option_id id;
        rt_bool_t found = RT_FALSE;
        rt_bool_t has_inline_value = RT_FALSE;
        rt_bool_t takes_value = RT_FALSE;
        rt_uint32_t parsed_value;
        rt_size_t opt_index;

        if (arg[0] != '-')
        {
            if (selected_dev != RT_NULL)
            {
                rt_kprintf("Specify only one watchdog device.\n");
                return -RT_EINVAL;
            }
            selected_dev = rt_device_find(arg);
            if (selected_dev == RT_NULL)
            {
                rt_kprintf("[FAIL] Watchdog device not found: %s\n", arg);
                return -RT_ERROR;
            }
            continue;
        }

        for (opt_index = 0; opt_index < sizeof(wdt_options) / sizeof(wdt_options[0]); ++opt_index)
        {
            const struct wdt_option *opt = &wdt_options[opt_index];
            rt_size_t long_len = strlen(opt->long_name);

            if (arg[0] == '-' && arg[1] == '-')
            {
                if (strncmp(arg + 2, opt->long_name, long_len) == 0 &&
                    (arg[2 + long_len] == '\0' || arg[2 + long_len] == '='))
                {
                    found = RT_TRUE;
                    id = opt->id;
                    takes_value = opt->takes_value;
                    if (arg[2 + long_len] == '=')
                    {
                        value = arg + 3 + long_len;
                        has_inline_value = RT_TRUE;
                    }
                    break;
                }
            }
            else if (arg[0] == '-' && arg[1] != '\0' && opt->short_name != 0 &&
                     arg[1] == opt->short_name)
            {
                found = RT_TRUE;
                id = opt->id;
                takes_value = opt->takes_value;
                if (arg[2] != '\0')
                {
                    value = arg + 2;
                    has_inline_value = RT_TRUE;
                }
                break;
            }
        }

        if (!found)
        {
            rt_kprintf("Unknown option: %s\n", arg);
            wdt_print_usage();
            return -RT_EINVAL;
        }

        if (takes_value)
        {
            if (!has_inline_value)
            {
                if (i + 1 >= argc || argv[i + 1][0] == '-')
                {
                    rt_kprintf("Option %s needs a value.\n", arg);
                    return -RT_EINVAL;
                }
                value = argv[++i];
            }
            if (!wdt_parse_duration_ms(value, &parsed_value))
            {
                rt_kprintf("Invalid or out-of-range duration for %s: %s\n", arg, value);
                return -RT_EINVAL;
            }

            switch (id)
            {
            case WDT_OPT_PERIOD:
                if (period_set)
                {
                    rt_kprintf("Specify the feed period only once.\n");
                    return -RT_EINVAL;
                }
                feed_period_ms = parsed_value;
                period_set = RT_TRUE;
                break;
            case WDT_OPT_TIMEOUT:
                if (timeout_set)
                {
                    rt_kprintf("Specify the timeout only once.\n");
                    return -RT_EINVAL;
                }
                timeout_ms = parsed_value;
                timeout_set = RT_TRUE;
                break;
            default:
                return -RT_EINVAL;
            }
            continue;
        }

        if (has_inline_value)
        {
            rt_kprintf("Option %s does not take a value.\n", arg);
            return -RT_EINVAL;
        }

        if (id == WDT_OPT_HELP)
        {
            wdt_print_usage();
            return 0;
        }

        if (action_set)
        {
            rt_kprintf("Specify only one action at a time.\n");
            return -RT_EINVAL;
        }
        action = id;
        action_set = RT_TRUE;
    }

    if (action_set && (timeout_set || period_set))
    {
        rt_kprintf("Timeout/period options start the watchdog; do not combine them with an action.\n");
        return -RT_EINVAL;
    }

    if (action_set)
    {
        switch (action)
        {
        case WDT_OPT_STOP_FEEDING:
            if (!wdt_active)
            {
                rt_kprintf("WDT not active!\n");
                return -RT_ERROR;
            }
            if (selected_dev != RT_NULL && selected_dev != wdt_dev)
            {
                rt_kprintf("[FAIL] Watchdog is active on another device.\n");
                return -RT_ERROR;
            }
            wdt_need_feed = RT_FALSE;
            wdt_stop_message_printed = RT_FALSE;
            rt_kprintf("Stop feeding; watchdog will reset about %u ms after the last keepalive.\n",
                       wdt_active_timeout_ms);
            return RT_EOK;

        case WDT_OPT_DISABLE:
            if (wdt_disable(selected_dev) != RT_EOK)
            {
                rt_kprintf("[FAIL] Hardware watchdog disable failed.\n");
                return -RT_ERROR;
            }
            rt_kprintf("Watchdog disabled.\n");
            return RT_EOK;

        case WDT_OPT_ENABLE:
            return wdt_start(wdt_config_timeout_ms, wdt_config_feed_period_ms,
                             selected_dev);

        case WDT_OPT_GET_TIMEOUT:
            return wdt_show_timeout(selected_dev);

        case WDT_OPT_GET_TIMELEFT:
            return wdt_show_timeleft(selected_dev);

        default:
            return -RT_EINVAL;
        }
    }

    if (timeout_set && !period_set)
    {
        /* Keep the default ping safely inside an explicitly selected timeout. */
        feed_period_ms = timeout_ms / 2;
        if (feed_period_ms == 0)
            feed_period_ms = 1;
    }

    return wdt_start(timeout_ms, feed_period_ms, selected_dev);
}
MSH_CMD_EXPORT(watchdog, Watchdog control: -t feed period -T timeout; append ms for milliseconds; optional device name);

