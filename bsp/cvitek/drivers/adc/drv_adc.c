/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024/02/22     flyingcys    first version
 * 2026/10/19                  expose adc1~adc6 as standalone devices, disable pad pulls
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "drv_adc.h"
#include "pinctrl.h"

#define DBG_LEVEL   DBG_LOG
#include <rtdbg.h>
#define LOG_TAG "DRV.ADC"

struct cvi_adc_dev
{
    struct rt_adc_device device;
    const char *name;
    rt_ubase_t base;        /* SARADC controller base */
    rt_int8_t hw_channel;   /* channel index inside the controller: 1 ~ 3 */
    rt_ubase_t pad_reg;     /* IOBLK pad register of the input pin */
};

/*
 * One device per ADC pin. adc1~adc3 are the TOP domain inputs (SARADC VIN1~3),
 * adc4~adc6 the RTC domain ones (PWRSAR VIN1~3, i.e. PWR_GPIO2 / PWR_GPIO1 /
 * PWR_VBAT_DET). Each device owns exactly one channel, so users always read
 * channel 0 and never need to know about the controllers behind them.
 *
 * The table is the largest set a chip can expose, not the smallest package:
 * CV180x has no ADC3, and its ADC2 is the XGPIOB[6]/USB_VBUS_DET pin selected
 * by FMUX function 3. Every input has a pad register; the pinmux stays a board
 * job.
 */
static struct cvi_adc_dev adc_dev_config[] =
{
#if defined(__CV181X__)
    { .name = "adc1", .base = SARADC_BASE,  .hw_channel = 1, .pad_reg = IOBLK_PAD_ADDR(ADC1) },
    { .name = "adc2", .base = SARADC_BASE,  .hw_channel = 2, .pad_reg = IOBLK_PAD_ADDR(ADC2) },
    { .name = "adc3", .base = SARADC_BASE,  .hw_channel = 3, .pad_reg = IOBLK_PAD_ADDR(ADC3) },
    { .name = "adc4", .base = RTC_ADC_BASE, .hw_channel = 1, .pad_reg = IOBLK_PAD_ADDR(PWR_GPIO2) },
    { .name = "adc5", .base = RTC_ADC_BASE, .hw_channel = 2, .pad_reg = IOBLK_PAD_ADDR(PWR_GPIO1) },
    { .name = "adc6", .base = RTC_ADC_BASE, .hw_channel = 3, .pad_reg = IOBLK_PAD_ADDR(PWR_VBAT_DET) },
#elif defined(__CV180X__)
    { .name = "adc1", .base = SARADC_BASE,  .hw_channel = 1, .pad_reg = IOBLK_PAD_ADDR(ADC1) },
    { .name = "adc2", .base = SARADC_BASE,  .hw_channel = 2, .pad_reg = IOBLK_PAD_ADDR(USB_VBUS_DET) },
    { .name = "adc4", .base = RTC_ADC_BASE, .hw_channel = 1, .pad_reg = IOBLK_PAD_ADDR(PWR_GPIO2) },
    { .name = "adc5", .base = RTC_ADC_BASE, .hw_channel = 2, .pad_reg = IOBLK_PAD_ADDR(PWR_GPIO1) },
    { .name = "adc6", .base = RTC_ADC_BASE, .hw_channel = 3, .pad_reg = IOBLK_PAD_ADDR(PWR_VBAT_DET) },
#else
#error "ADC driver: unsupported chip, add its device table"
#endif
};

/*
 * adc1~adc3 share the TOP domain controller and adc4~adc6 share the RTC one.
 * Their CTRL register holds the channel select and is updated read-modify-
 * write, so enable/disable/convert must be serialized or two threads would
 * overwrite each other's channel. A single lock for both domains is enough:
 * one conversion is a few microseconds.
 */
static struct rt_mutex _adc_lock;

