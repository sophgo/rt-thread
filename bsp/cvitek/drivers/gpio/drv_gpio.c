/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023/10/19     xiunian      first version
 */
#include <board.h>
#include <rtdevice.h>
#include <rthw.h>
#include <rtthread.h>
#ifdef RT_USING_SMART
#include <ioremap.h>
#endif

#ifdef RT_USING_PIN
#include "drv_gpio.h"
#include "dwapb_gpio.h"

/* pin numbering is defined in dwapb_gpio.h (same as the Linux gpio numbers) */
#define PIN_NUM(port, no) DWAPB_PIN_NUM(port, no)
#define PIN_PORT(pin)     ((uint8_t)DWAPB_PIN_PORT(pin))
#define PIN_NO(pin)       ((uint8_t)DWAPB_PIN_BIT(pin))

static struct dwapb_event {
    void (*(hdr[DWAPB_GPIO_NR]))(void *args);
    void      *args[DWAPB_GPIO_NR];
    rt_uint8_t is_both_edge[DWAPB_GPIO_NR];
} _dwapb_events[DWAPB_GPIO_PORT_NR];

static void dwapb_pin_mode(struct rt_device *device, rt_base_t pin, rt_uint8_t mode)
{
    rt_uint8_t bit, port;

    bit  = PIN_NO(pin);
    port = PIN_PORT(pin);

    switch (mode) {
    case PIN_MODE_OUTPUT:
        dwapb_gpio_set_pin_mode(port, bit, 1);
        break;
    case PIN_MODE_INPUT:
        dwapb_gpio_set_pin_mode(port, bit, 0);
        break;
    case PIN_MODE_INPUT_PULLUP:
        dwapb_gpio_set_pin_mode(port, bit, 0);
        // TODO: set pin pullup
        break;
    case PIN_MODE_INPUT_PULLDOWN:
        dwapb_gpio_set_pin_mode(port, bit, 0);
        // TODO: set pin pulldown
        break;
    }
}

static void dwapb_pin_write(struct rt_device *device, rt_base_t pin, rt_uint8_t value)
{
    dwapb_gpio_write_pin(PIN_PORT(pin), PIN_NO(pin), value);
}

static rt_ssize_t dwapb_pin_read(struct rt_device *device, rt_base_t pin)
{
    return dwapb_gpio_read_pin(PIN_PORT(pin), PIN_NO(pin));
}

static rt_base_t dwapb_pin_get(const char *name)
{
    rt_base_t pin = 0;
    int       port_num, pin_num = 0;
    int       i, name_len;

    name_len = rt_strlen(name);

    if ((name_len < 2) || (name_len > 3)) {
        goto out;
    }

    if ((name[0] >= 'A') && (name[0] <= 'E')) {
        port_num = (int)(name[0] - 'A');
    } else if ((name[0] >= 'a') && (name[0] <= 'e')) {
        port_num = (int)(name[0] - 'a');
    } else {
        goto out;
    }

    for (i = 1; i < name_len; i++) {
        pin_num *= 10;
        pin_num += name[i] - '0';
    }

    pin = PIN_NUM(port_num, pin_num);

    return pin;
out:
    rt_kprintf("\nError: pin name error, xy   x:A~E  y:0~31, e.g. C24\n");
    return -RT_EINVAL;
}

