# Cvitek BSP 驱动自测试集

BSP 驱动自测试程序（selftest）随各驱动模块维护在 `drivers/<module>/test/` 下，已集成到 RT-Thread 操作系统中。编译受 Kconfig 选项（`BSP_USING_SELFTEST` + 各 `BSP_TEST_xxx`）控制。看门狗 `watchdog` 命令属于运行时工具，不属于 selftest，位于 `cvi_common/cvi_tool/wdt_tool.c`。eFUSE 命令同样属于工具，位于 `cvi_common/cvi_tool/efuse_tool.c`，由 `BSP_USING_DRIVER_TOOL` + `BSP_USING_EFUSE_TOOL` 控制，支持 `efuse sn/dump/read/write/fastboot/bootfreq`；`efuse fastboot`、`efuse bootfreq` 默认查询，`enable/revert` 及 `write` 须输入 `yes` 确认。`efuse bootfreq enable` 烧录 Bonding0 bit11（CPU 启动频率 750MHz，OTP 不可逆，下次上电生效），开机自动烧录由 `CONFIG_BOOT_FREQ_HIGHER` 控制。

## 目录结构

测试源码随驱动模块维护，位于各模块的 `test/` 子目录：

```
drivers/
├── gpio/test/gpio_test.c         # GPIO 测试
├── spi/test/spi_loopback_test.c  # SPI 回环测试
├── spi/test/spif_test.c          # SPI Flash (FAL) 测试
├── rtc/test/rtc_test.c           # RTC 测试
├── timer/test/timer_test.c       # Timer 测试
├── adc/test/adc_test.c           # ADC 测试
├── pwm/test/pwm_test.c           # PWM 测试
├── tpu/test/tpu_tdma_test.c      # TPU TDMA 测试
├── uart/test/uart_test.c         # UART 测试
├── i2c/test/i2c_test.c           # I2C 测试
├── i2s/test/test_audio_play.c    # I2S 放音测试
├── i2s/test/test_audio_record.c  # I2S 录音测试
├── dma/test/dma_test.c           # DMA 测试
├── eth/test/eth_test.c           # Ethernet 测试
├── sdhci/test/sd_test.c          # SD Card 测试
├── spacc/test/spacc_test.c       # SPACC 测试
├── spacc/test/spacc_dynm_test.c  # SPACC 动态测试
└── pinctrl/test/pinmux_test.c    # PINMUX 测试
```

## 功能特性

### 自测试程序

这些测试程序在使能 `BSP_USING_SELFTEST` 以及特定的测试用例选项后会被编译。

1. **GPIO 测试 (`gpio/test/gpio_test.c`)**
    *   执行 GPIO 功能测试。
    *   **Kconfig 选项:** `BSP_USING_SELFTEST` 和 `BSP_TEST_GPIO`。

2. **SPI 回环测试 (`spi/test/spi_loopback_test.c`)**
    *   在 SPI 接口上执行回环测试。
    *   **Kconfig 选项:** `BSP_USING_SELFTEST` 和 `BSP_TEST_SPI`。

3. **TPU TDMA 测试 (`tpu/test/tpu_tdma_test.c`)**
    *   使用 TDMA 测试 TPU 功能。
    *   **Kconfig 选项:** `BSP_USING_SELFTEST` 和 `BSP_TEST_TPU`。

4. **RTC 测试 (`rtc/test/rtc_test.c`)**
    *   测试 RTC 功能。
    *   **Kconfig 选项:**  `BSP_USING_RTC` & `RT_USING_ALARM` & `BSP_USING_SELFTEST` & `BSP_TEST_RTC` 。

5. **TIMER 测试 (`timer/test/timer_test.c`)**
    *   测试 TIMER 功能。
    *   **Kconfig 选项:**  `BSP_USING_TIMER` & `BSP_USING_SELFTEST` & `BSP_TEST_TIMER` 。

