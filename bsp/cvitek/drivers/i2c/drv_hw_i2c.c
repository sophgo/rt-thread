/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-02-14     ShichengChu  first version
 */
#include "drv_hw_i2c.h"

#include <board.h>
#include <rtdevice.h>

#define DBG_TAG "drv.i2c"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define DW_IIC_DEFAULT_SPEED     400000
#define MAX_IIC_RECOVERY_CONFIGS 5
// TODO: support pwr_iic
static iic_recovery_config_t iic_recovery_configs[MAX_IIC_RECOVERY_CONFIGS];

static struct dw_iic_bus _i2c_obj[] = {
#ifdef BSP_USING_I2C0
    {
        .iic_base    = (dw_iic_regs_t*)I2C0_BASE,
        .device_name = "i2c0",
        .irq         = I2C0_IRQ,
    },
#endif /* BSP_USING_I2C0 */
#ifdef BSP_USING_I2C1
    {
        .iic_base    = (dw_iic_regs_t*)I2C1_BASE,
        .device_name = "i2c1",
        .irq         = I2C1_IRQ,
    },
#endif /* BSP_USING_I2C1 */
#ifdef BSP_USING_I2C2
    {
        .iic_base    = (dw_iic_regs_t*)I2C2_BASE,
        .device_name = "i2c2",
        .irq         = I2C2_IRQ,
    },
#endif /* BSP_USING_I2C2 */
#ifdef BSP_USING_I2C3
    {
        .iic_base    = (dw_iic_regs_t*)I2C3_BASE,
        .device_name = "i2c3",
        .irq         = I2C3_IRQ,
    },
#endif /* BSP_USING_I2C3 */
#ifdef BSP_USING_I2C4
    {
        .iic_base    = (dw_iic_regs_t*)I2C4_BASE,
        .device_name = "i2c4",
        .irq         = I2C4_IRQ,
    },
#endif /* BSP_USING_I2C4 */
};

static size_t i2c_bus_internal_count(void)
{
    return sizeof(_i2c_obj) / sizeof(_i2c_obj[0]);
}

const struct dw_iic_bus* i2c_bus_find_by_name(const char* name)
{
    if (!name) return NULL;
    size_t n = i2c_bus_internal_count();
    for (size_t i = 0; i < n; ++i) {
        const char* dev = _i2c_obj[i].device_name;
        if (dev && strcmp(dev, name) == 0) {
            return &_i2c_obj[i];
        }
    }
    return NULL;
}

const struct dw_iic_bus* i2c_bus_get_by_num(uint8_t id)
{
    char name[5];
    int  len = snprintf(name, sizeof(name), "i2c%u", (unsigned)id);
    if (len <= 0 || (size_t)len >= sizeof(name)) {
        return NULL;
    }
    return i2c_bus_find_by_name(name);
}

int concat_char_int(char c, int n, char* buf, size_t buf_size)
{
    if (buf_size == 0) return -1;
    int written = snprintf(buf, buf_size, "%c%d", c, n);
    if (written < 0 || (size_t)written >= buf_size) {
        return -1;
    }
    return 0;
}

/* Internal function to find recovery configuration by IIC index */
static iic_recovery_config_t* _get_recovery_config(uint8_t iic_idx)
{
    if (iic_idx >= MAX_IIC_RECOVERY_CONFIGS) {
        return NULL;
    }

    /* Check if this index is configured and enabled */
    if (iic_recovery_configs[iic_idx].enable) {
        return &iic_recovery_configs[iic_idx];
    }

    return NULL;
}

/* Public API implementations */
int iic_set_recovery_config(uint8_t iic_idx, const iic_recovery_config_t* config)
{
    if (!config || iic_idx >= MAX_IIC_RECOVERY_CONFIGS) {
        return -1;
    }

    /* Copy configuration directly to the corresponding index */
    if (memcmp(&iic_recovery_configs[iic_idx], config, sizeof(iic_recovery_config_t)) != 0) {
        memcpy(&iic_recovery_configs[iic_idx], config, sizeof(iic_recovery_config_t));
        // printf("IIC%d recovery config %s\r\n", iic_idx, config->enable ? "enabled" : "disabled");
    }

    return 0;
}

