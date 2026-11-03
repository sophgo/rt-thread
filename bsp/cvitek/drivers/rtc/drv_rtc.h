#ifndef __DRV_RTC_H__
#define __DRV_RTC_H__

#include <rtdevice.h>
#include <rtthread.h>

#include "cvi_rtc.h"

#define LEAPS_THRU_END_OF(y) ((y) / 4 - (y) / 100 + (y) / 400)

typedef struct {
    int tm_sec;   ///< Second.      [0-59]
    int tm_min;   ///< Minute.      [0-59]
    int tm_hour;  ///< Hour.        [0-23]
    int tm_mday;  ///< Day.         [1-31]
    int tm_mon;   ///< Month.       [0-11]
    int tm_year;  ///< Year-1900.   [70- ]      !NOTE:Set 100 mean 2000
    int tm_wday;  ///< Day of week. [0-6 ]      !NOTE:Set 0 mean Sunday
    int tm_yday;  ///< Days in year.[0-365]     !NOTE:Set 0 mean January 1st
} cvi_rtc_time_t;

struct rtc_device_object {
    rt_rtc_dev_t rtc_dev;
};

static struct rtc_device_object rtc_device;

#endif /* __DRV_RTC_H__ */
