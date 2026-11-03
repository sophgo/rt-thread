/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author          Notes
 * 2025-07-09     shuhan.zhang    copy from doc hwtimer
 */

#include <finsh.h>
#include <rtdevice.h>
#include <rtthread.h>



static char     TIMER[7] = "timer0";
static int      flag     = 0;
static rt_err_t timer_timeout_cb(rt_device_t dev, rt_size_t size)
{
    rt_kprintf("enter hardware %s isr\n", TIMER);
    flag += 1;
    return 0;
}
static rt_err_t timer1_timeout_cb(rt_device_t dev, rt_size_t size)
{
    rt_kprintf("enter hardware %s isr\n", TIMER);
    flag += 1;
    return 0;
}

int timer_test_period(int t)
{
    rt_err_t          err;
    rt_hwtimerval_t   val;
    rt_device_t       dev = RT_NULL;
    rt_hwtimer_mode_t mode;

    rt_kprintf("\n--- Starting %s Driver Test ---\n", TIMER);
    if ((dev = rt_device_find(TIMER)) == RT_NULL) {
        rt_kprintf("[FAIL] No Device: %s\n", TIMER);
        return -1;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[FAIL] Open %s Fail\n", TIMER);
        return -1;
    }
    rt_device_set_rx_indicate(dev, timer_timeout_cb);

    // start periodic mode test
    mode = HWTIMER_MODE_PERIOD;
    err  = rt_device_control(dev, HWTIMER_CTRL_MODE_SET, &mode);

    val.sec  = t;
    val.usec = 0;
    rt_kprintf("SetTime: Sec %d, Usec %d\n", val.sec, val.usec);
    if (rt_device_write(dev, 0, &val, sizeof(val)) != sizeof(val)) {
        rt_kprintf("[FAIL] SetTime Fail\n");
        goto EXIT;
    }
    rt_kprintf("Sleep %d sec\n", t);
    rt_thread_delay(t * RT_TICK_PER_SECOND);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);

    rt_kprintf("Sleep %d sec\n", t);
    rt_thread_delay(t * RT_TICK_PER_SECOND);
    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);
    err = rt_device_control(dev, HWTIMER_CTRL_STOP, RT_NULL);
    if (err != RT_EOK) {
        rt_kprintf("[FAIL] Stop Timer Fail\n");
        goto EXIT;
    }
    rt_kprintf("%s Stoped\n", TIMER);
    if (flag > 1) {
        rt_kprintf("[PASS] %s IRQ comes %d times\n", TIMER, flag);
        flag = 0;
    } else {
        rt_kprintf("[FAIL] No %s IRQ or only one IRQ\n", TIMER);
        goto EXIT;
    }

EXIT:
    err = rt_device_close(dev);
    rt_kprintf("Close %s\n", TIMER);
    rt_kprintf("\n--- END %s Driver Test ---\n", TIMER);

    return err;
}

int timer_test_irq_handle(int t)
{
    rt_err_t          err;
    rt_hwtimerval_t   val;
    rt_device_t       dev = RT_NULL;
    rt_hwtimer_mode_t mode;

    rt_kprintf("\n--- Starting %s Driver Test ---\n", TIMER);
    if ((dev = rt_device_find(TIMER)) == RT_NULL) {
        rt_kprintf("[FAIL] No Device: %s\n", TIMER);
        return -1;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[FAIL] Open %s Fail\n", TIMER);
        return -1;
    }
    rt_device_set_rx_indicate(dev, timer_timeout_cb);

    mode = HWTIMER_MODE_PERIOD;
    err  = rt_device_control(dev, HWTIMER_CTRL_MODE_SET, &mode);

    val.sec  = t;
    val.usec = 0;
    rt_kprintf("SetTime: Sec %d, Usec %d\n", val.sec, val.usec);
    if (rt_device_write(dev, 0, &val, sizeof(val)) != sizeof(val)) {
        rt_kprintf("[FAIL] SetTime Fail\n");
        goto EXIT;
    }
    rt_kprintf("Sleep %d sec\n", t);
    rt_thread_delay(t * RT_TICK_PER_SECOND);
    if (flag == 0) {
        rt_kprintf("[FAIL] No %s IRQ\n", TIMER);
        goto EXIT;
    } else {
        rt_kprintf("[PASS] %s IRQ comes %d times\n", TIMER, flag);
        flag = 0;
    }

EXIT:
    err = rt_device_close(dev);
    rt_kprintf("Close %s\n", TIMER);
    rt_kprintf("\n--- END %s Driver Test ---\n", TIMER);

    return err;
}