static rt_err_t dwapb_pin_attach_irq(struct rt_device *device, rt_base_t pin, rt_uint8_t mode,
                                     void (*hdr)(void *args), void *args)
{
    rt_uint8_t bit, port;
    rt_uint8_t level = 0, rising = 0, both_edge = 0;

    bit  = PIN_NO(pin);
    port = PIN_PORT(pin);

    if (mode == PIN_IRQ_MODE_HIGH_LEVEL || mode == PIN_IRQ_MODE_LOW_LEVEL) {
        level = 1;
        rising = (mode == PIN_IRQ_MODE_HIGH_LEVEL);
    } else if (mode == PIN_IRQ_MODE_RISING_FALLING) {
        both_edge = 1;
    } else if (mode == PIN_IRQ_MODE_RISING || mode == PIN_IRQ_MODE_FALLING) {
        rising = (mode == PIN_IRQ_MODE_RISING);
    }

    dwapb_gpio_irq_mode(port, bit, level, rising, both_edge);

    _dwapb_events[PIN_PORT(pin)].hdr[PIN_NO(pin)]          = hdr;
    _dwapb_events[PIN_PORT(pin)].args[PIN_NO(pin)]         = args;
    _dwapb_events[PIN_PORT(pin)].is_both_edge[PIN_NO(pin)] = (mode == PIN_IRQ_MODE_RISING_FALLING);

    return RT_EOK;
}

static rt_err_t dwapb_pin_detach_irq(struct rt_device *device, rt_base_t pin)
{
    _dwapb_events[PIN_PORT(pin)].hdr[PIN_NO(pin)]          = RT_NULL;
    _dwapb_events[PIN_PORT(pin)].args[PIN_NO(pin)]         = RT_NULL;
    _dwapb_events[PIN_PORT(pin)].is_both_edge[PIN_NO(pin)] = 0;

    return RT_EOK;
}

static rt_err_t dwapb_pin_irq_enable(struct rt_device *device, rt_base_t pin, rt_uint8_t enabled)
{
    dwapb_gpio_irq_enable(PIN_PORT(pin), PIN_NO(pin), enabled);

    return RT_EOK;
}

/* clang-format off */
static const struct rt_pin_ops _dwapb_ops =
{
    dwapb_pin_mode,
    dwapb_pin_write,
    dwapb_pin_read,
    dwapb_pin_attach_irq,
    dwapb_pin_detach_irq,
    dwapb_pin_irq_enable,
    dwapb_pin_get,
};
/* clang-format on */

static void rt_hw_gpio_isr(int irqno, void *param)
{
    rt_uint8_t  port;
    rt_uint32_t pending, mask;

    mask = 0;
    port = (irqno == BSP_SYS_GPIO_IRQ_BASE ? 4 : (irqno - BSP_GPIO_IRQ_BASE));

    pending = dwapb_gpio_irq_status(port);

    if (pending) {
        rt_base_t bit;

        for (bit = 0; bit < DWAPB_GPIO_NR; ++bit) {
            if (pending & BIT(bit)) {
                mask = (mask | (BIT(bit)));

                if (_dwapb_events[port].hdr[bit] != RT_NULL) {
                    _dwapb_events[port].hdr[bit](_dwapb_events[port].args[bit]);
                }

                if (_dwapb_events[port].is_both_edge[bit]) {
                    dwapb_gpio_toggle_trigger(port, bit);
                }
            }
        }
    }

    dwapb_gpio_irq_eoi(port, mask);
}

int rt_hw_gpio_init(void)
{
    rt_device_pin_register("gpio", &_dwapb_ops, RT_NULL);

#define INT_INSTALL_GPIO_DEVICE(no)                                                     \
    rt_hw_interrupt_install(BSP_GPIO_IRQ_BASE + (no), rt_hw_gpio_isr, RT_NULL, "gpio"); \
    rt_hw_interrupt_umask(BSP_GPIO_IRQ_BASE + (no));

    INT_INSTALL_GPIO_DEVICE(0);
    INT_INSTALL_GPIO_DEVICE(1);
    INT_INSTALL_GPIO_DEVICE(2);
    INT_INSTALL_GPIO_DEVICE(3);

    rt_hw_interrupt_install(BSP_SYS_GPIO_IRQ_BASE, rt_hw_gpio_isr, RT_NULL, "gpio");
    rt_hw_interrupt_umask(BSP_SYS_GPIO_IRQ_BASE);
    return 0;
}
INIT_DEVICE_EXPORT(rt_hw_gpio_init);

#endif /* RT_USING_PIN */
