/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 */

#include <rtthread.h>
#include <rthw.h>
#include <rtdef.h>
#include <board.h>
#include "core/core_rv64.h"

#ifdef RT_USING_CACHE

/* used by csi_mmu_set_mode(If there are multiple mmu modes) */
int g_mmu_mode = 0;

void rt_hw_cpu_icache_enable(void)
{
    csi_icache_enable();
}

void rt_hw_cpu_icache_disable(void)
{
    csi_icache_disable();
}

rt_base_t rt_hw_cpu_icache_status(void)
{
    return csi_icache_is_enable();
}

void rt_hw_cpu_icache_ops(int ops, void *addr, int size)
{
    if (ops & RT_HW_CACHE_INVALIDATE)
    {
        csi_icache_invalid();
    }
    else
    {
        RT_ASSERT(0);
    }
    (void)addr;
    (void)size;
}

void rt_hw_cpu_dcache_enable(void)
{
    csi_dcache_enable();
}

void rt_hw_cpu_dcache_disable(void)
{
    csi_dcache_disable();
}

rt_base_t rt_hw_cpu_dcache_status(void)
{
    return csi_dcache_is_enable();
}

void rt_hw_cpu_dcache_ops(int ops, void *addr, int size)
{
    rt_uint32_t clean_invalid = RT_HW_CACHE_FLUSH | RT_HW_CACHE_INVALIDATE;

    if ((ops & clean_invalid) == clean_invalid)
    {
        csi_dcache_clean_invalid_range((rt_uint64_t *)addr, size);
    }
    else if (ops & RT_HW_CACHE_FLUSH)
    {
        csi_dcache_clean_range((rt_uint64_t *)addr, size);
    }
    else if (ops & RT_HW_CACHE_INVALIDATE)
    {
        csi_dcache_invalid_range((rt_uint64_t *)addr, size);
    }
    else
    {
        RT_ASSERT(0);
    }
}

void rt_hw_cpu_dcache_clean(void *addr, int size)
{
    csi_dcache_clean_range((rt_uint64_t *)addr, size);
}

void rt_hw_cpu_dcache_invalidate(void *addr, int size)
{
    csi_dcache_invalid_range((rt_uint64_t *)addr, size);
}

void rt_hw_cpu_dcache_clean_invalidate(void *addr, int size)
{
    csi_dcache_clean_invalid_range((rt_uint64_t *)addr, size);
}

#endif /* RT_USING_CACHE */