int timer_test_oneshot(int t)
{
    rt_err_t          err;
    rt_hwtimerval_t   val;
    rt_device_t       dev = RT_NULL;
    rt_hwtimer_mode_t mode;

    rt_kprintf("\n--- Starting %s Driver Test ---\n", TIMER);
    if ((dev = rt_device_find(TIMER)) == RT_NULL) {
        rt_kprintf("[FAIL] No Device: %s\n", TIMER);
        return -1;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[FAIL] Open %s Fail\n", TIMER);
        return -1;
    }
    rt_device_set_rx_indicate(dev, timer_timeout_cb);

    mode = HWTIMER_MODE_ONESHOT;
    err  = rt_device_control(dev, HWTIMER_CTRL_MODE_SET, &mode);

    val.sec  = t;
    val.usec = 0;
    rt_kprintf("SetTime: Sec %d, Usec %d\n", val.sec, val.usec);
    if (rt_device_write(dev, 0, &val, sizeof(val)) != sizeof(val)) {
        rt_kprintf("[FAIL] SetTime Fail\n");
        goto EXIT;
    }
    rt_kprintf("Sleep %d sec\n", t);
    rt_thread_delay(t * RT_TICK_PER_SECOND);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);

    rt_kprintf("Sleep %d sec\n", t);
    rt_thread_delay(t * RT_TICK_PER_SECOND);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);

    if (flag == 1) {
        rt_kprintf("[PASS] %s IRQ comes %d times\n", TIMER, flag);
        flag = 0;
    } else {
        rt_kprintf("[FAIL] No %s IRQ or more than one IRQ\n", TIMER);
        goto EXIT;
    }

EXIT:
    err = rt_device_close(dev);
    rt_kprintf("Close %s\n", TIMER);
    rt_kprintf("\n--- END %s Driver Test ---\n", TIMER);

    return err;
}

int timer_test_precise(int t)
{
    rt_err_t          err;
    rt_hwtimerval_t   val;
    rt_device_t       dev = RT_NULL;
    rt_tick_t         tick;
    rt_hwtimer_mode_t mode;

    rt_kprintf("\n--- Starting %s Driver Test ---\n", TIMER);
    if ((dev = rt_device_find(TIMER)) == RT_NULL) {
        rt_kprintf("[FAIL] No Device: %s\n", TIMER);
        return -1;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[FAIL] Open %s Fail\n", TIMER);
        return -1;
    }
    rt_device_set_rx_indicate(dev, timer_timeout_cb);

    mode = HWTIMER_MODE_ONESHOT;
    err  = rt_device_control(dev, HWTIMER_CTRL_MODE_SET, &mode);

    tick = rt_tick_get();
    rt_kprintf("Start Timer> Tick: %d\n", tick);

    val.sec  = 0;
    val.usec = t;
    rt_kprintf("SetTime: Sec %d, Usec %d\n", val.sec, val.usec);
    if (rt_device_write(dev, 0, &val, sizeof(val)) != sizeof(val)) {
        rt_kprintf("[FAIL] SetTime Fail\n");
        goto EXIT;
    }
    rt_kprintf("Sleep %d usec\n", t);
    rt_thread_delay(t / RT_TICK_PER_SECOND);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);
    if (flag == 1) {
        rt_kprintf("[PASS] %s IRQ comes %d times\n", TIMER, flag);
        flag = 0;
    } else {
        rt_kprintf("[FAIL] No %s IRQ or more than one IRQ\n", TIMER);
        goto EXIT;
    }

