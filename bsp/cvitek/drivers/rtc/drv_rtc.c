/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date         Author          Notes
 * 2024-03-04   ShichengChu     the first version
 */

#include <rtdevice.h>
#include <rtthread.h>

#define DBG_TAG "DRV.RTC"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#include "drv_rtc.h"
#include "cvi_rtc.h"
#include "mmio.h"

static const unsigned char cvi_rtc_days_in_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
};

static inline int is_leap_year(unsigned int year)
{
    return (!(year % 4) && (year % 100)) || !(year % 400);
}

static int rtc_month_days(unsigned int month, unsigned int year)
{
    return cvi_rtc_days_in_month[month] + (is_leap_year(year) && month == 1);
}

static inline int64_t div_u64_rem(uint64_t dividend, rt_uint32_t divisor, rt_uint32_t *remainder)
{
    *remainder = dividend % divisor;
    return dividend / divisor;
}

/*
 * rtc_time_to_tm64 - Converts time64_t to rtc_time.
 * Convert seconds since 01-01-1970 00:00:00 to Gregorian date.
 */
static void rtc_time64_to_tm(int64_t time, cvi_rtc_time_t *cvi_tm)
{
    unsigned int month, year, secs;
    int          days;

    /* time must be positive */
    days = div_u64_rem(time, 86400, &secs);

    /* day of the week, 1970-01-01 was a Thursday */
    cvi_tm->tm_wday = (days + 4) % 7;

    year = 1970 + days / 365;
    days -= (year - 1970) * 365 + LEAPS_THRU_END_OF(year - 1) - LEAPS_THRU_END_OF(1970 - 1);
    while (days < 0) {
        year -= 1;
        days += 365 + is_leap_year(year);
    }
    cvi_tm->tm_year = year - 1900;
    cvi_tm->tm_yday = days + 1;

    for (month = 0; month < 11; month++) {
        int newdays;

        newdays = days - rtc_month_days(month, year);
        if (newdays < 0) break;
        days = newdays;
    }
    cvi_tm->tm_mon  = month;
    cvi_tm->tm_mday = days + 1;

    cvi_tm->tm_hour = secs / 3600;
    secs -= cvi_tm->tm_hour * 3600;
    cvi_tm->tm_min = secs / 60;
    cvi_tm->tm_sec = secs - cvi_tm->tm_min * 60;
}

static int64_t mktime64(const unsigned int year0, const unsigned int mon0, const unsigned int day,
                        const unsigned int hour, const unsigned int min, const unsigned int sec)
{
    unsigned int mon = mon0, year = year0;

    /* 1..12 -> 11,12,1..10 */
    if (0 >= (int)(mon -= 2)) {
        mon += 12; /* Puts Feb last since it has leap day */
        year -= 1;
    }

    return ((((int64_t)(year / 4 - year / 100 + year / 400 + 367 * mon / 12 + day) + year * 365 -
              719499) *
                 24 +
             hour /* now have hours - midnight tomorrow handled here */
             ) * 60 +
            min /* now have minutes */
            ) * 60 +
           sec; /* finally seconds */
}

/*
 * rtc_tm_to_time64 - Converts rtc_time to time64_t.
 * Convert Gregorian date to seconds since 01-01-1970 00:00:00.
 */
static int64_t rtc_tm_to_time64(const cvi_rtc_time_t *cvi_tm)
{
    return mktime64(cvi_tm->tm_year + 1900, cvi_tm->tm_mon + 1, cvi_tm->tm_mday, cvi_tm->tm_hour,
                    cvi_tm->tm_min, cvi_tm->tm_sec);
}

static rt_err_t _rtc_get_timeval(struct timeval *tv)
{
    unsigned long sec;

    hal_cvi_rtc_get_time_sec(CVI_RTC_BASE, &sec);
    tv->tv_sec = sec;

    return RT_EOK;
}

static rt_err_t _rtc_set_timeval(struct timeval *tv)
{
    struct tm     tm_new = {0};
    unsigned long set_sec;

    // Convert timeval to tm structure
    gmtime_r(&tv->tv_sec, &tm_new);

    set_sec = rtc_tm_to_time64((cvi_rtc_time_t *)&tm_new);
    hal_cvi_rtc_set_time(CVI_RTC_BASE, set_sec);

    return RT_EOK;
}

static rt_err_t _rtc_init(void)
{
    hal_cvi_rtc_clk_set(1);
    hal_cvi_rtc_enable_sec_counter(CVI_RTC_BASE);

    return RT_EOK;
}

static rt_err_t _rtc_get_secs(time_t *sec)
{
    struct timeval tv;

    _rtc_get_timeval(&tv);
    *(time_t *)sec = tv.tv_sec;
    LOG_D("RTC: get rtc_time %d", *sec);

    return RT_EOK;
}

static rt_err_t _rtc_set_secs(time_t *sec)
{
    struct tm     tm = {0};
    unsigned long set_sec;

    gmtime_r(sec, &tm);

    set_sec = rtc_tm_to_time64((cvi_rtc_time_t *)&tm);
    hal_cvi_rtc_set_time(CVI_RTC_BASE, set_sec);

    return RT_EOK;
}