int iic_get_recovery_config(uint8_t iic_idx, iic_recovery_config_t* config)
{
    if (!config || iic_idx >= MAX_IIC_RECOVERY_CONFIGS) {
        return -1;
    }

    /* Check if this index has been configured (non-zero structure) */
    if (iic_recovery_configs[iic_idx].config_gpio_mode != NULL &&
        iic_recovery_configs[iic_idx].config_iic_mode != NULL) {
        memcpy(config, &iic_recovery_configs[iic_idx], sizeof(iic_recovery_config_t));
        return 0;
    }

    return -1; /* Config not found */
}

int iic_recovery_enable(uint8_t iic_idx, uint8_t enable)
{
    if (iic_idx >= MAX_IIC_RECOVERY_CONFIGS) {
        return -1;
    }

    /* Check if this index has been configured */
    if (iic_recovery_configs[iic_idx].config_gpio_mode != NULL &&
        iic_recovery_configs[iic_idx].config_iic_mode != NULL) {
        iic_recovery_configs[iic_idx].enable = enable ? 1 : 0;
        printf("IIC%d recovery %s\r\n", iic_idx, enable ? "enabled" : "disabled");
        return 0;
    }

    return -1; /* Config not found */
}

int iic_recovery_is_enabled(uint8_t iic_idx)
{
    if (iic_idx >= MAX_IIC_RECOVERY_CONFIGS) {
        return -1;
    }

    /* Check if this index has been configured */
    if (iic_recovery_configs[iic_idx].config_gpio_mode != NULL &&
        iic_recovery_configs[iic_idx].config_iic_mode != NULL) {
        return iic_recovery_configs[iic_idx].enable ? 1 : 0;
    }

    return 0; /* Not configured, treated as disabled */
}

void _gpio_set_val(char gpio_grp, uint8_t gpio_num, uint8_t level)
{
    rt_ssize_t ret;

    char gpio_name[4];

    if (concat_char_int(gpio_grp, gpio_num, gpio_name, sizeof(gpio_name)) != 0) {
        printf("iic_recovery gpio_name set failed\r\n");
        return;
    }

    rt_base_t pin = rt_pin_get(gpio_name);
    if (pin < 0) {
        printf("iic_recovery gpio_name set failed\r\n");
        return;
    }

    if (level == 1) {
        // OD output high level
        rt_pin_mode(pin, PIN_MODE_INPUT);
        rt_pin_write(pin, level);
    } else {
        // OD output low level
        rt_pin_mode(pin, PIN_MODE_OUTPUT);
        rt_pin_write(pin, level);
    }
}

int32_t _gpio_get_val(uint8_t gpio_grp, uint8_t gpio_num)
{
    rt_ssize_t ret;
    char       gpio_name[4];
    uint32_t   level = 0;

    if (concat_char_int(gpio_grp, gpio_num, gpio_name, sizeof(gpio_name)) != 0) {
        printf("iic_recovery gpio_name get failed\r\n");
        return;
    }

    rt_base_t pin = rt_pin_get(gpio_name);
    if (pin < 0) {
        printf("iic_recovery gpio_name set failed\r\n");
        return;
    }

    // gpio read
    rt_pin_mode(pin, PIN_MODE_INPUT);
    level = rt_pin_read(pin);
    return level;
}

#define RECOVERY_UDELAY  20
#define RECOVERY_CLK_CNT 9

/* Dynamic GPIO operations based on specific IIC configuration */
static inline void _set_scl_by_config(iic_recovery_config_t* config, uint8_t val)
{
    if (config) {
        _gpio_set_val(config->scl_gpio_grp, config->scl_gpio_num, val);
    }
}

static inline void _set_sda_by_config(iic_recovery_config_t* config, uint8_t val)
{
    if (config) {
        _gpio_set_val(config->sda_gpio_grp, config->sda_gpio_num, val);
    }
}