EXIT:
    err = rt_device_close(dev);
    rt_kprintf("Close %s\n", TIMER);
    rt_kprintf("\n--- END %s Driver Test ---\n", TIMER);

    return err;
}

int timer_test_drift(int t)
{
    rt_err_t          err;
    rt_hwtimerval_t   val;
    rt_device_t       dev = RT_NULL;
    rt_tick_t         s_tick, e_tick;
    rt_hwtimer_mode_t mode;

    rt_kprintf("\n--- Starting %s Driver Test ---\n", TIMER);
    if ((dev = rt_device_find(TIMER)) == RT_NULL) {
        rt_kprintf("[FAIL] No Device: %s\n", TIMER);
        return -1;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[FAIL] Open %s Fail\n", TIMER);
        return -1;
    }
    rt_device_set_rx_indicate(dev, timer_timeout_cb);

    mode = HWTIMER_MODE_ONESHOT;
    err  = rt_device_control(dev, HWTIMER_CTRL_MODE_SET, &mode);

    val.sec  = t;
    val.usec = 0;
    rt_kprintf("SetTime: Sec %d, Usec %d\n", val.sec, val.usec);
    if (rt_device_write(dev, 0, &val, sizeof(val)) != sizeof(val)) {
        rt_kprintf("[FAIL] SetTime Fail\n");
        goto EXIT;
    }
    s_tick = rt_tick_get();
    rt_kprintf("Start Timer> Tick: %d\n", s_tick);
    rt_kprintf("Sleep %d sec\n", t);
    rt_thread_delay(t * RT_TICK_PER_SECOND);

    e_tick = rt_tick_get();
    rt_kprintf("End Timer> Tick: %d\n", e_tick);
    rt_kprintf("Timer drift: %d\n", e_tick - s_tick - t * RT_TICK_PER_SECOND);
    if (e_tick - s_tick - t * RT_TICK_PER_SECOND < 5) {
        rt_kprintf("[PASS] %s drift is less than 5 ticks\n", TIMER);
    } else {
        rt_kprintf("[FAIL] %s drift is more than 5 ticks\n", TIMER);
        goto EXIT;
    }

    // rt_device_read(dev, 0, &val, sizeof(val));
    // rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);

EXIT:
    err = rt_device_close(dev);
    rt_kprintf("Close %s\n", TIMER);
    rt_kprintf("\n--- END %s Driver Test ---\n", TIMER);

    return err;
}

int timer_test_sub_thread_oneshot(void *parameter)
{
    int t = *(int *)parameter;
    timer_test_oneshot(t);
}

int timer_test_parallelism(char *timer1, int t)
{
    rt_thread_t thread1 = rt_thread_create("t1", (void (*)(void *))timer_test_sub_thread_oneshot,
                                           (void *)&t, 2048, 20, 10);
    if (thread1 != RT_NULL) {
        rt_kprintf("child thread call timer_test_oneshot\n");
        rt_thread_startup(thread1);
    } else {
        rt_kprintf("Failed to create thread1\n");
        return -1;
    }
    rt_kprintf("main thread call timer_test_oneshot\n");
    rt_err_t          err;
    rt_hwtimerval_t   val;
    rt_device_t       dev = RT_NULL;
    rt_hwtimer_mode_t mode;

    rt_kprintf("\n--- Starting %s Driver Test ---\n", timer1);
    if ((dev = rt_device_find(timer1)) == RT_NULL) {
        rt_kprintf("[FAIL] No Device: %s\n", timer1);
        return -1;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[FAIL] Open %s Fail\n", timer1);
        return -1;
    }
    rt_device_set_rx_indicate(dev, timer1_timeout_cb);

    mode = HWTIMER_MODE_ONESHOT;
    err  = rt_device_control(dev, HWTIMER_CTRL_MODE_SET, &mode);

    val.sec  = t;
    val.usec = 0;
    rt_kprintf("SetTime: Sec %d, Usec %d\n", val.sec, val.usec);
    if (rt_device_write(dev, 0, &val, sizeof(val)) != sizeof(val)) {
        rt_kprintf("[FAIL] SetTime Fail\n");
        goto EXIT;
    }
    rt_kprintf("Sleep %d sec\n", t);
    rt_thread_delay(t * RT_TICK_PER_SECOND);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);

    rt_kprintf("Sleep %d sec\n", t);
    rt_thread_delay(t * RT_TICK_PER_SECOND);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);