#ifdef RT_USING_ALARM
static void rt_hw_rtc_isr(int irqno, void *param)
{
    rt_interrupt_enter();

    /* send event to alarm */
    rt_alarm_update(&rtc_device.rtc_dev.parent, 1);
    /* clear alarm */
    hal_cvi_rtc_alarm_enable(CVI_RTC_BASE, 0);

    rt_interrupt_leave();
}
#endif

static rt_err_t _rtc_get_alarm(struct rt_rtc_wkalarm *alarm)
{
    if (alarm == RT_NULL) return -RT_ERROR;

    unsigned long int sec;
    cvi_rtc_time_t    t = {0};

    sec = hal_cvi_rtc_get_alarm_time(CVI_RTC_BASE);
    rtc_time64_to_tm(sec, &t);

    alarm->tm_sec  = t.tm_sec;
    alarm->tm_min  = t.tm_min;
    alarm->tm_hour = t.tm_hour;
    alarm->tm_mday = t.tm_mday;
    alarm->tm_mon  = t.tm_mon;
    alarm->tm_year = t.tm_year;
    LOG_D("GET_ALARM %d:%d:%d", alarm->tm_hour, alarm->tm_min, alarm->tm_sec);

    return RT_EOK;
}

static rt_err_t _rtc_set_alarm(struct rt_rtc_wkalarm *alarm)
{
    if (alarm == RT_NULL) return -RT_ERROR;

    cvi_rtc_time_t    t = {0};
    unsigned long int set_sec;

    if (alarm->enable) {
        hal_cvi_rtc_alarm_enable(CVI_RTC_BASE, 0);
        t.tm_sec  = alarm->tm_sec;
        t.tm_min  = alarm->tm_min;
        t.tm_hour = alarm->tm_hour;
        t.tm_mday = alarm->tm_mday;
        t.tm_mon  = alarm->tm_mon;
        t.tm_year = alarm->tm_year;

        set_sec = rtc_tm_to_time64(&t);
        hal_cvi_rtc_set_alarm_time(CVI_RTC_BASE, set_sec);

        LOG_D("GET_ALARM %d:%d:%d", alarm->tm_hour, alarm->tm_min, alarm->tm_sec);
    }

    hal_cvi_rtc_alarm_enable(CVI_RTC_BASE, alarm->enable);

    return RT_EOK;
}

static rt_err_t _rtc_get_timespec(struct timespec *tp)
{
    if (tp == RT_NULL) return -RT_ERROR;

    unsigned long sec;

    if (hal_cvi_rtc_get_time_sec(CVI_RTC_BASE, &sec) != RT_EOK) {
        LOG_E("get timespec failed");
        return -RT_ERROR;
    }

    tp->tv_sec  = sec;
    tp->tv_nsec = 0;
    return RT_EOK;
}

static rt_err_t _rtc_set_timespec(struct timespec *tp)
{
    if (tp == RT_NULL) return -RT_ERROR;

    /* Ignore tv_nsec */
    hal_cvi_rtc_set_time(CVI_RTC_BASE, tp->tv_sec);

    return RT_EOK;
}

static const struct rt_rtc_ops _rtc_ops = {
    _rtc_init,        _rtc_get_secs,    _rtc_set_secs,
#ifdef RT_USING_ALARM
    _rtc_get_alarm,   _rtc_set_alarm,
#else
    RT_NULL,          RT_NULL,
#endif
    _rtc_get_timeval, _rtc_set_timeval, _rtc_get_timespec, _rtc_set_timespec,
};

static int rt_hw_rtc_init(void)
{
    rt_err_t result;

    rtc_device.rtc_dev.ops = &_rtc_ops;
    result = rt_hw_rtc_register(&rtc_device.rtc_dev, "rtc", RT_DEVICE_FLAG_RDWR, RT_NULL);
    if (result != RT_EOK) {
        LOG_E("rtc register err code: %d", result);
        return result;
    }

#ifdef RT_USING_ALARM
    rt_hw_interrupt_install(RTC_ALARM_IRQ_NUM, rt_hw_rtc_isr, RT_NULL, "rtc");
    rt_hw_interrupt_umask(RTC_ALARM_IRQ_NUM);
#endif

#if defined(RTC_USING_SW_CALIB)
    if ((mmio_read_32(CVI_RTC_CTRL_BASE + 0x8) & 0x400) == 0x0) {
        /* Enable calibration only when use internal osc */
        hal_cvi_rtc_32k_coarse_value_calib(CVI_RTC_BASE, CVI_RTC_CTRL_BASE);
        hal_cvi_rtc_32k_fine_value_calib(CVI_RTC_BASE, CVI_RTC_CTRL_BASE);
        LOG_E("rtc 32k calibration has been completed\n");
    } else {
        LOG_E("Disable calibration because using external xtal\n");
    }
#endif

    LOG_D("rtc init success");

    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_rtc_init);