static inline int32_t _get_scl_by_config(iic_recovery_config_t* config)
{
    if (config) {
        return _gpio_get_val(config->scl_gpio_grp, config->scl_gpio_num);
    }
    return -1;
}

static inline int32_t _get_sda_by_config(iic_recovery_config_t* config)
{
    if (config) {
        return _gpio_get_val(config->sda_gpio_grp, config->sda_gpio_num);
    }
    return -1;
}

static inline int8_t get_idx_by_base(dw_iic_regs_t* iic_base)
{
    int8_t iic_idx = -1;
    iic_idx        = ((uint32_t)iic_base - I2C0_BASE) / (I2C1_BASE - I2C0_BASE);

    return iic_idx;
}
/* Macros for GPIO operations with specific config */
#define set_scl(config, val) _set_scl_by_config(config, val)
#define set_sda(config, val) _set_sda_by_config(config, val)
#define get_scl(config)      _get_scl_by_config(config)
#define get_sda(config)      _get_sda_by_config(config)
/**
  \brief       recover bus when busy: csi_iic_uninit-->pinmux-->io-->soft
  reset-->pinmux->csi_iic_init \param[in]   iic            handle to operate. \return      0 if
  success, -1 if failed
*/
int dw_iic_recover_bus(dw_iic_regs_t* iic_base)
{
    rt_int32_t             i                    = 0;
    rt_uint32_t            i2c_soft_rst_reg_val = 0;
    rt_uint8_t             iic_idx              = get_idx_by_base(iic_base);
    struct dw_iic_bus*     i2c_obj              = i2c_bus_get_by_num(iic_idx);
    iic_recovery_config_t* config               = _get_recovery_config(iic_idx);
    rt_uint32_t            ori_speed            = dw_iic_get_speed(iic_base);

    /* Check if this IIC index supports recovery */
    if (!config) {
        printf("i2c_recover_bus: IIC%d recovery not configured or disabled\r\n", iic_idx);
        return -1;
    }

    printf("Starting IIC%d bus recovery...\r\n", iic_idx);

    // Same recovery logic as before...
    /* prepare_recovery */
    dw_iic_clear_all_irq(iic_base);
    dw_iic_disable_all_irq(iic_base);
    dw_iic_disable(iic_base);
    rt_hw_interrupt_mask(i2c_obj->irq);
    // rt_hw_interrupt_detach(i2c_obj->irq);

    /* Configure pins as GPIO using platform configuration */
    if (config->config_gpio_mode) {
        config->config_gpio_mode();
    }
    printf("IIC%d initial state -> scl:%d sda:%d\r\n", iic_idx, get_scl(config), get_sda(config));

    // Step 1: Ensure SCL is high
    set_scl(config, 1);
    rt_hw_us_delay(RECOVERY_UDELAY / 2);
    set_sda(config, 1);
    rt_hw_us_delay(RECOVERY_UDELAY);

    // Step 2: Generate 9 clock pulses
    printf("Generating 9 clock pulses on SCL...\r\n");
    for (i = 0; i < RECOVERY_CLK_CNT; i++) {
        if (get_scl(config) == 0) {
            printf("SCL is stuck low, cannot recover.\n");
            if (config->config_iic_mode) {
                config->config_iic_mode();
            }
            return -1;
        }

        set_scl(config, 0);
        rt_hw_us_delay(RECOVERY_UDELAY);
        set_scl(config, 1);
        rt_hw_us_delay(RECOVERY_UDELAY);

        if (get_sda(config) == 1) {
            printf("SDA released after %d clock pulses\r\n", i + 1);
            break;
        }
    }

    // Step 3: Generate STOP condition
    printf("Generating a STOP condition...\r\n");
    set_scl(config, 1);
    rt_hw_us_delay(RECOVERY_UDELAY / 2);
    set_sda(config, 0);
    rt_hw_us_delay(RECOVERY_UDELAY);
    set_sda(config, 1);
    rt_hw_us_delay(RECOVERY_UDELAY);

    printf("Recovery sequence finished. Final state -> scl:%d sda:%d\r\n", get_scl(config),
           get_sda(config));

    if (get_scl(config) == 0 || get_sda(config) == 0) {
        printf("Warning: IIC%d bus may not be fully recovered. SCL:%d SDA:%d\r\n", iic_idx,
               get_scl(config), get_sda(config));
    }

    /* soft reset iic ip*/
    i2c_soft_rst_reg_val = mmio_read_32(0x03003000);
    printf("i2c%d_soft_rst_reg_val:%#x\r\n", iic_idx, i2c_soft_rst_reg_val);
    mmio_write_32(0x03003000, i2c_soft_rst_reg_val & (~(1 << (27 + iic_idx))));
    rt_hw_us_delay(100);
    mmio_write_32(0x03003000, i2c_soft_rst_reg_val | (1 << (27 + iic_idx)));
    rt_hw_us_delay(100);

    /* Restore pinmux configuration */
    if (config->config_iic_mode) {
        config->config_iic_mode();
    }
    rt_hw_us_delay(50);

    /* Reinitialize I2C controller */
    dw_iic_disable(iic_base);
    rt_hw_us_delay(10);
    dw_iic_clear_all_irq(iic_base);
    dw_iic_disable_all_irq(iic_base);
    iic_base->IC_CLR_TX_ABRT;

    // Reset software state
    iic_base->IC_SAR = 0;
    // iic->state.writeable = 1U;
    // iic->state.readable  = 1U;
    // iic->state.error     = 0U;

    dw_iic_set_receive_fifo_threshold(iic_base, 0x1);
    dw_iic_set_transmit_fifo_threshold(iic_base, 0x0);
    dw_iic_set_sda_hold_time(iic_base, 0x1e);
    dw_iic_set_master_mode(iic_base);
    dw_iic_enable_restart(iic_base);
    dw_iic_set_speed(iic_base, ori_speed);

    rt_hw_us_delay(100);
    iic_base->IC_CLR_TX_ABRT;

    printf("IIC%d recovery completed successfully\r\n", iic_idx);
    return 0;
}

