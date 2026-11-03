/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024/02/19     flyingcys    first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "drv_pwm.h"

#define DBG_LEVEL   DBG_LOG
#include <rtdbg.h>
#define LOG_TAG "DRV.PWM"

struct cvi_pwm_dev
{
    struct rt_device_pwm device;
    const char *name;
    rt_ubase_t reg_base;
    /* capture support */
    rt_pwm_capture_cb_t cap_cb;
    void *cap_cb_arg;
};

static const uint64_t count_unit = 100000000;  // 100M count per second
static const uint64_t NSEC_COUNT = 1000000000;  // ns

static rt_err_t cvi_pwm_set_config(rt_ubase_t reg_base, struct rt_pwm_configuration *cfg)
{
    unsigned long long duty_clk, period_clk;
    cvi_pwm_start_dis_ch(reg_base, (cfg->channel & PWM_MAX_CH));
    duty_clk = (cfg->pulse * count_unit) / NSEC_COUNT;
    period_clk = (cfg->period * count_unit) / NSEC_COUNT;
    if(duty_clk ==period_clk)
    {
        duty_clk = period_clk-1;
    }
    if(duty_clk == 0)
    {
        duty_clk = 1;
    }
    LOG_I("duty_clk: %llu, period_clk: %llu", duty_clk, period_clk);
    if (duty_clk > period_clk || period_clk <=0 || period_clk >0xffffffff || duty_clk<=0 || duty_clk>0xffffffff)
    {
        LOG_E("PWM ch%d invalid: duty_clk(%llu) , period_clk(%llu)",
              (cfg->channel & PWM_MAX_CH), duty_clk, period_clk);
        return -RT_EINVAL;
    }
    if (cfg->polarity)
    {
        cvi_pwm_set_polarity_high_ch(reg_base, (cfg->channel & PWM_MAX_CH));
    }
    else
    {
        cvi_pwm_set_polarity_low_ch(reg_base, (cfg->channel & PWM_MAX_CH));
    }
    cvi_pwm_set_high_period_ch(reg_base, (cfg->channel & PWM_MAX_CH), period_clk-duty_clk);
    cvi_pwm_set_period_ch(reg_base, (cfg->channel & PWM_MAX_CH), period_clk);
    cvi_pwm_start_en_ch(reg_base, (cfg->channel & PWM_MAX_CH));
    cvi_pwm_set_update_ch(reg_base, (cfg->channel & PWM_MAX_CH), 1);
    cvi_pwm_output_en_ch(reg_base, cfg->channel & PWM_MAX_CH);
    cvi_pwm_set_update_ch(reg_base, (cfg->channel & PWM_MAX_CH), 0);
    return RT_EOK;
}

static void cvi_pwm_get_config(rt_ubase_t reg_base, struct rt_pwm_configuration *cfg)
{
    unsigned long long duty_clk, period_clk;

    duty_clk = cvi_pwm_get_high_period_ch(reg_base, (cfg->channel & PWM_MAX_CH));
    cfg->pulse = duty_clk * NSEC_COUNT / count_unit;

    period_clk = cvi_pwm_get_period_ch(reg_base, (cfg->channel & PWM_MAX_CH));
    cfg->period = period_clk * NSEC_COUNT / count_unit;
}