6. **ADC 测试 (`adc/test/adc_test.c`)**
    *   测试 ADC 功能。
    *   **Kconfig 选项:**  `BSP_USING_ADC` & `BSP_USING_SELFTEST` & `BSP_TEST_ADC` 。
    *   ADC chip 以及 ADC 通道需要再代码中自行修改。

7. **PWM 测试 (`pwm/test/pwm_test.c`)**
    *   测试 PWM 功能。
    *   **Kconfig 选项:**  `BSP_USING_PWM` & `BSP_USING_SELFTEST` & `BSP_TEST_PWM` 。
    *   PWM chip 以及 PWM 通道需要再代码中自行修改

8. **SD Card 测试 (`sdhci/test/sd_test.c`)**
    *   测试 SD Card 功能。
    *   **Kconfig 选项:**  `BSP_USING_SDH` & `BSP_USING_SELFTEST` & `BSP_TEST_SD` 。
    *   上电前插入SD Card，板子启动时自动检测SD Card 并挂载。测试时 执行 sd_card_test 命令行测试基本读写
    *   执行 sd_card_test perf 测试读写速率。

9. **Ethernet 测试 (`eth/test/eth_test.c`)**
    *   测试 Ethernet 功能。
    *   **Kconfig 选项:**  `BSP_USING_ETH` & `BSP_USING_SELFTEST` & `BSP_TEST_ETH` 。
    *   使用网线连接板端与PC，执行 eth_test

10. **I2S 放音/录音测试 (`i2s/test/test_audio_play.c` / `i2s/test/test_audio_record.c`)**
    *   通过 i2s 音频设备（i2s0 录音 / i2s3 放音）读写 PCM 文件。
    *   **Kconfig 选项:**  `BSP_USING_AUDIO` & `BSP_USING_SELFTEST` & `BSP_TEST_AUDIO` 。
    *   执行 audio_play 8000 2 160 play.raw / audio_record 8000 2 160 record.pcm 10。

## 构建说明

本目录中的组件作为 RT-Thread 主构建过程的一部分进行构建。请通过 Kconfig (例如，使用 `menuconfig`) 来使能所需的功能。

1.  根据需要在 `ENABLE SELFTEST`(`BSP_USING_SELFTEST`) 部分下使能特定的测试case。例如：
    *   要使能 GPIO 测试：使能 `Enable GPIO_SELFTEST` (`BSP_TEST_GPIO`)。
    *   要使能 SPI 测试：使能 `Enable SPI_SELFTEST` (`BSP_TEST_SPI`)。
    *   要使能 RTC 测试：使能 `Enable RTC_SELFTEST` (`BSP_TEST_RTC`)。
    *   要使能 TIMER 测试：使能 `Enable TIMER_SELFTEST` (`BSP_TEST_TIMER`)。
    *   要使能 ADC 测试：使能 `Enable ADC_SELFTEST` (`BSP_TEST_ADC`)。
    *   要使能 PWM 测试：使能 `Enable PWM_SELFTEST` (`BSP_TEST_PWM`)。
    *   要使能 SD Card 测试：使能 `Enable SD_SELFTEST` (`BSP_TEST_SD`)。
    *   要使能 ETH 测试：使能 `Enable ETH_SELFTEST` (`BSP_TEST_ETH`)。
    *   要使能 I2S 音频测试：使能 `Enable AUDIO_SELFTEST` (`BSP_TEST_AUDIO`)。

## 使用方法

编译并烧录到目标设备后，可以通过 MSH shell 调用这些工具。例如：

```msh
msh />gpio_test
msh />spi_loopback_test
msh />rtc_test
msh />timer_test
msh />adc_test
msh />pwm_test 1000000 500000 10
msh />sd_card_test
msh />sd_card_test perf
msh />eth_test
msh />audio_play 8000 2 160 play.raw
msh />audio_record 8000 2 160 record.pcm 10
```
