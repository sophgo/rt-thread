#include <rtdevice.h>
#include <rtthread.h>
#include <time.h>

#define DBG_TAG "test_rtc"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static rt_device_t rtc_dev;
static rt_alarm_t *alarm;

static void print_time(void)
{
    time_t now = time(RT_NULL);
    rt_kprintf("%s", ctime(&now));
}

static void rt_alarm_callback(rt_alarm_t alarm, time_t timestamp)
{
    rt_kprintf("Alarm triggerd at %s\n", ctime(&timestamp));
}

int test_rtc(int argc, char **argv)
{
    rt_err_t ret = RT_EOK;

    if (argc < 2) {
        goto usage;
    }

    rtc_dev = rt_device_find("rtc");
    if (!rtc_dev) {
        LOG_E("RTC device not found!");
        return -1;
    }

    if (rt_device_open(rtc_dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        LOG_E("Failed to open RTC device!");
        return -1;
    }

    if (!rt_strcmp(argv[1], "-r")) {
        print_time();
        rt_device_close(rtc_dev);
        return 0;
    }

    if (!rt_strcmp(argv[1], "-s") && argc == 8) {
        int year  = atoi(argv[2]);
        int month = atoi(argv[3]);
        int day   = atoi(argv[4]);
        int hour  = atoi(argv[5]);
        int min   = atoi(argv[6]);
        int sec   = atoi(argv[7]);

        ret = set_date(year, month, day);
        if (ret != RT_EOK) {
            LOG_E("Set date failed: %d", ret);
            return -1;
        }
        rt_thread_mdelay(1);  // wait date stable
        ret = set_time(hour, min, sec);
        if (ret != RT_EOK) {
            LOG_E("Set time failed: %d", ret);
            rt_device_close(rtc_dev);
            return -1;
        }
        rt_thread_mdelay(1);  // wait date stable

        LOG_I("RTC set successfully. Current time:");
        print_time();
        rt_device_close(rtc_dev);
        return 0;
    }
    if (!rt_strcmp(argv[1], "-a")) {
        time_t    now;
        struct tm tm;

        struct rt_alarm_setup setup = {0};

        // Disable alarm if test_rtc -a 0
        if (argc == 3 && !rt_strcmp(argv[2], "0")) {
            if (alarm == RT_NULL) {
                rt_device_close(rtc_dev);
                return 0;
            } else {
                rt_alarm_stop(alarm);
                rt_device_close(rtc_dev);
            }
            return 0;
        }

        // Set alarm and start
        if (argc >= 4 && !rt_strcmp(argv[2], "1")) {
            now = time(RT_NULL);
            now += atoi(argv[3]);
            gmtime_r(&now, &tm);
            setup.wktime.tm_sec  = tm.tm_sec;
            setup.wktime.tm_min  = tm.tm_min;
            setup.wktime.tm_hour = tm.tm_hour;
            setup.wktime.tm_mday = tm.tm_mday;
            setup.wktime.tm_mon  = tm.tm_mon;
            setup.wktime.tm_year = tm.tm_year;
            setup.wktime.tm_wday = tm.tm_wday;

            setup.flag = RT_ALARM_ONESHOT | RT_ALARM_STATE_START;  // alarm trigger once
            // create alarm if we don't have one
            if (alarm == RT_NULL)
                alarm = rt_alarm_create(rt_alarm_callback, &setup);
            else
                rt_alarm_control(alarm, RT_ALARM_CTRL_MODIFY, &setup);
            rt_alarm_start(alarm);
            rt_device_close(rtc_dev);
            return 0;
        }
    }

    /* Boundary test */
    if (!rt_strcmp(argv[1], "-b")) {
        LOG_I("Testing boundary values...");

        // min time (1970-01-01 00:00:00)
        set_date(1970, 1, 2);
        rt_thread_mdelay(1);  // wait register update
        set_time(0, 8, 0);
        rt_thread_mdelay(1);  // wait register update
        LOG_I("Min time set:");
        print_time();

        // max time (9999-12-31 23:59:59)
        set_date(9999, 12, 31);
        rt_thread_mdelay(1);
        set_time(23, 59, 59);
        rt_thread_mdelay(1);
        LOG_I("Max time set:");
        print_time();
        rt_device_close(rtc_dev);
        return 0;
    }

    /* date boundary test */
    if (!rt_strcmp(argv[1], "-d")) {
        LOG_I("Testing date boundaries...");

        // end of month (4-30->5-1)
        set_date(2023, 4, 30);
        rt_thread_mdelay(10);
        set_time(23, 59, 59);
        rt_thread_mdelay(2100);  // waite 2.1s
        LOG_I("After month-end transition:");
        print_time();

        // Normal February (2023-02-28->03-01)
        set_date(2023, 2, 28);
        rt_thread_mdelay(10);
        set_time(23, 59, 59);
        rt_thread_mdelay(2100);
        LOG_I("After non-leap February:");
        print_time();

        // leap year February (2024-02-29->03-01)
        set_date(2024, 2, 29);
        rt_thread_mdelay(1);
        set_time(23, 59, 59);
        rt_thread_mdelay(2100);
        LOG_I("After leap February:");
        print_time();

        // end of year (2023-12-31->2024-01-01)
        set_date(2023, 12, 31);
        rt_thread_mdelay(1);
        set_time(23, 59, 59);
        rt_thread_mdelay(2100);
        LOG_I("After year-end transition:");
        print_time();
        rt_device_close(rtc_dev);
        return 0;
    }
usage:
    LOG_E("Invalid arguments!");
    rt_kprintf("RTC Test Usage:\n");
    rt_kprintf("  test_rtc -r                  Read current time\n");
    rt_kprintf("  test_rtc -s y m d H M S      Set time (e.g. test_rtc -s 2025 8 15 12 30 0)\n");
    rt_kprintf(
        "  test_rtc -a [en][n] seconds  Set alarm (e.g. test_rtc -a 1 10 / test_rtc -a 0)\n");
    rt_kprintf("  test_rtc -a 0                Disable alarm\n");
    rt_kprintf("  test_rtc -b                  Test boundary values\n");
    rt_kprintf("  test_rtc -d                  Test date boundaries\n");
    if (rtc_dev)
        rt_device_close(rtc_dev);
    return 1;
}
MSH_CMD_EXPORT(test_rtc, RTC test commands);