static void _adc_pad_init(struct cvi_adc_dev *adc_dev)
{
    /* Clear the pad register to disable the pull-up/pull-down; most ADC pads
     * come up with PD enabled (PU=0, PD=1), which would load the analog input.
     * Same as io_config() in the v4.2.x Linux driver, applied to every input
     * so there is no per-pin special case (PWR_VBAT_DET already resets to 0).
     */
    mmio_write_32(adc_dev->pad_reg, 0);
}

static rt_err_t _adc_enabled(struct rt_adc_device *device, rt_int8_t channel, rt_bool_t enabled)
{
    struct cvi_adc_dev *adc_dev = (struct cvi_adc_dev *)device->parent.user_data;

    RT_ASSERT(adc_dev != RT_NULL);

    if (channel != 0)
        return -RT_EINVAL;

    rt_mutex_take(&_adc_lock, RT_WAITING_FOREVER);

    if (enabled)
    {
        _adc_pad_init(adc_dev);

        //set channel
        cvi_adc_channel_enable(adc_dev->base, adc_dev->hw_channel, RT_TRUE);

        //set saradc clock cycle
        cvi_adc_set_cyc(adc_dev->base);

        LOG_D("enable saradc channel %d...", adc_dev->hw_channel);
    }
    else
    {
        cvi_adc_channel_enable(adc_dev->base, adc_dev->hw_channel, RT_FALSE);
        LOG_D("disable saradc channel %d...", adc_dev->hw_channel);
    }

    rt_mutex_release(&_adc_lock);

    return RT_EOK;
}

static rt_err_t _adc_convert(struct rt_adc_device *device, rt_int8_t channel, rt_uint32_t *value)
{
    struct cvi_adc_dev *adc_dev = (struct cvi_adc_dev *)device->parent.user_data;
    rt_uint32_t result;
    rt_uint32_t cnt = 0;
    rt_err_t ret = RT_EOK;

    RT_ASSERT(adc_dev != RT_NULL);

    if (channel != 0)
        return -RT_EINVAL;

    rt_mutex_take(&_adc_lock, RT_WAITING_FOREVER);

    cvi_adc_channel_enable(adc_dev->base, adc_dev->hw_channel, RT_TRUE);
    cvi_adc_start(adc_dev->base);

    while (cvi_adc_get_status(adc_dev->base) & SARADC_STATUS_BUSY)
    {
        rt_thread_delay(10);
        LOG_D("wait saradc ready");
        cnt ++;
        if (cnt > 100)
        {
            ret = -RT_ETIMEOUT;
            goto _exit;
        }
    }

    result = cvi_adc_read_result(adc_dev->base, adc_dev->hw_channel);
    if (result & SARADC_RESULT_VALID)
    {
        *value = result & SARADC_RESULT_MASK;
        // LOG_D("saradc channel %d value: %04x", adc_dev->hw_channel, *value);
    }
    else
    {
        LOG_E("saradc channel %d read failed. result:0x%04x", adc_dev->hw_channel, result);
        ret = -RT_ERROR;
    }

_exit:
    rt_mutex_release(&_adc_lock);

    return ret;
}

static const struct rt_adc_ops _adc_ops =
{
    .enabled = _adc_enabled,
    .convert = _adc_convert,
};


int rt_hw_adc_init(void)
{
    rt_uint8_t i;

    if (rt_mutex_init(&_adc_lock, "adc_lock", RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        LOG_E("adc lock init failed!");
        return -RT_ERROR;
    }

    /* One trim register per controller, so set it once per domain rather
     * than once per device.
     */
    cvi_adc_set_trim(SARADC_BASE);
    cvi_adc_set_trim(RTC_ADC_BASE);

    for (i = 0; i < sizeof(adc_dev_config) / sizeof(adc_dev_config[0]); i++)
    {
        if (rt_hw_adc_register(&adc_dev_config[i].device, adc_dev_config[i].name, &_adc_ops, &adc_dev_config[i]) != RT_EOK)
        {
            LOG_E("%s register failed!", adc_dev_config[i].name);
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_adc_init);
