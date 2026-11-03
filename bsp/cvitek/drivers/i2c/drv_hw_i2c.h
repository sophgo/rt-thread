/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-02-14     ShichengChu  first version
 */
#ifndef __DRV_HW_I2C_H__
#define __DRV_HW_I2C_H__

#include <rthw.h>
#include <rtthread.h>

#include "dw_iic.h"
#include "mmio.h"
#include "pinctrl.h"
#include "rtdevice.h"

#define I2C0_IRQ (BSP_I2C_IRQ_BASE + 0)
#define I2C1_IRQ (BSP_I2C_IRQ_BASE + 1)
#define I2C2_IRQ (BSP_I2C_IRQ_BASE + 2)
#define I2C3_IRQ (BSP_I2C_IRQ_BASE + 3)
#define I2C4_IRQ (BSP_I2C_IRQ_BASE + 4)

struct dw_iic_bus {
    struct rt_i2c_bus_device parent;
    dw_iic_regs_t           *iic_base;
    rt_uint32_t              irq;
    char                    *device_name;
};

/* IIC recovery pin configuration callback function types */
typedef void (*iic_recovery_pinmux_config_func_t)(void);

/* IIC recovery configuration structure */
typedef struct {
    uint8_t                           enable;           /* Enable/disable recovery for this IIC */
    char                              scl_gpio_grp;     /* SCL GPIO group */
    uint8_t                           scl_gpio_num;     /* SCL GPIO number */
    char                              sda_gpio_grp;     /* SDA GPIO group */
    uint8_t                           sda_gpio_num;     /* SDA GPIO number */
    iic_recovery_pinmux_config_func_t config_gpio_mode; /* Function to configure pins as GPIO */
    iic_recovery_pinmux_config_func_t config_iic_mode;  /* Function to configure pins as IIC */
} iic_recovery_config_t;

int                      rt_hw_i2c_init(void);
const struct dw_iic_bus *i2c_bus_find_by_name(const char *name);
const struct dw_iic_bus *i2c_bus_get_by_num(uint8_t id);
int                      iic_set_recovery_config(uint8_t iic_idx, const iic_recovery_config_t *config);
int                      iic_get_recovery_config(uint8_t iic_idx, iic_recovery_config_t *config);
int                      iic_recovery_enable(uint8_t iic_idx, uint8_t enable);
int                      iic_recovery_is_enabled(uint8_t iic_idx);
int                      dw_iic_recover_bus(dw_iic_regs_t *iic_base);
#endif /* __DRV_HW_I2C_H__ */