static int dw_iic_xfer_init(dw_iic_regs_t* iic_base, rt_uint32_t dev_addr)
{
    if (dw_iic_wait_for_bb(iic_base)) {
        printf("I2C transfer init failed, starting recovery...\r\n");
        if (dw_iic_recover_bus(iic_base) == 0) {
            // Recovery successful, try initialization again
            if (dw_iic_wait_for_bb(iic_base)) {
                printf("I2C transfer init failed again after recovery\r\n");
                return -RT_ERROR;
            }
        } else {
            printf("I2C bus recovery failed\r\n");
            return -RT_ERROR;
        }
    }

    // dw_iic_master_enable_receive_irq(iic_base);
    dw_iic_master_enable_transmit_irq(iic_base);
    dw_iic_set_target_address(iic_base, dev_addr);
    dw_iic_enable(iic_base);

    return RT_EOK;
}

static int dw_iic_xfer_finish(dw_iic_regs_t* iic_base)
{
    rt_uint32_t timeout = 0;

    while (1) {
        if (iic_base->IC_RAW_INTR_STAT & DW_IIC_RAW_STOP_DET) {
            iic_base->IC_CLR_STOP_DET;
            break;
        } else {
            timeout++;
            rt_hw_us_delay(5);
            if (timeout > 10000) {
                LOG_E("xfer finish timeout\n");
                return -RT_ETIMEOUT;
                break;
            }
        }
    }

    if (dw_iic_wait_for_bb(iic_base)) {
        return -RT_ERROR;
    }

    dw_iic_flush_rxfifo(iic_base);

    return RT_EOK;
}

static rt_err_t dw_iic_check_addr(dw_iic_regs_t* iic_base)
{
    if (iic_base->IC_TX_ABRT_SOURCE & DW_IIC_TX_ABRT_7B_ADDR_NOACK ||
        iic_base->IC_TX_ABRT_SOURCE & DW_IIC_TX_ABRT_10ADDR1_NOACK ||
        iic_base->IC_TX_ABRT_SOURCE & DW_IIC_TX_ABRT_10ADDR2_NOACK)
        return -RT_EIO;
    else
        return RT_EOK;
}

