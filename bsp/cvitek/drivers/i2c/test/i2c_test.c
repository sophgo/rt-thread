#include <board.h>
#include <drivers/dev_pin.h>
#include <drv_gpio.h>
#include <drv_pinmux.h>
#include <mmio.h>
#include <rtthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drv_hw_i2c.h"
#include "pinctrl.h"

typedef struct {
    dw_iic_regs_t *base;
    rt_uint8_t    *slave_bus;
} i2c_intr_args_t;

int slave_pos;

// This irq handler is used to test
static void dw_i2c_irq_handler(int irqno, void *param)
{
    uint32_t intr_stat    = ((i2c_intr_args_t *)param)->base->IC_INTR_STAT;
    uint32_t rawintr_stat = ((i2c_intr_args_t *)param)->base->IC_RAW_INTR_STAT;

    if (intr_stat & DW_IIC_INTR_RX_FULL) {
        int tmp = ((i2c_intr_args_t *)param)->base->IC_DATA_CMD;

        ((i2c_intr_args_t *)param)->slave_bus[slave_pos] = tmp;
        slave_pos++;
    }

    if (intr_stat & DW_IIC_INTR_RD_REQ) {
        // fill data
        if (((i2c_intr_args_t *)param)->base->IC_RAW_INTR_STAT & DW_IIC_INTR_TX_ABRT) {
            rt_kprintf("DW_IIC_INTR_TX_ABRT, source:0x%x\n",
                       ((i2c_intr_args_t *)param)->base->IC_TX_ABRT_SOURCE);
            ((i2c_intr_args_t *)param)->base->IC_CLR_TX_ABRT;
        }
        if (((i2c_intr_args_t *)param)->base->IC_STATUS & DW_IIC_TXFIFO_NOT_FULL_STATE) {
            ((i2c_intr_args_t *)param)->base->IC_DATA_CMD =
                ((i2c_intr_args_t *)param)->slave_bus[slave_pos];
            slave_pos++;
        } else {
            rt_kprintf("tx full\n");
        }
        dw_iic_clear_rd_req_irq(((i2c_intr_args_t *)param)->base);
    }

    if (intr_stat & DW_IIC_INTR_STOP_DET) {
        ((i2c_intr_args_t *)param)->base->IC_CLR_STOP_DET;
        slave_pos = 0;
    }

    if (intr_stat & DW_IIC_INTR_START_DET) {
        ((i2c_intr_args_t *)param)->base->IC_CLR_START_DET;
        slave_pos = 0;
    }
}

void dump_hex(const void *addr, size_t size)
{
    const uint8_t *p      = (const uint8_t *)addr;
    size_t         offset = 0;

    while (offset < size) {
        size_t line = size - offset;

        if (line > 16) line = 16;
        rt_kprintf("===%lx:\n", offset);
        for (size_t i = 0; i < 16; ++i) {
            if (i < line - 1) {
                rt_kprintf("%x ", p[offset + i]);
            } else {
                rt_kprintf("%x \n", p[offset + i]);
            }
        }
        offset += line;
    }
}

static void i2c_slave_mode(u8 slave_iic, rt_uint8_t slave_id, size_t size)
{
    i2c_intr_args_t args;

    struct dw_iic_bus *slave_obj = i2c_bus_get_by_num(slave_iic);
    if (!slave_obj) {
        rt_kprintf("[FAIL] I2C slave bus %d not found!\n", slave_iic);
        return -1;
    }

    rt_uint8_t *slave_buf = rt_malloc(size);

    if (!slave_buf) {
        rt_kprintf("[FAIL] Memory allocation failed!\n");
        return -1;
    }

    dw_iic_slave_init(slave_obj->iic_base, slave_id);
    args.base      = slave_obj->iic_base;
    args.slave_bus = slave_buf;
    rt_hw_interrupt_install(slave_obj->irq, dw_i2c_irq_handler, &args, "i2c");
    rt_hw_interrupt_umask(slave_obj->irq);
    dw_iic_slave_enable_transmit_irq(slave_obj->iic_base);
    slave_obj->iic_base->IC_INTR_MASK |= DW_IIC_INTR_RX_FULL | DW_IIC_INTR_START_DET;

    rt_kprintf("Enter slave mode, slave iic: IIC%d, slave id:0x%x\n", slave_iic, slave_id);
    while (1) {
        rt_thread_mdelay(1000);
    }
    if (slave_buf) rt_free(slave_buf);
}