static rt_err_t _pwm_control(struct rt_device_pwm *device, int cmd, void *arg)
{
    struct rt_pwm_configuration *cfg = (struct rt_pwm_configuration *)arg;
    struct cvi_pwm_dev *pwm_dev = (struct cvi_pwm_dev *)device->parent.user_data;
    unsigned long long duty_clk, period_clk;
    const uint64_t count_unit = 100000000;  // 100M count per second
    const uint64_t NSEC_COUNT = 1000000000;  // ns

    if (cfg->channel >= PWM_CHANNEL_NUM)
        return -RT_EINVAL;

    switch (cmd)
    {
        case PWM_CMD_ENABLE:
            cvi_pwm_start_en_ch(pwm_dev->reg_base, cfg->channel & PWM_MAX_CH);
        break;

        case PWM_CMD_DISABLE:
            cvi_pwm_start_dis_ch(pwm_dev->reg_base, cfg->channel & PWM_MAX_CH);
        break;

        case PWM_CMD_SET:
        {
            rt_err_t r = cvi_pwm_set_config(pwm_dev->reg_base, cfg);
            if (r != RT_EOK) return r;
        }
        break;

        case PWM_CMD_GET:
            cvi_pwm_get_config(pwm_dev->reg_base, cfg);
        break;

        case PWM_CMD_SET_PERIOD:
        {
            unsigned long long cur_duty_clk;
            period_clk = (cfg->period * count_unit) / NSEC_COUNT;
            cur_duty_clk = cvi_pwm_get_high_period_ch(pwm_dev->reg_base, (cfg->channel & PWM_MAX_CH));
            if (cur_duty_clk > period_clk || period_clk <=0 || period_clk >0xffffffff)
            {
                LOG_E("PWM ch%d invalid: new period_clk(%llu) ,current duty_clk(%llu)",
                      (cfg->channel & PWM_MAX_CH), period_clk, cur_duty_clk);
                return -RT_EINVAL;
            }
            cvi_pwm_set_period_ch(pwm_dev->reg_base, (cfg->channel & PWM_MAX_CH), period_clk);
        }
        break;

        case PWM_CMD_SET_PULSE:
        {
            unsigned long long cur_period_clk;
            duty_clk = (cfg->pulse * count_unit) / NSEC_COUNT;
            cur_period_clk = cvi_pwm_get_period_ch(pwm_dev->reg_base, (cfg->channel & PWM_MAX_CH));
            if(duty_clk ==cur_period_clk)
            {
                duty_clk = cur_period_clk-1;
            }
            if(duty_clk == 0)
            {
                duty_clk = 1;
            }
            if (duty_clk > cur_period_clk || duty_clk <=0 || duty_clk>0xffffffff)
            {
                LOG_E("PWM ch%d invalid: new duty_clk(%llu) ,current period_clk(%llu)",
                      (cfg->channel & PWM_MAX_CH), duty_clk, cur_period_clk);
                return -RT_EINVAL;
            }
            cvi_pwm_set_high_period_ch(pwm_dev->reg_base, (cfg->channel & PWM_MAX_CH), cur_period_clk-duty_clk);
        }
        break;

        case PWM_CMD_SET_POLARITY:
        {
            if (cfg->polarity)
            {
                cvi_pwm_set_polarity_high_ch(pwm_dev->reg_base, (cfg->channel & PWM_MAX_CH));
            }
            else
            {
                cvi_pwm_set_polarity_low_ch(pwm_dev->reg_base, (cfg->channel & PWM_MAX_CH));
            }
        }
        break;

        case PWM_CMD_CAPTURE_CONFIG: {
            rt_pwm_capture_cfg_t *c = (rt_pwm_capture_cfg_t *)arg;
            if (!c || c->channel >= PWM_CHANNEL_NUM) return -RT_EINVAL;
            cvi_pwm_input_en_ch(pwm_dev->reg_base, (c->channel & PWM_MAX_CH));
            cvi_cap_set_freqnum_ch(pwm_dev->reg_base, (c->channel & PWM_MAX_CH), c->count ? c->count : 1);
        }
        break;

        case PWM_CMD_CAPTURE_START: {
            rt_pwm_capture_ch_t *c = (rt_pwm_capture_ch_t *)arg;
            if (!c || c->channel >= PWM_CHANNEL_NUM) return -RT_EINVAL;
            cvi_cap_freq_en_ch(pwm_dev->reg_base, (c->channel & PWM_MAX_CH));
        }
        break;

        case PWM_CMD_CAPTURE_STOP: {
            rt_pwm_capture_ch_t *c = (rt_pwm_capture_ch_t *)arg;
            if (!c || c->channel >= PWM_CHANNEL_NUM) return -RT_EINVAL;
            cvi_cap_freq_dis_ch(pwm_dev->reg_base, (c->channel & PWM_MAX_CH));
        } break;

        case PWM_CMD_CAPTURE_READ: {
            rt_pwm_capture_read_t *r = (rt_pwm_capture_read_t *)arg;
            if (!r || r->channel >= PWM_CHANNEL_NUM) return -RT_EINVAL;
            uint32_t data = cvi_cap_get_freq_data_ch(pwm_dev->reg_base, (r->channel & PWM_MAX_CH));
            uint32_t done = cvi_cap_get_freq_done_num_ch(pwm_dev->reg_base, (r->channel & PWM_MAX_CH));
            r->period_count = (uint32_t)((data + 50) / 100);
            r->done_num = done;
            r->lperiod_count = cvi_cap_get_freq_lperiod_count_ch(pwm_dev->reg_base, (r->channel & PWM_MAX_CH));
            r->hperiod_count = cvi_cap_get_freq_hperiod_count_ch(pwm_dev->reg_base, (r->channel & PWM_MAX_CH));
        } break;

        case PWM_CMD_CAPTURE_ATTACH_CB: {
            rt_pwm_capture_cb_desc_t *d = (rt_pwm_capture_cb_desc_t *)arg;
            pwm_dev->cap_cb = d ? d->cb : RT_NULL;
            pwm_dev->cap_cb_arg = d ? d->arg : RT_NULL;
        } break;

        case PWM_CMD_CAPTURE_DETACH_CB:
            pwm_dev->cap_cb = RT_NULL;
            pwm_dev->cap_cb_arg = RT_NULL;
        break;
        default:
        LOG_D("cmd: %x channel: %d period: %llu pulse: %llu", cmd, cfg->channel,
              (unsigned long long)cfg->period, (unsigned long long)cfg->pulse);
        break;
    }

    return RT_EOK;
}

const static struct rt_pwm_ops cvi_pwm_ops =
{
    .control = &_pwm_control
};

static struct cvi_pwm_dev cvi_pwm[] =
{
#ifdef BSP_USING_PWM0
    {
        .name = "pwm0",
        .reg_base = CVI_PWM0_BASE,
    },
#endif

#ifdef BSP_USING_PWM1
    {
        .name = "pwm1",
        .reg_base = CVI_PWM1_BASE,
    },
#endif

#ifdef BSP_USING_PWM2
    {
        .name = "pwm2",
        .reg_base = CVI_PWM2_BASE,
    },
#endif

#ifdef BSP_USING_PWM3
    {
        .name = "pwm3",
        .reg_base = CVI_PWM3_BASE,
    },
#endif
};


int rt_hw_pwm_init(void)
{
    int result = RT_EOK;
    uint8_t i;

    for (i = 0; i < sizeof(cvi_pwm) / sizeof(cvi_pwm[0]); i++)
    {
        result = rt_device_pwm_register(&cvi_pwm[i].device, cvi_pwm[i].name, &cvi_pwm_ops, &cvi_pwm[i]);
        if (result != RT_EOK)
        {
            LOG_E("device %s register failed", cvi_pwm[i].name);
            return -RT_ERROR;
        }
    }
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_pwm_init);