static rt_err_t dw_iic_recv(dw_iic_regs_t* iic_base, struct rt_i2c_msg* msg, rt_uint32_t timeout)
{
    rt_err_t    ret = RT_EOK;
    rt_uint32_t timecount, size, send_size;
    rt_uint8_t* data;
    uint16_t    cmd           = 0;
    uint8_t     rx_fifo_depth = dw_iic_get_rx_fifo_depth(iic_base);

    timecount = timeout + rt_tick_get_millisecond();
    data      = msg->buf;
    size      = msg->len;
    RT_ASSERT(data != RT_NULL);

    send_size = size;

    while (size > 0 || send_size > 0) {
        ret = dw_iic_check_addr(iic_base);
        if (ret) {
            if (!(msg->flags & RT_I2C_IGNORE_NACK)) LOG_E("Rmote I/O ERROR\n");
            return ret;
        }

        if (iic_base->IC_STATUS & DW_IIC_TXFIFO_NOT_FULL_STATE) {
            u16 tx_valid = rx_fifo_depth - iic_base->IC_TXFLR;

            for (; send_size > 0 && tx_valid > 0; send_size--, tx_valid--) {
                cmd = 0;
                if (send_size == 1 && !(msg->flags & RT_I2C_NO_STOP)) cmd |= DW_IIC_DATA_STOP;

                if (msg->flags & RT_I2C_RESTART) {
                    cmd |= DW_IIC_DATA_RESTART;
                    msg->flags &= ~RT_I2C_RESTART;
                }
                dw_iic_transmit_data(iic_base, DW_IIC_DATA_CMD | cmd);
            }
        }

        if (iic_base->IC_STATUS & DW_IIC_RXFIFO_NOT_EMPTY_STATE) {
            uint16_t rx_valid = iic_base->IC_RXFLR;

            for (; size > 0 && rx_valid > 0; size--, rx_valid--) {
                *data++ = dw_iic_receive_data(iic_base);
            }
            timecount = timeout + rt_tick_get_millisecond();
        } else if (rt_tick_get_millisecond() >= timecount) {
            LOG_E("Timed out read ic_cmd_data");
            LOG_E("size:%d, send_size:%d", size, send_size);
            LOG_E("Dump master:");
            dump_iic_status(iic_base);

            return -RT_ETIMEOUT;
        }
    }

    return ret;
}

static rt_err_t dw_iic_send(dw_iic_regs_t* iic_base, struct rt_i2c_msg* msg, rt_uint32_t timeout)
{
    rt_err_t       ret = RT_EOK;
    rt_uint32_t    timecount, size;
    const uint8_t* data;
    uint16_t       cmd = 0;

    timecount = timeout + rt_tick_get_millisecond();
    data      = msg->buf;
    size      = msg->len;
    RT_ASSERT(data != RT_NULL);

    while (size > 0) {
        ret = dw_iic_check_addr(iic_base);
        if (ret) {
            if (!(msg->flags & RT_I2C_IGNORE_NACK)) LOG_E("Rmote I/O ERROR\n");
            return ret;
        }

        if (size - 1 == 0 && !(msg->flags & RT_I2C_NO_STOP)) {
            cmd |= DW_IIC_DATA_STOP;
        }

        if (msg->flags & RT_I2C_RESTART) {
            cmd |= DW_IIC_DATA_RESTART;
            msg->flags &= ~RT_I2C_RESTART;
        }

        if (iic_base->IC_STATUS & DW_IIC_TXFIFO_NOT_FULL_STATE) {
            dw_iic_transmit_data(iic_base, *data++ | cmd);
            size--;
            cmd       = 0;
            timecount = timeout + rt_tick_get_millisecond();
        } else if (rt_tick_get_millisecond() >= timecount) {
            LOG_D("ic status is not TFNF\n");
            return -RT_ETIMEOUT;
        }
    }

    return ret;
}