static int i2c_no_ack_test(u8 master_iic, rt_uint8_t slave_id)
{
    int                       ret = 0, tmp = 0;
    struct rt_i2c_msg         msgs[1];
    int                       size  = 0x10;
    rt_uint8_t               *r_buf = rt_malloc(size);
    struct rt_i2c_bus_device *bus;

    struct dw_iic_bus *master_obj = i2c_bus_get_by_num(master_iic);
    if (!master_obj) {
        rt_kprintf("[FAIL] I2C master bus %d not found!\n", master_iic);
        return -1;
    }

    dw_iic_init(master_obj->iic_base, 100000);

    bus = rt_i2c_bus_device_find(i2c_idx_to_name(master_iic));
    if (!bus) {
        rt_kprintf("----%d bus no found---\n", master_iic);
        ret = -1;
        goto exit;
    }
    msgs[0].addr  = slave_id;
    msgs[0].flags = RT_I2C_RD | RT_I2C_IGNORE_NACK;
    msgs[0].buf   = r_buf;
    msgs[0].len   = size;

    tmp = rt_i2c_transfer(bus, msgs, 1);
    if (tmp == -RT_EIO) {
        ret = 0;
    } else if (tmp > 0) {
        rt_kprintf("Current slave id: 0x%x, got ack.\n", slave_id);
        rt_kprintf(
            "This is 'No ack' test, please use an I2C slave ID that is currently not connected.\n");
        ret = -1;
    } else {
        rt_kprintf("ret: %d\n", tmp);
        ret = -1;
    }

exit:
    if (r_buf) rt_free(r_buf);
    return ret;
}

static int random_range(int min, int max)
{
    static rt_uint32_t seed;

    if (seed == 0) {
        seed = rt_tick_get();
    }
    return (rand_r(&seed) % (max - min + 1)) + min;
}

// iic recovery
static void iic0_config_iic_mode(void)
{
    PINMUX_CONFIG(IIC0_SCL, IIC0_SCL);
    PINMUX_CONFIG(IIC0_SDA, IIC0_SDA);
}

static void iic0_config_gpio_mode(void)
{
    PINMUX_CONFIG(IIC0_SCL, XGPIOA_28);
    PINMUX_CONFIG(IIC0_SDA, XGPIOA_29);
}

static int i2c_recovery_test(u8 master_iic)
{
    // Init config
    iic_recovery_config_t config = {
        .enable           = 0,
        .scl_gpio_grp     = 'A',
        .scl_gpio_num     = 28,
        .sda_gpio_grp     = 'A',
        .sda_gpio_num     = 29,
        .config_gpio_mode = iic0_config_gpio_mode,
        .config_iic_mode  = iic0_config_iic_mode,
    };

    iic_set_recovery_config(master_iic, &config);

    iic_recovery_config_t check_config;
    iic_get_recovery_config(master_iic, &check_config);

    rt_kprintf(
        "Get config: enable:%d, scl_gpio_grp:%c, scl_gpio_num:%d, sda_gpio_grp:%c, "
        "sda_gpio_num:%d\n",
        check_config.enable, check_config.scl_gpio_grp, check_config.scl_gpio_num,
        check_config.sda_gpio_grp, check_config.sda_gpio_num);
    iic_recovery_enable(master_iic, 1);

    rt_kprintf("iic_check enable: %d\n", iic_recovery_is_enabled(master_iic));

    struct dw_iic_bus *master_obj = i2c_bus_get_by_num(master_iic);
    if (!master_obj) {
        rt_kprintf("[FAIL] I2C master bus %d not found!\n", master_iic);
        return -1;
    }
    rt_kprintf("Ready to recover I2C bus %d..., please use LA/Scope to check SCL/SDA line\n",
               master_iic);
    dw_iic_recover_bus(master_obj->iic_base);
    return 0;
}

