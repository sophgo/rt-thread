/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023/06/25     flyingcys    first version
 */

#include <rtthread.h>
#include <pthread.h>
#include <stdio.h>
#include <drivers/dev_pin.h>

#if defined(BOARD_TYPE_MILKV_DUOS)
#define LED_PIN     "A29" /* Onboard LED pins */
#elif defined(__CV181X__)
#define LED_PIN     "E02" /* Onboard LED pins */
#elif defined(__CV180X__)
#define LED_PIN     "C24" /* Onboard LED pins */
#else
#warning "No known chip series matched: falling back to safe pin defaults, LED/USB hub demo hooks may not match this board"
#define LED_PIN     "A29"
#endif

#if defined(__CV181X__)
#define USB_RST_PIN     "E00" /* Onboard USB hub reset pin */
#define USB_SEL_PIN     "E01" /* Onboard USB hub select pin */
#elif defined(__CV180X__)
#define USB_RST_PIN     "E26" /* Onboard USB hub reset pin */
#define USB_SEL_PIN     "E25" /* Onboard USB hub select pin */
#else
#define USB_RST_PIN     "A28" /* Fallback USB hub reset pin */
#define USB_SEL_PIN     "A27" /* Fallback USB hub select pin */
#endif

int main(void)
{
#ifdef RT_USING_SMART
    rt_kprintf("Hello RT-Smart!\n");
#else
    rt_kprintf("Hello RISC-V!\n");
#endif

    /* LED pin: C24 */
    rt_uint16_t led = rt_pin_get(LED_PIN);

    /* set LED pin mode to output */
    rt_pin_mode(led, PIN_MODE_OUTPUT);

    #if (CONFIG_SUPPORT_USB_DC || CONFIG_SUPPORT_USB_HC)
    extern void enable_usb_hub(void);
    enable_usb_hub();
    #endif

    while (1)
    {
        rt_pin_write(led, PIN_HIGH);

        rt_thread_mdelay(500);

        rt_pin_write(led, PIN_LOW);

        rt_thread_mdelay(500);
    }

    return 0;
}

#if (CONFIG_SUPPORT_USB_DC || CONFIG_SUPPORT_USB_HC)
void enable_usb_hub(void)
{
    /* LED pin: E00 */
    rt_uint16_t usb_rst = rt_pin_get(USB_RST_PIN);

    /* set LED pin mode to output */
    rt_pin_mode(usb_rst, PIN_MODE_OUTPUT);

    rt_pin_write(usb_rst, PIN_HIGH);

    /* LED pin: E01 */
    rt_uint16_t usb_sel = rt_pin_get(USB_SEL_PIN);

    /* set LED pin mode to output */
    rt_pin_mode(usb_sel, PIN_MODE_OUTPUT);

    rt_pin_write(usb_sel, PIN_HIGH);
}


void disable_usb_hub(void)
{
    /* LED pin: E00 */
    rt_uint16_t usb_rst = rt_pin_get(USB_RST_PIN);

    /* set LED pin mode to output */
    rt_pin_mode(usb_rst, PIN_MODE_OUTPUT);

    rt_pin_write(usb_rst, PIN_LOW);

    /* LED pin: E01 */
    rt_uint16_t usb_sel = rt_pin_get(USB_SEL_PIN);

    /* set LED pin mode to output */
    rt_pin_mode(usb_sel, PIN_MODE_OUTPUT);

    rt_pin_write(usb_sel, PIN_LOW);
}

MSH_CMD_EXPORT_ALIAS(disable_usb_hub, disable_usb_hub, disable_usb_hub);
MSH_CMD_EXPORT_ALIAS(enable_usb_hub, enable_usb_hub, enable_usb_hub);
#endif