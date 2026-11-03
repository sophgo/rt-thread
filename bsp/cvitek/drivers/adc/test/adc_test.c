#include <rtthread.h>
#include <rtdevice.h>
#include <time.h> // 需要为 RTC 功能添加此头文件

#define ADC_DEV_NAME        "adc1" // custom to your adc device
#define ADC_CHANNEL         0       // every adcN device has a single channel 0
#define REFERENCE_VOLTAGE   1.5f
#define ADC_RESOLUTION      4096

int test_adc(int argc, char *argv[])
{
    rt_adc_device_t adc_dev;
    rt_uint32_t raw_value;
    rt_uint32_t voltage;
    rt_err_t ret;
    int count = 10;

    const char *adc_name = ADC_DEV_NAME; /* 默认 adc1，可由 argv[1] 覆盖 */
    if (argc > 1) adc_name = argv[1];
    if (argc > 2) {
        count = atoi(argv[2]);
        if (count <= 0) count = 10;
    }

    rt_kprintf("\n--- Starting ADC Driver Test ---\n");

    adc_dev = (rt_adc_device_t)rt_device_find(adc_name);
    if (adc_dev == RT_NULL) {
        rt_kprintf("[FAIL] ADC device %s not found!\n", adc_name);
        return -1;
    }

    // enable adc channel
    ret = rt_adc_enable(adc_dev, ADC_CHANNEL);
    if (ret != RT_EOK) {
        rt_kprintf("[FAIL] Enable ADC channel %d failed: %d\n", ADC_CHANNEL, ret);
        return -1;
    }
    rt_kprintf("ADC channel %d enabled\n", ADC_CHANNEL);

    // sampling
    rt_kprintf("Sampling %d times:\n", count);
    for (int i = 0; i < count; i++) {

        raw_value = rt_adc_read(adc_dev, ADC_CHANNEL);
        
        voltage = (raw_value * REFERENCE_VOLTAGE * 1000) / ADC_RESOLUTION;  // mv
        
        rt_kprintf("[%2d/%d] Raw: %4d -> Voltage: %d.%03dV\n", 
                  i + 1, count, raw_value, voltage / 1000, voltage % 1000);
        
        rt_thread_mdelay(200);
    }

    rt_adc_disable(adc_dev, ADC_CHANNEL);
    rt_kprintf("ADC channel %d disabled\n", ADC_CHANNEL);
    rt_kprintf("[PASS] ADC Driver Test Pass\n");
    rt_kprintf("\n--- END ADC Driver Test ---\n");
    return 0;
}

MSH_CMD_EXPORT(test_adc, ADC device test e.g: adc_test [dev=adc1] [count=10]);

#define ADC_RATE_TEST_CHANNEL   0
#define TARGET_VOLTAGE_MV       900     // 目标电压 0.9V = 900mV
#define VOLTAGE_TOLERANCE_MV    50      // 电压容忍范围 ±50mV
#define TARGET_ADC_VALUE        2458    // 0.9V对应的ADC原始值 (0.9/1.5)*4096
#define ADC_TOLERANCE           136     // ADC值容忍范围 ±136 (对应±50mV)

// 用于在定时器和主循环之间通信的全局变量
static volatile rt_bool_t timer_expired = RT_FALSE;
static volatile rt_uint64_t adc_sample_count = 0;
static volatile rt_uint64_t target_hit_count = 0; // 目标值命中次数
static rt_timer_t rate_test_timer = RT_NULL;

/**
 * 定时器超时回调函数
 * 注意：此函数在定时器中断上下文中执行
 */
static void timer_timeout_callback(void *parameter)
{
    timer_expired = RT_TRUE;
}

/**
 * 检查ADC采样值是否在目标范围内
 */
static rt_bool_t is_target_voltage(rt_uint32_t adc_value)
{
    if (adc_value >= (TARGET_ADC_VALUE - ADC_TOLERANCE) && 
        adc_value <= (TARGET_ADC_VALUE + ADC_TOLERANCE)) {
        return RT_TRUE;
    }
    return RT_FALSE;
}