static int i2c_get_test(u8 master_iic, u8 slave_id)
{
    rt_kprintf("loop i2cget test:\n");
    char cmd[128] = {0};
    u8   cnt      = 50;
    while (cnt--) {
        rt_snprintf(cmd, sizeof(cmd), "i2cget %d %d 10", master_iic, slave_id);
        rt_kprintf("> %s\n", cmd);
        msh_exec(cmd, rt_strlen(cmd));
        rt_thread_mdelay(1000);
    }
    return 0;
}
static int i2c_loopback_test(u8 master_iic, u8 slave_iic, int size, rt_uint32_t rate,
                             rt_uint8_t slave_id, rt_uint16_t cycles)
{
    int             ret = 0;
    i2c_intr_args_t args;
    rt_uint8_t     *w_buf     = NULL;
    rt_uint8_t     *r_buf     = NULL;
    rt_uint8_t     *slave_buf = NULL;
    rt_uint32_t     test_size;
    rt_uint16_t     pass_cnt = 0;

    struct dw_iic_bus *master_obj = i2c_bus_get_by_num(master_iic);
    if (!master_obj) {
        rt_kprintf("[FAIL] I2C master bus %d not found!\n", master_iic);
        return -1;
    }
    struct dw_iic_bus *slave_obj = i2c_bus_get_by_num(slave_iic);
    if (!slave_obj) {
        rt_kprintf("[FAIL] I2C slave bus %d not found!\n", slave_iic);
        return -1;
    }

    if (size > 0) {
        w_buf     = rt_malloc(size);
        r_buf     = rt_malloc(size);
        slave_buf = rt_malloc(size);
    } else {  // for 0 size test
        w_buf     = rt_malloc(1);
        r_buf     = rt_malloc(1);
        slave_buf = rt_malloc(1);
    }

    if (!w_buf || !r_buf || !slave_buf) {
        rt_kprintf("[FAIL] Memory allocation failed!\n");
        if (w_buf) rt_free(w_buf);
        if (r_buf) rt_free(r_buf);
        if (slave_buf) rt_free(slave_buf);
        return -1;
    }
    rt_kprintf("size:%d\n", size);

    dw_iic_init(master_obj->iic_base, rate ? 400000 : 100000);
    dw_iic_slave_init(slave_obj->iic_base, slave_id);

    args.base      = slave_obj->iic_base;
    args.slave_bus = slave_buf;

    rt_hw_interrupt_install(slave_obj->irq, dw_i2c_irq_handler, &args, "i2c");
    rt_hw_interrupt_umask(slave_obj->irq);
    dw_iic_slave_enable_transmit_irq(slave_obj->iic_base);
    slave_obj->iic_base->IC_INTR_MASK |= DW_IIC_INTR_RX_FULL | DW_IIC_INTR_START_DET;

    struct rt_i2c_bus_device *bus;
    struct rt_i2c_msg         msgs[2];

    bus = rt_i2c_bus_device_find(i2c_idx_to_name(master_iic));
    if (!bus) {
        rt_kprintf("----%d bus no found---\n", master_iic);
        ret = -1;
        goto err;
    }

    for (int i = 0; i < cycles; i++) {
        rt_memset(r_buf, 0, size);
        if (cycles > 1)
            test_size = random_range(1, size);
        else
            test_size = size;
        rt_kprintf("--- round [%d/%d] start, test_size: %d ---\n", i + 1, cycles, test_size);
        for (int i = 0; i < test_size; i++) w_buf[i] = i;

        // test write and then read (using restart)
        msgs[0].addr  = slave_id;
        msgs[0].flags = RT_I2C_WR;
        msgs[0].buf   = w_buf;
        msgs[0].len   = test_size;

        msgs[1].addr  = slave_id;
        msgs[1].flags = RT_I2C_RD;
        msgs[1].buf   = r_buf;
        msgs[1].len   = test_size;

        rt_i2c_transfer(bus, msgs, 2);

        if (!memcmp(r_buf, w_buf, test_size)) {
            rt_kprintf("[Pass]: the write data is same as read!\n");
            pass_cnt++;
        } else {
            rt_kprintf("[Failed]: write and read not the same!!!!\n");
            rt_kprintf("rbuf:\n");
            dump_hex(r_buf, test_size > 200 ? 200 : test_size);
            ret = -1;
        }
        rt_kprintf("--- round [%d]/[%d] end ---\n\n", i + 1, cycles);
    }
    rt_kprintf("loop-back end. Total passed: [%d/%d] [%s]\n\n", pass_cnt, cycles,
               pass_cnt == cycles ? "Pass" : "Failed");
    rt_kprintf("dump slave status:\n");
    dump_iic_status(slave_obj->iic_base);

err:
    rt_hw_interrupt_mask(slave_obj->irq);
    dw_iic_disable(slave_obj->iic_base);
    dw_iic_clear_all_irq(slave_obj->iic_base);
    // Test finished, set current slave iic to master mode
    dw_iic_init(slave_obj->iic_base, 400000);
    if (w_buf) rt_free(w_buf);
    if (r_buf) rt_free(r_buf);
    if (slave_buf) rt_free(slave_buf);
    return ret;
}

