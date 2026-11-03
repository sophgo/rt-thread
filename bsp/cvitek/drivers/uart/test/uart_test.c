#include <rtdbg.h>
#include <rtdevice.h>
#include <rthw.h>
#include <rtthread.h>

#include "drv_uart.h"
#include "random_data.h"

#define UART_TEST_DEVICE "uart1" /* Test device, custom to your device */
#define TEST_TIMEOUT     RT_TICK_PER_SECOND * 10

static void hex_dump(char *buf, int len, int addr)
{
    int  i, j, k, extra_len;
    char binstr[128];

    extra_len = 0;
    for (i = 0; i < len; i++) {
        if ((i % 16) == 0) {
            extra_len = 0;
            extra_len += sprintf(binstr + extra_len, "%08x -", i + addr);
            extra_len += sprintf(binstr + extra_len, " %02x", (unsigned char)buf[i]);
        } else if ((i % 16) == 15) {
            extra_len += sprintf(binstr + extra_len, " %02x", (unsigned char)buf[i]);
            extra_len += sprintf(binstr + extra_len, "  ");
            for (j = i - 15; j <= i; j++)
                extra_len += sprintf(binstr + extra_len, "%c",
                                     ('!' < buf[j] && buf[j] <= '~') ? buf[j] : '.');
            printf("%s\n", binstr);
        } else {
            extra_len += sprintf(binstr + extra_len, " %02x", (unsigned char)buf[i]);
        }
    }

    if ((i % 16) != 0) {
        k = 16 - (i % 16);
        for (j = 0; j < k; j++) extra_len += sprintf(binstr + extra_len, "   ");
        extra_len += sprintf(binstr + extra_len, "  ");
        k = 16 - k;
        for (j = i - k; j < i; j++)
            extra_len +=
                sprintf(binstr + extra_len, "%c", ('!' < buf[j] && buf[j] <= '~') ? buf[j] : '.');
        printf("%s\n", binstr);
    }
}

enum uart_test_mode { UART_MODE_IRQ, UART_MODE_DMA };

static void show_uart_test_usage(void)
{
    rt_kprintf("Usage: uart_test [mode] [options]\n");
    rt_kprintf("Options:\n");
    rt_kprintf("  irq             - Use interrupt mode (default)\n");
    rt_kprintf("  dma             - Use DMA mode\n");
    rt_kprintf("  usage      -h   - Show usage\n");
    rt_kprintf("  data       -D   - Set data bits (5,6,7,8)\n");
    rt_kprintf("  parity     -P   - Set parity (n-none, o-odd, e-even)\n");
    rt_kprintf("  stop       -S   - Set stop bits (1,2)\n");
    rt_kprintf("  flow       -F   - Set flow control (n-none, a-auto)\n");
    rt_kprintf("  dev name   -N   - Device name to test (default " UART_TEST_DEVICE ")\n");
    rt_kprintf("  boad rate  -R   - Set boad rate\n");
    rt_kprintf("Example:\n");
    rt_kprintf("  uart_test dma -D 8 -P o -S 1 -F a -R 115200\n");
}

