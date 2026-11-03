/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024/05/24     unicornx     first version
 */

#include <string.h>

#include <rtthread.h>
#include "mmio.h"
#include "pinctrl.h"
#include "drv_pinmux.h"

#define DBG_TAG              "drv.pinmux"
#define DBG_LVL               DBG_INFO
#include <rtdbg.h>

static int8_t pinmux_get_index(uint8_t pin_index, fs_type func_type)
{
    const struct fselect *p;
    for (int i = 0; i < 8; i++) {
        p = &(pin_selects_array[pin_index][i]);
        LOG_D("[%d], type = %d, select = %d\n", i, p->type, p->select);
        if (p->type == func_type)
            return (int8_t)p->select; // it's safe bcos select should be [0, 7]
    }
    return -1;
}

static int pinmux_check_whitelist(const char *pin_name, const char *whitelist[])
{
    const char **name = &whitelist[0];
    while (*name) {
        if (0 == strcmp(pin_name, *name))
            return 0;
        name++;
    }
    return -1;
}

int pinmux_config(const char *pin_name, fs_type func_type, const char *whitelist[])
{
    const struct fmux *p_fmux;
    int index;
    int8_t select;

    if (whitelist) {
        if (0 != pinmux_check_whitelist(pin_name, whitelist)) {
            LOG_W("Pin Name \"%s\" is NOT Allowed by Whitelist!", pin_name);
            return -RT_ERROR;
        }
    }

    for (index = 0; index < pinmux_array_size; index++) {
        p_fmux = &(pinmux_array[index]);
        LOG_D("index[%d]: name: %s, addr: %d, offset: %d, mask: %d\n",
                index, p_fmux->name, p_fmux->addr, p_fmux->offset, p_fmux->mask);
        if (0 == strcmp(pin_name, p_fmux->name)) {
            break;
        }
    }
    if (index == pinmux_array_size) {
        LOG_W("Pin Name \"%s\" is not found!", pin_name);
        return -RT_ERROR;;
    }
    if (p_fmux->selected) {
        LOG_W("Pin Name \"%s\" has been selected, duplicated?", pin_name);
        return -RT_ERROR;
    }

    select = pinmux_get_index(index, func_type);
    if (-1 == select) {
        LOG_W("Can not found Function selection for Pin \"%s\"", pin_name);
        return -RT_ERROR;
    }

    LOG_D("Pin Name = \"%s\", Func Type = %d, selected Func [%d]\n", pin_name, func_type, select);
    pinmux_array[index].selected = 1;
    mmio_clrsetbits_32(PINMUX_BASE + p_fmux->addr, p_fmux->mask << p_fmux->offset, select);
    return RT_EOK;
}
