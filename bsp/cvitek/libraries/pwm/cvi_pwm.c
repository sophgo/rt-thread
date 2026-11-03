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
#include "cvi_pwm.h"

struct cvi_pwm_regs_t cv182x_pwm_reg = {
    .HLPERIOD0 = 0x0,
    .PERIOD0 = 0x4,
    .HLPERIOD1 = 0x8,
    .PERIOD1 = 0xc,
    .HLPERIOD2 = 0x10,
    .PERIOD2 = 0x14,
    .HLPERIOD3 = 0x18,
    .PERIOD3 = 0x1c,
    .CAP_FREQNUM = 0x20,
    .CAP_FREQDATA = 0x24,
    .POLARITY = 0x40,
    .PWMSTART = 0x44,
    .PWMDONE = 0x48,
    .PWMUPDATE = 0x4c,
    .PCOUNT0 = 0x50,
    .PCOUNT1 = 0x54,
    .PCOUNT2 = 0x58,
    .PCOUNT3 = 0x5c,
    .PULSECOUNT0 = 0x60,
    .PULSECOUNT1 = 0x64,
    .PULSECOUNT2 = 0x68,
    .PULSECOUNT3 = 0x6c,
    .SHIFTCOUNT0 = 0x80,
    .SHIFTCOUNT1 = 0x84,
    .SHIFTCOUNT2 = 0x88,
    .SHIFTCOUNT3 = 0x8c,
    .SHIFTSTART = 0x90,
    .CAP_FREQEN = 0x9c,
    .CAP_FREQHIGHCOUNT = 0xa0,
    .CAP_FREQLOWCOUNT = 0xa4,
    .CAP_FREQDONE_NUM = 0xC0,
    .PWM_OE = 0xd0,
};

struct cvi_pwm_regs_t *cvi_pwm_reg = &cv182x_pwm_reg;

void cvi_pwm_set_high_period_ch(unsigned long reg_base, uint32_t ch, unsigned long long value)
{
    PWM_HLPERIODX(reg_base, ch) = value;
}

unsigned long long cvi_pwm_get_high_period_ch(unsigned long reg_base, uint32_t ch)
{
    return PWM_HLPERIODX(reg_base, ch);
}

void cvi_pwm_set_period_ch(unsigned long reg_base, uint32_t ch, unsigned long long value)
{
    PWM_PERIODX(reg_base, ch) = value;
}
void cvi_pwm_set_update_ch(unsigned long reg_base, uint32_t ch, uint32_t value)
{
    if(value)
    {
        PWM_PWMUPDATE(reg_base) |= (1U<<ch);
    }
    else
    {
        PWM_PWMUPDATE(reg_base) &= ~(1U<<ch);
    }
}
unsigned long long cvi_pwm_get_period_ch(unsigned long reg_base, uint32_t ch)
{
    return PWM_PERIODX(reg_base, ch);
}

void cvi_pwm_set_polarity_high_ch(unsigned long reg_base, uint32_t ch)
{
    PWM_POLARITY(reg_base) |= CVI_PWM_POLARITY_CH_HIGH(ch);
}

void cvi_pwm_set_polarity_low_ch(unsigned long reg_base, uint32_t ch)
{
    PWM_POLARITY(reg_base) &= ~CVI_PWM_POLARITY_CH_HIGH(ch);
}

uint32_t cvi_pwm_get_polarity(unsigned long reg_base, uint32_t ch)
{
    return (PWM_POLARITY(reg_base) & CVI_PWM_POLARITY_CH_Msk(ch));
}

void cvi_pwm_start_en_ch(unsigned long reg_base, uint32_t ch)
{
    PWM_PWMSTART(reg_base) |= CVI_PWM_START_CH_EN(ch);
}

void cvi_pwm_start_dis_ch(unsigned long reg_base, uint32_t ch)
{
    PWM_PWMSTART(reg_base) &= ~CVI_PWM_START_CH_EN(ch);
}

void cvi_pwm_output_en_ch(unsigned long reg_base, uint32_t ch)
{
    PWM_PWM_OE(reg_base) |= CVI_PWM_OUTPUT_CH_EN(ch);
}

void cvi_pwm_input_en_ch(unsigned long reg_base, uint32_t ch)
{
    PWM_PWM_OE(reg_base) &= ~CVI_PWM_OUTPUT_CH_EN(ch);
}

void cvi_cap_set_freqnum_ch(unsigned long reg_base, uint32_t ch, uint32_t value)
{
    CAP_FREQNUM(reg_base, ch) = value;
}

void cvi_cap_freq_en_ch(unsigned long reg_base, uint32_t ch)
{
    CAP_FREQEN(reg_base) |= CVI_CAP_FREQEN(ch);
}

void cvi_cap_freq_dis_ch(unsigned long reg_base, uint32_t ch)
{
    CAP_FREQEN(reg_base) &= ~CVI_CAP_FREQEN(ch);
}

uint32_t cvi_cap_get_freq_done_num_ch(unsigned long reg_base, uint32_t ch)
{
    return CAP_FREQDONE_NUM(reg_base, ch);
}

uint32_t cvi_cap_get_freq_data_ch(unsigned long reg_base, uint32_t ch)
{
    return CAP_FREQDATA(reg_base, ch);
}
uint32_t cvi_cap_get_freq_lperiod_count_ch(unsigned long reg_base, uint32_t ch)
{
    return CAP_FREQLOWCOUNT(reg_base, ch);
}

uint32_t cvi_cap_get_freq_hperiod_count_ch(unsigned long reg_base, uint32_t ch)
{
    return CAP_FREQHIGHCOUNT(reg_base, ch);
}