EXIT:
    err = rt_device_close(dev);
    rt_kprintf("Close %s\n", timer1);
    rt_kprintf("\n--- END %s Driver Test ---\n", timer1);

    return err;
}

int timer_test_dynamic(int t1, int t2)
{
    rt_err_t          err;
    rt_hwtimerval_t   val;
    rt_device_t       dev = RT_NULL;
    rt_hwtimer_mode_t mode;

    rt_kprintf("\n--- Starting %s Driver Test ---\n", TIMER);
    if ((dev = rt_device_find(TIMER)) == RT_NULL) {
        rt_kprintf("[FAIL] No Device: %s\n", TIMER);
        return -1;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[FAIL] Open %s Fail\n", TIMER);
        return -1;
    }
    rt_device_set_rx_indicate(dev, timer1_timeout_cb);

    mode = HWTIMER_MODE_ONESHOT;
    err  = rt_device_control(dev, HWTIMER_CTRL_MODE_SET, &mode);

    val.sec  = t1;
    val.usec = 0;
    rt_kprintf("SetTime: Sec %d, Usec %d\n", val.sec, val.usec);
    if (rt_device_write(dev, 0, &val, sizeof(val)) != sizeof(val)) {
        rt_kprintf("[FAIL] SetTime Fail\n");
        goto EXIT;
    }
    rt_kprintf("Sleep %d sec\n", 2);
    rt_thread_delay(2 * RT_TICK_PER_SECOND);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);

    val.sec  = t2;
    val.usec = 0;
    rt_kprintf("SetTime: Sec %d, Usec %d\n", val.sec, val.usec);
    if (rt_device_write(dev, 0, &val, sizeof(val)) != sizeof(val)) {
        rt_kprintf("[FAIL] SetTime Fail\n");
        goto EXIT;
    }

    rt_kprintf("Sleep %d sec\n", 3);
    rt_thread_delay(3 * RT_TICK_PER_SECOND);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("Read: Sec = %d, Usec = %d\n", val.sec, val.usec);

    err = rt_device_control(dev, HWTIMER_CTRL_STOP, RT_NULL);
    if (err != RT_EOK) {
        rt_kprintf("[FAIL] Stop Timer Fail\n");
        goto EXIT;
    }
    rt_kprintf("%s Stoped\n", TIMER);

    if (flag >= 1) {
        rt_kprintf("[PASS] %s IRQ comes %d times\n", TIMER, flag);
        flag = 0;
    } else {
        rt_kprintf("[FAIL] No %s IRQ or more than one IRQ\n", TIMER);
        goto EXIT;
    }

EXIT:
    err = rt_device_close(dev);
    rt_kprintf("Close %s\n", TIMER);
    rt_kprintf("\n--- END %s Driver Test ---\n", TIMER);

    return err;
}

