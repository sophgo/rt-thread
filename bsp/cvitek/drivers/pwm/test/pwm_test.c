#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <finsh.h>
#include <string.h>
#include <stdint.h>
#include <mmio.h>
#define PWM_DEV_NAME        "pwm2" // custom to your pwm device
static const char *pwm_dev_name = PWM_DEV_NAME; /* 运行期可被 pwm_test/autotest 的 argv[1] 覆盖 */
#define PWM_DEV_CHANNEL     8       // custom to your pwm channel

/* local enabled-state tracking (test-only) */
#ifndef PWM_TRACK_MAX_CH
#define PWM_TRACK_MAX_CH 16
#endif
static rt_bool_t s_pwm_enabled[PWM_TRACK_MAX_CH] = {0};

static int _pwm_norm_ch(int ch)
{
    if (ch < 0) ch = -ch;
    if (ch >= PWM_TRACK_MAX_CH) ch &= (PWM_TRACK_MAX_CH - 1);
    return ch;
}

static rt_bool_t pwm_is_enabled_local(int ch)
{
    ch = _pwm_norm_ch(ch);
    return s_pwm_enabled[ch];
}

static void pwm_set_enabled_local(int ch, rt_bool_t en)
{
    ch = _pwm_norm_ch(ch);
    s_pwm_enabled[ch] = en;
}

static void pwm_ensure_enabled(rt_device_t dev, int ch)
{
    if (!pwm_is_enabled_local(ch))
    {
        rt_pwm_enable(dev, ch);
        pwm_set_enabled_local(ch, RT_TRUE);
        rt_kprintf("PWM channel %d enabled\n", ch);
    }
}


int pwm_test(int argc, char *argv[])
{
    rt_device_t pwm_dev;
    rt_uint64_t period = 1000000; // 1MHz
    rt_uint64_t pulse = 500000; // duty cycle 50%
    int polarity = 0;
    int count = 10;
    int i;
    int ch = PWM_DEV_CHANNEL;
    int argi = 1;
    /* dev ch period pulse count */
    if (argc > argi) { pwm_dev_name = argv[argi]; argi++; }
    if (argc > argi) { ch = atoi(argv[argi]); argi++; }
    if (argc > argi) { period = strtoull(argv[argi], RT_NULL, 10); argi++; }
    if (argc > argi) { pulse = strtoull(argv[argi], RT_NULL, 10); argi++; }
    if (argc > argi) { count = atoi(argv[argi]); argi++; }
    if (argc > argi) { polarity = atoi(argv[argi]); argi++; }

    rt_kprintf("\n--- Starting PWM Driver Test ---\n");

    pwm_dev = rt_device_find(pwm_dev_name);
    if (pwm_dev == RT_NULL) {
        rt_kprintf("[FAIL] PWM device %s not found!\n", pwm_dev_name);
        return -1;
    }
    rt_kprintf("Set PWM: ch=%d period=%llu ns, pulse=%llu ns\n", ch, (unsigned long long)period, (unsigned long long)pulse);
    if (rt_pwm_set(pwm_dev, ch, period, pulse) != RT_EOK)
    {
        rt_kprintf("[FAIL] rt_pwm_set failed\n");
        return -1;
    }
    if (rt_pwm_set_polarity(pwm_dev, ch, polarity) != RT_EOK)
    {
        rt_kprintf("[FAIL] rt_pwm_set_polarity failed\n");
        return -1;
    }
    // ensure enabled
    pwm_ensure_enabled(pwm_dev, ch);

    rt_kprintf("--- [PASS] PWM Driver Test ---\n");
    return 0;
}
static void pwm_autotest(int argc, char *argv[])
{
    rt_device_t pwm_dev;
    rt_uint32_t period = 1000000; // 1MHz
    rt_uint32_t pulse = 500000; // duty cycle 50%
    int count = 10;
    int i;
    int ch = PWM_DEV_CHANNEL;
    int argi = 1;
    /*dev ch period pulse count */
    if (argc > argi) { pwm_dev_name = argv[argi]; argi++; }
    if (argc > argi) { ch = atoi(argv[argi]); argi++; }
    if (argc > argi) { period = (rt_uint32_t)atoi(argv[argi]); argi++; }
    if (argc > argi) { pulse = (rt_uint32_t)atoi(argv[argi]); argi++; }
    if (argc > argi) { count = atoi(argv[argi]); argi++; }

    rt_kprintf("\n--- Starting PWM Driver Test ---\n");

    pwm_dev = rt_device_find(pwm_dev_name);
    if (pwm_dev == RT_NULL) {
        rt_kprintf("[FAIL] PWM device %s not found!\n", pwm_dev_name);
        return;
    }

    rt_pwm_set(pwm_dev, ch, period, pulse);
    rt_kprintf("Set PWM: ch=%d period=%dns, pulse=%dns\n", ch, period, pulse);

    // ensure enabled
    pwm_ensure_enabled(pwm_dev, ch);

    //change pulse width
    rt_kprintf("Changing pulse width %d times...\n", count);
    for (i = 0; i < count; i++) {
        pulse = period * (100 + i * 80 / count) / 1000;
        rt_pwm_set(pwm_dev, ch, period, pulse);
        rt_kprintf("[%2d/%d] Pulse: %dns \n", i+1, count, pulse);
        rt_thread_mdelay(2000);
    }
    rt_kprintf("--- [PASS] PWM Driver Test ---\n");
}