static rt_ssize_t dw_iic_master_xfer(struct rt_i2c_bus_device* bus, struct rt_i2c_msg msgs[],
                                     rt_uint32_t num)
{
    struct rt_i2c_msg* msg;
    rt_uint32_t        i;
    rt_ssize_t         ret = -RT_ERROR;
    rt_uint32_t        timeout;

    struct dw_iic_bus* i2c_bus  = (struct dw_iic_bus*)bus;
    dw_iic_regs_t*     iic_base = i2c_bus->iic_base;

    if (msgs[0].flags & RT_I2C_ADDR_10BIT) {
        dw_iic_set_master_10bit_addr_mode(iic_base);
        dw_iic_set_slave_10bit_addr_mode(iic_base);
    } else {
        dw_iic_set_master_7bit_addr_mode(iic_base);
        dw_iic_set_slave_7bit_addr_mode(iic_base);
    }

    if (dw_iic_xfer_init(iic_base, msgs[0].addr)) {
        ret = -RT_ERROR;
        goto ERR_EXIT;
    }

    for (i = 0; i < num; i++) {
        msg = &msgs[i];
        if (i > 0) msg->flags |= RT_I2C_RESTART;
        if (i != num - 1) msg->flags |= RT_I2C_NO_STOP;
        if (msg->flags & RT_I2C_RD) {
            timeout = 100;
            ret     = dw_iic_recv(iic_base, msg, timeout);
            if (ret == -RT_ETIMEOUT) {
                goto WAIT_STOP;
            }
            if (ret != RT_EOK) goto ERR_EXIT;
        } else {
            timeout = 100;
            ret     = dw_iic_send(iic_base, msg, timeout);
            if (ret == -RT_ETIMEOUT) {
                goto WAIT_STOP;
            }
            if (ret != RT_EOK) goto ERR_EXIT;
        }
    }

WAIT_STOP:
    ret = dw_iic_xfer_finish(iic_base);
    if (ret) {
        dw_iic_recover_bus(iic_base);
        goto ERR_EXIT;
    }

ERR_EXIT:
    dw_iic_disable(iic_base);
    dw_iic_clear_all_irq(iic_base);
    return ret == RT_EOK ? num : ret;
}

static rt_err_t dw_iic_bus_control(struct rt_i2c_bus_device* bus, int cmd, void* args)
{
    struct dw_iic_bus* i2c_bus = (struct dw_iic_bus*)bus;
    rt_uint32_t        speed;

    RT_ASSERT(bus != RT_NULL);

    dw_iic_regs_t* iic_base = i2c_bus->iic_base;

    switch (cmd) {
    case RT_I2C_DEV_CTRL_CLK:
        speed = *(rt_uint32_t*)args;
        if (dw_iic_set_speed(iic_base, speed)) return RT_ERROR;
        break;

    case RT_I2C_DEV_CTRL_10BIT:
        dw_iic_set_master_10bit_addr_mode(iic_base);
        dw_iic_set_slave_10bit_addr_mode(iic_base);
        break;

    default:
        return -RT_EIO;
        break;
    }

    return RT_EOK;
}

static const struct rt_i2c_bus_device_ops i2c_ops = {
    .master_xfer     = dw_iic_master_xfer,
    .slave_xfer      = RT_NULL,
    .i2c_bus_control = dw_iic_bus_control,
};

int rt_hw_i2c_init(void)
{
    int result = RT_EOK;

    for (rt_size_t i = 0; i < sizeof(_i2c_obj) / sizeof(struct dw_iic_bus); i++) {
        dw_iic_init(_i2c_obj[i].iic_base, DW_IIC_DEFAULT_SPEED);

        _i2c_obj[i].parent.ops = &i2c_ops;

        /* register i2c device */
        if (rt_i2c_bus_device_register(&_i2c_obj[i].parent, _i2c_obj[i].device_name) == RT_EOK) {
            LOG_D("%s init success", _i2c_obj[i].device_name);
        } else {
            LOG_E("%s register failed", _i2c_obj[i].device_name);
            result = -RT_ERROR;
        }
    }

    return result;
}
INIT_DEVICE_EXPORT(rt_hw_i2c_init);