int i2c_test(int argc, char **argv)
{
    u8          slave_id   = 0x50;              // default: 0x50
    u8          master_iic = 0, slave_iic = 1;  // default: master:iic0 slave:iic1
    int         size        = 30;               // default
    u8          is_test_all = 0, is_slave_mode = 0, is_test_recovery = 0, is_i2c_get_test = 0;
    u8          rate   = 0;  // default: 100k
    int         ret    = 0;
    rt_uint16_t cycles = 1;

    rt_kprintf("--- IIC_TEST ---\n");

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];

        if (strcmp(a, "-a") == 0) {
            is_test_all = 1;
            continue;
        }

        if (strcmp(a, "-l") == 0) {
            if (i + 1 >= argc) {
                rt_kprintf("Error: -l requires a value(byte)\n");
                goto Usage;
            }
            char *end = NULL;

            size = strtol(argv[++i], &end, 10);
            if (*end != '\0' || size < 0) {
                rt_kprintf("Error: -l requires a numeric-only and positive parameter\n");
                goto Usage;
            }
            continue;
        }

        if (strcmp(a, "-m") == 0) {
            if (i + 1 >= argc) {
                rt_kprintf("Error: -m(master) requires a id: iic[x]\n");
                goto Usage;
            }
            char *end = NULL;

            int tmp = strtol(argv[++i], &end, 10);
            if (*end != '\0' || tmp < 0) {
                rt_kprintf("Error: -m(master) requires a positive id: iic[x]\n");
                goto Usage;
            }
            master_iic = tmp;
            continue;
        }

        if (strcmp(a, "-s") == 0) {
            if (i + 1 >= argc) {
                rt_kprintf("Error: -s(slave) requires a id: iic[x]\n");
                goto Usage;
            }
            char *end = NULL;

            int tmp = strtol(argv[++i], &end, 10);
            if (*end != '\0' || tmp < 0) {
                rt_kprintf("Error: -s(slave) requires a positive id: iic[x]\n");
                goto Usage;
            }
            slave_iic = tmp;
            continue;
        }

        if (strcmp(a, "-r") == 0) {
            if (i + 1 >= argc) {
                rt_kprintf("Error: -r(rate) requires a value: 0: 100k 1:400k\n");
                goto Usage;
            }
            char *end = NULL;

            int tmp = strtol(argv[++i], &end, 10);
            if (*end != '\0' || tmp < 0 || tmp > 1) {
                rt_kprintf("Error: -r(rate) requires a value: 0: 100k 1:400k\n");
                goto Usage;
            }
            rate = tmp;
            continue;
        }

        if (strcmp(a, "-id") == 0) {
            if (i + 1 >= argc) {
                rt_kprintf("Error: -id(slave) requires a value\n");
                goto Usage;
            }
            char *end = NULL;

            int tmp = strtol(argv[++i], &end, 10);
            if (*end != '\0' || tmp > 0x77 || tmp < 0x8) {
                rt_kprintf("Error: -id(slave) requires a value 0x8~0x77\n");
                goto Usage;
            }
            slave_id = tmp;
            continue;
        }

        if (strcmp(a, "-ss") == 0) {
            is_slave_mode = 1;
            continue;
        }

        if (strcmp(a, "-re") == 0) {
            is_test_recovery = 1;
            continue;
        }

        if (strcmp(a, "-ge") == 0) {
            is_i2c_get_test = 1;
            continue;
        }

        if (strcmp(a, "-c") == 0) {
            if (i + 1 >= argc) {
                rt_kprintf("Error: -c(cycles) requires a value: test cycles\n");
                goto Usage;
            }
            char *end = NULL;
            int   tmp = strtol(argv[++i], &end, 10);

            if (*end != '\0' || tmp < 0 || tmp > 65536) {
                rt_kprintf("Error: -c(cycles) requires a value: 1~65535\n");
                goto Usage;
            }
            cycles = tmp;
            continue;
        }

        rt_kprintf("Error: unknown option: %s\n", a);
        goto Usage;
    }

    if (master_iic == slave_iic) {
        rt_kprintf("Error: master(0x%x) and slave(0x%x) can not be the same\n", master_iic,
                   slave_iic);
        goto Usage;
    }

    rt_kprintf("master_iic:%d, slave_iic:%d, size:%d(byte), rate:%d(k), slave_id:0x%x, cycles:%d\n",
               master_iic, slave_iic, size, rate ? 400 : 100, slave_id, cycles);
    rt_kprintf(
        "***1. Ensure that the IIC you intend to use is configured in menuconfig***\n***2. Connect "
        "the master and slave with wires***\n");

    if (is_slave_mode) {
        i2c_slave_mode(slave_iic, slave_id, size);
        return 0; /* slave mode runs until interrupted; treat start-up as done */
    }

    if (is_i2c_get_test) {
        return i2c_get_test(master_iic, slave_id);
    }

    if (is_test_recovery) {
        return i2c_recovery_test(master_iic);
    }

    if (!is_test_all) {
        return i2c_loopback_test(master_iic, slave_iic, size, rate, slave_id, cycles);
    } else if (is_test_all) {
        rt_kprintf("==== All Test Start: ====\n");
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[1]no ack test start:\n");
        ret = i2c_no_ack_test(master_iic, slave_id);
        rt_kprintf("[1]no ack test %s\n", ret ? "failed" : "passed");
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[2]0 size test start:\n");
        rt_kprintf(
            "[2]0 size test %s\n",
            (ret = i2c_loopback_test(master_iic, slave_iic, 0, rate, slave_id, 1)) ? "failed" : "passed");
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[3]100k loop 256B test start\n");
        rt_kprintf(
            "[3]100k loop 256B test %s\n",
            (ret = i2c_loopback_test(master_iic, slave_iic, 256, 0, slave_id, 1)) ? "failed" : "passed");
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[4]400k loop 256B test start\n");
        rt_kprintf(
            "[4]400k loop 256B test %s\n",
            (ret = i2c_loopback_test(master_iic, slave_iic, 256, 1, slave_id, 1)) ? "failed" : "passed");
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[5]400k loop 1MB test (Bulk Data) start\n");
        rt_kprintf("[5]400k loop 1MB test (Bulk Data)  %s\n",
                   (ret = i2c_loopback_test(master_iic, slave_iic, 1024 * 1024, 1, slave_id, 1))
                       ? "failed"
                       : "passed");
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[6]400k long time test start\n");
        rt_kprintf(
            "[6]400k long time test %s\n",
            (ret = i2c_loopback_test(master_iic, slave_iic, 4096, 1, slave_id, cycles > 1 ? cycles : 200))
                ? "failed"
                : "passed");
        rt_kprintf("------------------------------------\n");
        rt_kprintf("==== All Test Finished ====\n");
        return ret;
    }

    return 0;