static int parse_pwm_index(const char *name)
{
    int idx = -1;
    const char *p = name;
    while (*p) p++;
    while (p > name && (*(p-1) >= '0' && *(p-1) <= '9')) p--;
    if (*p) idx = atoi(p);
    return idx;
}


/* Explicit enable/disable commands */
static int pwm_test_enable_cmd(int argc, char *argv[])
{
    rt_device_t pwm_dev;
    int ch;
    ch = atoi(argv[1]);

    pwm_dev = rt_device_find(pwm_dev_name);
    if (pwm_dev == RT_NULL) {
        rt_kprintf("[FAIL] PWM device %s not found!\n", pwm_dev_name);
        return -1;
    }

    if (!pwm_is_enabled_local(ch))
    {
        rt_err_t r = rt_pwm_enable(pwm_dev, ch);
        if (r == RT_EOK)
        {
            pwm_set_enabled_local(ch, RT_TRUE);
            rt_kprintf("[OK] enabled ch=%d\n", ch);
        }
        else
        {
            rt_kprintf("[FAIL] enable ch=%d ret=%d\n", ch, r);
            return -1;
        }
    }
    else
    {
        rt_kprintf("[SKIP] ch=%d already enabled\n", ch);
    }
    return 0;
}

static int pwm_test_disable_cmd(int argc, char *argv[])
{
    rt_device_t pwm_dev;
    int ch;
    ch = atoi(argv[1]);

    pwm_dev = rt_device_find(pwm_dev_name);
    if (pwm_dev == RT_NULL) {
        rt_kprintf("[FAIL] PWM device %s not found!\n", pwm_dev_name);
        return -1;
    }

    rt_err_t r = rt_pwm_disable(pwm_dev, ch);
    if (r == RT_EOK)
    {
        pwm_set_enabled_local(ch, RT_FALSE);
        rt_kprintf("[OK] disabled ch=%d\n", ch);
        return 0;
    }
    rt_kprintf("[FAIL] disable ch=%d ret=%d\n", ch, r);
    return -1;
}
typedef struct {
    int ch;
    rt_uint32_t count;
} pwm_capture_params_t;