int timer_test_error(void)
{
    rt_err_t          err;
    rt_device_t       dev = RT_NULL;
    rt_hwtimer_mode_t mode;

    rt_kprintf("\n--- Starting %s Driver Test ---\n", TIMER);
    if ((dev = rt_device_find(TIMER)) == RT_NULL) {
        rt_kprintf("[FAIL] No Device: %s\n", TIMER);
        return -1;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[FAIL] Open %s Fail\n", TIMER);
        return -1;
    }

    mode = HWTIMER_CTRL_MODE_SET;
    err  = rt_device_control(dev, HWTIMER_CTRL_MODE_SET, &mode);
    if (err == RT_EOK) {
        rt_kprintf("[FAIL] args test fail\n");
    }

    err = rt_device_control(dev, 222222, RT_NULL);
    if (err == RT_EOK) {
        rt_kprintf("[FAIL] args test fail\n");
        goto EXIT;
    }

EXIT:
    err = rt_device_close(dev);
    rt_kprintf("Close %s\n", TIMER);
    rt_kprintf("\n--- END %s Driver Test ---\n", TIMER);

    return err;
}

int timer_test(int32_t argc, char **argv)
{
    int test_case = atoi(argv[1]);
    if (argc < 2 && (test_case != 8)) {
        rt_kprintf("para error, usage: timer_test [<timer0>...<timer7>] <case_num> <t1> <t2>\r\n");
        rt_kprintf(
            "case_num 1 <timer0> <t1>: periodic mode test, <t1> is in seconds\r\n");
        rt_kprintf(
            "case_num 2 <timer0> <t1>: interrupt callback test, <t1> is in seconds\r\n");
        rt_kprintf(
            "case_num 3 <timer0> <t1>: one-shot mode test, <t1> is in seconds\r\n");
        rt_kprintf(
            "case_num 4 <timer0> <t1>: precise mode test, <t1> is in usec\r\n");
        rt_kprintf(
            "case_num 5 <timer0> <t1>: drift test, <t1> is in seconds\r\n");
        rt_kprintf(
            "case_num 6 <timer0> <timer1> <t2>: parallelism test, <t2> is in seconds\r\n");
        rt_kprintf(
            "case_num 7 <timer0> <t1> <t2>: dynamic mode test, <t1> is in seconds, <t2> is in seconds\r\n");
        rt_kprintf("case_num 8 <timer0>: error test\r\n");
        return -1;
    }

    int timer_index = atoi(argv[2]);
    if (timer_index < 0 || timer_index > 7) {
        rt_kprintf("para error: invalid timer index\r\n");
        return -1;
    } else {
        TIMER[5] = timer_index + '0';
        rt_kprintf("test %s\n", TIMER);
    }
    int t1 = 5;
    int t2 = 3;

    char TIMER1[7] = "timer1";
    if (test_case == 6) {
        timer_index = atoi(argv[3]);
        if (timer_index < 0 || timer_index > 7) {
            rt_kprintf("para error: invalid timer index\r\n");
            return -1;
        } else {
            TIMER1[5] = timer_index + '0';
            rt_kprintf("test %s\n", TIMER1);
        }
    }
    if (test_case != 8) {
        t1 = atoi(argv[3]);
    }

    rt_kprintf("t1 = %d, t2 = %d\n", t1, t2);

    if (1 == test_case) {
        return timer_test_period(t1);
    } else if (2 == test_case) {
        return timer_test_irq_handle(t1);
    } else if (3 == test_case) {
        return timer_test_oneshot(t1);
    } else if (4 == test_case) {
        return timer_test_precise(t1);
    } else if (5 == test_case) {
        return timer_test_drift(t1);
    } else if (6 == test_case) {
        t2 = atoi(argv[4]);
        return timer_test_parallelism(TIMER1, t2);
    } else if (7 == test_case) {
        t2 = atoi(argv[4]);
        return timer_test_dynamic(t1, t2);
    } else if (8 == test_case) {
        return timer_test_error();
    } else {
        rt_kprintf("para error: invalid case num\r\n");
        return -1;
    }
}

MSH_CMD_EXPORT(timer_test, "Test hardware timer");