Usage:
    rt_kprintf("Usage:\n");
    rt_kprintf("iic test:\n");
    rt_kprintf("  General settings: applicable to all cases\n");
    rt_kprintf("	-m: set master iic[x] (default: iic0)\n");
    rt_kprintf("	-s: set slave iic[x] (default: iic1)\n");
    rt_kprintf("	-c: set test cycles: loopback cycles\n");
    rt_kprintf(
        "	    (when -c is set, the data size is a random value between 1 and size(-l).)\n");
    rt_kprintf("	-id: set slave id: 0x8~0x77 (default: 0x50)\n");
    rt_kprintf("	-a: start all test (without -a, it is a loopback test)\n");
    rt_kprintf("  Only effective for loopback tests:\n");
    rt_kprintf("	-r: rate: 0: 100k, 1: 400k (default: 100k)\n");
    rt_kprintf("	-l: size: length of loopback data (default: 30 Byte)\n");
    rt_kprintf("  Other modes:\n");
    rt_kprintf("	-ge: Enter i2cget test\n");
    rt_kprintf("	-re: Enter i2c bus recovery test\n");
    rt_kprintf(
        "	-ss: Enable the I2C as a slave device, with the current device running in slave "
        "mode.\n");
    rt_kprintf("  e.g. i2c_test -m 0 -s 1 -c 100 -id 0x50 -r 0 -l 100\n");
    rt_kprintf(
        "  which means master: iic0, slave: iic1, loopback cycles: 100, slave id:0x50, rate: "
        "100kHz, data len: 100 Byte\n");
    return 1;
}

MSH_CMD_EXPORT(i2c_test, I2C test);