static void pwm_capture_thread(void *parameter)
{
    pwm_capture_params_t *p = (pwm_capture_params_t *)parameter;
    if (!p) return;

    struct rt_device_pwm *pwm = (struct rt_device_pwm *)rt_device_find(PWM_DEV_NAME);
    if (!pwm) {
        rt_kprintf("[FAIL] cannot find device %s\n", PWM_DEV_NAME);
        rt_free(p);
        return;
    }

    /* 1) config: set input mode and capture count */
    rt_pwm_capture_cfg_t cfg = { .channel = (rt_uint32_t)p->ch, .count = p->count ? p->count : 1 };
    if (rt_device_control(&pwm->parent, PWM_CMD_CAPTURE_CONFIG, &cfg) != RT_EOK) {
        rt_kprintf("[FAIL] capture config failed\n");
        rt_free(p);
        return;
    }
    /* 2) start capture */
    rt_pwm_capture_ch_t ch_arg = { .channel = (rt_uint32_t)p->ch };
    if (rt_device_control(&pwm->parent, PWM_CMD_CAPTURE_START, &ch_arg) != RT_EOK) {
        rt_kprintf("[FAIL] capture start failed\n");
        rt_free(p);
        return;
    }

    /* 3) poll read: until done_num reach target count or timeout */
    rt_uint32_t done_num = 0, time_us = 0;
    rt_pwm_capture_read_t r = { .channel = (rt_uint32_t)p->ch };
    while (1) {
        if (rt_device_control(&pwm->parent, PWM_CMD_CAPTURE_READ, &r) == RT_EOK) {
            done_num = r.done_num;
            if (done_num >= p->count) break;
        }
        rt_thread_mdelay(1);
    }

    /* 4) stop capture */
    (void)rt_device_control(&pwm->parent, PWM_CMD_CAPTURE_STOP, &ch_arg);

    if (done_num < p->count) {
        rt_kprintf("[FAIL] capture timeout: ch=%d count=%u done=%u\n", p->ch, p->count, done_num);
        rt_free(p);
        return;
    }
    rt_kprintf("[OK] capture done: ch=%d count=%u period_count=%u\n", p->ch, p->count, r.period_count);
    rt_free(p);
}

static int pwm_capture_test_bg(int argc, char *argv[])
{
    int ch = PWM_DEV_CHANNEL;
    rt_uint32_t count = 1;

    if (argc > 1) ch = atoi(argv[1]);
    if (argc > 2) count = (rt_uint32_t)atoi(argv[2]);

    pwm_capture_params_t *p = (pwm_capture_params_t *)rt_malloc(sizeof(*p));
    if (!p) {
        rt_kprintf("[FAIL] alloc params failed\n");
        return -1;
    }
    p->ch = ch;
    p->count = count;

    rt_thread_t tid = rt_thread_create("pwm_cap", pwm_capture_thread, p, 2048, RT_THREAD_PRIORITY_MAX - 2, 10);
    if (!tid) {
        rt_kprintf("[FAIL] create thread failed\n");
        rt_free(p);
        return -1;
    }
    rt_thread_startup(tid);
    rt_kprintf("[BG] pwm capture started: ch=%d count=%u\n", ch, count);
    return 0;
}
static void capture_disable_cmd(int argc, char *argv[])
{
    rt_device_t pwm_dev;
    int ch;
    ch = atoi(argv[1]);
    pwm_dev = rt_device_find(pwm_dev_name);
    if (pwm_dev == RT_NULL) {
        rt_kprintf("[FAIL] PWM device %s not found!\n", pwm_dev_name);
        return;
    }
    (void)rt_device_control(&pwm_dev->parent, PWM_CMD_CAPTURE_STOP, &ch);
    rt_kprintf("[OK] capture stopped: ch=%d\n", ch);
}
static void capture_enable_cmd(int argc, char *argv[])
{
    rt_device_t pwm_dev;
    int ch;
    ch = atoi(argv[1]);
    pwm_dev = rt_device_find(pwm_dev_name);
    if (pwm_dev == RT_NULL) {
        rt_kprintf("[FAIL] PWM device %s not found!\n", pwm_dev_name);
        return;
    }
    (void)rt_device_control(&pwm_dev->parent, PWM_CMD_CAPTURE_START, &ch);
    rt_kprintf("[OK] capture started: ch=%d\n", ch);
}
MSH_CMD_EXPORT(pwm_test, pwm_test);
MSH_CMD_EXPORT(pwm_autotest, pwm_autotest);
MSH_CMD_EXPORT(pwm_test_disable_cmd, pwm_test_disable_cmd);
MSH_CMD_EXPORT(pwm_test_enable_cmd, pwm_test_enable_cmd);
MSH_CMD_EXPORT(pwm_capture_test_bg, pwm capture via control (bg));
MSH_CMD_EXPORT(capture_disable_cmd, capture disable);
MSH_CMD_EXPORT(capture_enable_cmd, capture enable);