int uart_test(int argc, char **argv)
{
    char *recv_buf                   = RT_NULL;
    char  uart_dev_name[RT_NAME_MAX] = UART_TEST_DEVICE;

    enum uart_test_mode test_mode = UART_MODE_DMA;

    rt_size_t   total_recv = 0;
    rt_size_t   send_len   = 0;
    rt_device_t dev        = RT_NULL;
    rt_tick_t   start_time;

    int config_changed = 0;
    int len            = G_RANDOM_DATA_SIZE;
    int ret_code       = -RT_ERROR;

    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (rt_strcmp(argv[i], "dma") == 0) {
            test_mode = UART_MODE_DMA;
        } else if (rt_strcmp(argv[i], "irq") == 0) {
            test_mode = UART_MODE_IRQ;
        } else if (rt_strcmp(argv[i], "-N") == 0 && i + 1 < argc) {
            rt_strncpy(uart_dev_name, argv[++i], RT_NAME_MAX - 1);
            uart_dev_name[RT_NAME_MAX - 1] = '\0';
        } else if (rt_strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            len = atoi(argv[++i]);
            if (len <= 0 || len > G_RANDOM_DATA_SIZE) {
                rt_kprintf("Invalid length %d, using default %d\n", len, G_RANDOM_DATA_SIZE);
                len = G_RANDOM_DATA_SIZE;
            } else if (test_mode == UART_MODE_DMA && len % 64 != 0) {
                rt_kprintf(
                    "Invalid length %d for DMA (requires 64 bytes aligned), using default %d\n",
                    len, G_RANDOM_DATA_SIZE);
                len = G_RANDOM_DATA_SIZE;
            } else {
                config_changed = 1;
            }
        } else if (rt_strcmp(argv[i], "-h") == 0) {
            show_uart_test_usage();
            return 0;
        } else if (rt_strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            config.data_bits = atoi(argv[++i]);
            config_changed   = 1;
        } else if (rt_strcmp(argv[i], "-P") == 0 && i + 1 < argc) {
            i++;
            if (rt_strcmp(argv[i], "n") == 0)
                config.parity = PARITY_NONE;
            else if (rt_strcmp(argv[i], "o") == 0)
                config.parity = PARITY_ODD;
            else if (rt_strcmp(argv[i], "e") == 0)
                config.parity = PARITY_EVEN;
            config_changed = 1;
        } else if (rt_strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            config.stop_bits = atoi(argv[++i]);
            config_changed   = 1;
        } else if (rt_strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
            i++;
            if (rt_strcmp(argv[i], "n") == 0)
                config.flowcontrol = RT_SERIAL_FLOWCONTROL_NONE;
            else if (rt_strcmp(argv[i], "a") == 0)
                config.flowcontrol = RT_SERIAL_FLOWCONTROL_CTSRTS;
            config_changed = 1;
        } else if (rt_strcmp(argv[i], "-R") == 0 && i + 1 < argc) {
            config.baud_rate = atoi(argv[++i]);
            config_changed   = 1;
        }
    }
    /* Print test configuration */
    rt_kprintf("\n--- Starting UART Test ---\n");
    rt_kprintf("Testing UART: %s\n", uart_dev_name);
    rt_kprintf("Test Mode: %s\n", test_mode == UART_MODE_DMA ? "DMA" : "IRQ");
    rt_kprintf("Please short connect the TX and RX pins!\n");

    /* Allocate receive buffer and initialize it to zero */
    recv_buf = rt_malloc_align(len, 64);
    if (recv_buf == RT_NULL) {
        rt_kprintf("[FAIL] malloc recv_buf failed!\n");
        goto FREE_MEM;
    }
    rt_memset(recv_buf, 0, len);

    rt_kprintf("Custom Configuration:\n");
    rt_kprintf("  Data length: %d\n", len);
    rt_kprintf("  Data bits: %d\n", config.data_bits);
    rt_kprintf("  Parity: %s\n", config.parity == PARITY_NONE  ? "none"
                                 : config.parity == PARITY_ODD ? "odd"
                                                               : "even");
    rt_kprintf("  Stop bits: %d\n", config.stop_bits);
    rt_kprintf("  Flow control: %s\n",
               config.flowcontrol == RT_SERIAL_FLOWCONTROL_NONE ? "none" : "auto");
    rt_kprintf("  Baud rate: %d\n", config.baud_rate);

    dev = rt_device_find(uart_dev_name);
    if (!dev) {
        rt_kprintf("[FAIL] Device %s not found!\n", uart_dev_name);
        goto FREE_MEM;
    }

    /* Set open flags based on test mode */
    rt_uint16_t oflags = RT_DEVICE_FLAG_RDWR;
    if (test_mode == UART_MODE_DMA) {
        oflags |= RT_DEVICE_FLAG_DMA_TX | RT_DEVICE_FLAG_DMA_RX;
    } else {
        oflags |= RT_DEVICE_FLAG_INT_RX;
    }

    /* Apply custom configuration if needed */
    if (config_changed) {
        if (rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, &config) != RT_EOK) {
            rt_kprintf("[WARN] Failed to apply custom configuration, using defaults\n");
        }
    }

    /* Open device based on open flags */
    if (rt_device_open(dev, oflags) != RT_EOK) {
        rt_kprintf("[FAIL] Open device %s failed!\n", uart_dev_name);
        goto FREE_MEM;
    }

    if (test_mode == UART_MODE_IRQ) {
        send_len = rt_device_write(dev, 0, g_random_data, len);
        if (send_len != len) {
            rt_kprintf("[FAIL] Send data failed! Only send %d bytes.\n", send_len);
            goto CLOSE_DEV;
        }

        start_time = rt_tick_get();
        total_recv = 0;

        while (total_recv < len) {
            rt_size_t recv_len;
            recv_len = rt_device_read(dev, 0, recv_buf + total_recv, len - total_recv);
            if (recv_len > 0) {
                total_recv += recv_len;
            }

            if (rt_tick_get() - start_time > TEST_TIMEOUT) {
                break;
            }
        }

        if (total_recv != len) {
            rt_kprintf("[FAIL] Receive timeout! Only received %d bytes.\n", total_recv);
            goto CLOSE_DEV;
        }

        if (rt_memcmp(g_random_data, recv_buf, len) == 0) {
            rt_kprintf("[PASS] IRQ-Mode: %d bytes Data verification successful!\n", len);
            ret_code = RT_EOK;
        } else {
            rt_kprintf("[FAIL] Received data does not match!\n");
            hex_dump((char *)g_random_data, len, 0);
            rt_kprintf("recv_buf:\n");
            hex_dump((char *)recv_buf, len, 0);
            goto CLOSE_DEV;
        }
    } else if (test_mode == UART_MODE_DMA) {
        /* enable rx dma first*/
        rt_device_read(dev, 0, recv_buf, len);
        rt_thread_mdelay(10);
        send_len = rt_device_write(dev, 0, g_random_data, len);
        rt_thread_mdelay(10);
        if (send_len != len) {
            rt_kprintf("[FAIL] Send data failed! Only send %d bytes.\n", send_len);
            goto CLOSE_DEV;
        }

        /* wait for data to be received (modify it if needed)*/
        rt_thread_mdelay(500);

        if (rt_memcmp(g_random_data, recv_buf, len) == 0) {
            rt_kprintf("[PASS] DMA-Mode: %d bytes Data verification successful!\n", len);
            ret_code = RT_EOK;
        } else {
            rt_kprintf("[FAIL] Received data does not match!\n");
            hex_dump((char *)g_random_data, len, 0);
            rt_kprintf("recv_buf:\n");
            hex_dump((char *)recv_buf, len, 0);
            goto CLOSE_DEV;
        }
    } else {
        rt_kprintf("[FAIL] Invalid test mode!\n");
    }
CLOSE_DEV:
    rt_device_close(dev);
FREE_MEM:
    rt_free_align(recv_buf);
    rt_kprintf("\n--- END UART Test ---\n");
    return ret_code;
}
MSH_CMD_EXPORT(uart_test, uart_test[mode][options]);