static void adc_rate_test(int argc, char *argv[])
{
    rt_adc_device_t adc_dev = RT_NULL;
    rt_err_t ret;
    int delay_us = 4; // 默认延时4微秒，略大于规格要求的3.125微秒
    rt_uint32_t adc_value = 0;
    rt_uint32_t voltage_mv = 0;

    const char *rate_adc_name = ADC_DEV_NAME; /* 默认 adc1，可由 argv[1] 覆盖 */
    /* 如果用户提供了设备名/延时参数 */
    if (argc > 1) rate_adc_name = argv[1];
    if (argc > 2) {
        delay_us = atoi(argv[2]);
        if (delay_us < 1) delay_us = 4;
    }

    rt_kprintf("\n--- Starting ADC Sample Rate Test (Timer-based) ---\n");
    rt_kprintf("Sampling interval: %d microseconds\n", delay_us);
    rt_kprintf("Target voltage: %d.%03dV (ADC: %d ± %d)\n", 
              TARGET_VOLTAGE_MV / 1000, TARGET_VOLTAGE_MV % 1000, 
              TARGET_ADC_VALUE, ADC_TOLERANCE);

    // 1. 查找 ADC 设备
    adc_dev = (rt_adc_device_t)rt_device_find(rate_adc_name);
    if (adc_dev == RT_NULL) {
        rt_kprintf("[FAIL] ADC device '%s' not found!\n", rate_adc_name);
        return;
    }

    // 2. 使能 ADC 通道
    ret = rt_adc_enable(adc_dev, ADC_RATE_TEST_CHANNEL);
    if (ret != RT_EOK) {
        rt_kprintf("[FAIL] Enable ADC channel %d failed: %d\n", ADC_RATE_TEST_CHANNEL, ret);
        return;
    }

    // 3. 创建一个 1 秒的单次定时器
    rate_test_timer = rt_timer_create("adc_rate_timer",
                                     timer_timeout_callback,
                                     RT_NULL,
                                     RT_TICK_PER_SECOND,  // 1 秒
                                     RT_TIMER_FLAG_ONE_SHOT);
    if (rate_test_timer == RT_NULL) {
        rt_kprintf("[FAIL] Failed to create timer.\n");
        rt_adc_disable(adc_dev, ADC_RATE_TEST_CHANNEL);
        return;
    }

    rt_kprintf("Timer created for 1 second. Starting continuous sampling...\n");

    // 4. 重置标志位和计数器，启动定时器
    timer_expired = RT_FALSE;
    adc_sample_count = 0;
    target_hit_count = 0;

    rt_timer_start(rate_test_timer);

    // 5. 连续采样直到定时器超时，每次采样后添加延时
    while (timer_expired == RT_FALSE) {
        adc_value = rt_adc_read(adc_dev, ADC_RATE_TEST_CHANNEL);
        adc_sample_count++;

        // 检查是否命中目标电压范围
        if (is_target_voltage(adc_value)) {
            target_hit_count++;
            voltage_mv = (adc_value * REFERENCE_VOLTAGE * 1000) / ADC_RESOLUTION;
            
            // 在中断安全的方式下打印采样成功信息
            // rt_kprintf("[HIT #%d] ADC: %d -> %d.%03dV (Sample #%d)\n", 
            //           target_hit_count, adc_value, 
            //           voltage_mv / 1000, voltage_mv % 1000, 
            //           adc_sample_count);
        }
        
        // 添加微秒级延时，确保 ADC 有足够时间完成转换
        rt_hw_us_delay(delay_us);
    }

    // 6. 打印结果
    rt_kprintf("\n--- Test Results ---\n");
    rt_kprintf("Timer expired.\n");
    rt_kprintf("Total samples: %d\n", adc_sample_count);
    rt_kprintf("Target hits: %d (%.2d.%02d%% hit rate)\n", 
              target_hit_count, 
              (target_hit_count * 10000 / adc_sample_count) / 100,
              (target_hit_count * 10000 / adc_sample_count) % 100);
    rt_kprintf("Actual sample rate: %d.%03d KHz (%d Samples/Second)\n",
              adc_sample_count / 1000, adc_sample_count % 1000, adc_sample_count);
    
    // 7. 与规格对比
    if (adc_sample_count <= 320000) {
        rt_kprintf("[PASS] Sample rate within spec (≤ 320 KS/s)\n");
    } else {
        rt_kprintf("[WARN] Sample rate exceeds spec (> 320 KS/s)\n");
    }
    
    // 8. 目标电压检测结果
    if (target_hit_count > 0) {
        rt_kprintf("[PASS] Target voltage (0.9V) detected %d times\n", target_hit_count);
    } else {
        rt_kprintf("[WARN] Target voltage (0.9V) not detected\n");
    }
    
    rt_kprintf("\n--- END ADC Sample Rate Test ---\n");

    // 9. 清理资源
    rt_timer_delete(rate_test_timer);
    rate_test_timer = RT_NULL;
    rt_adc_disable(adc_dev, ADC_RATE_TEST_CHANNEL);
}

MSH_CMD_EXPORT(adc_rate_test, ADC sample rate test. Usage: adc_rate_test [dev=adc1] [delay_us=4]);
