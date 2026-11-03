#include "usbd_cdc_uart.h"

#include <finsh.h>
#include <ipc/ringbuffer.h>
#include <rtdevice.h>
#include <rtthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usbd_cdc.h"
#include "usbd_cdc_uart_descriptor.h"
#include "usbd_comp.h"
#include "usbd_core.h"
#include "usbd_descriptor.h"

#define CDC_MAX_RW_LEN (CDC_UART_MPS)

/*!< uart cfg */
#define EVENT_WRITE  0x0F0F0000
#define EVENT_READ   0x00000F0F
#define UART_RB_SIZE 4096

#define uart(dev)              ((cdc_uart_dev_t *)dev)
#define usb_serial_uart_uninit rvm_hal_device_free

enum {
    USB_SERIAL_CTRL_CONFIG = 0x1000,
    USB_SERIAL_CTRL_SET_TYPE,
    USB_SERIAL_CTRL_SET_BUFFER_SIZE,
    USB_SERIAL_CTRL_SET_EVENT,
};

typedef struct {
    rt_device_t device;
    rt_event_t  event_write_read;
    void (*write_event)(rt_device_t dev, int event_id, void *priv);
    void                *priv;
    int                  type;
    rt_uint8_t          *recv_buf;
    uint8_t             *cdc_tx_buffer;
    uint8_t             *cdc_rx_buffer;
    struct rt_ringbuffer read_buffer;
} cdc_uart_dev_t;

static cdc_uart_dev_t                                *s_uart_ctx = NULL;
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_cdc_rx_buffer[CDC_MAX_RW_LEN];

static struct cdc_uart_device_info cdc_uart_info;
static uint8_t                    *cdc_uart_descriptor = NULL;

static cdc_uart_dev_t *dev_ctx(void)
{
    if (!s_uart_ctx) {
        USB_LOG_INFO("cdc ctx is null\n");
    }
    return s_uart_ctx;
}

static rt_err_t usb_serial_uart_open(rt_device_t dev, rt_uint16_t oflag)
{
    return RT_EOK;
}

static rt_err_t usb_serial_uart_close(rt_device_t dev)
{
    cdc_uart_dev_t *uart_dev = (cdc_uart_dev_t *)dev->user_data;

    if (uart_dev->event_write_read) {
        rt_event_delete(uart_dev->event_write_read);
        uart_dev->event_write_read = RT_NULL;
    }

    if (uart_dev->cdc_tx_buffer) {
        rt_free(uart_dev->cdc_tx_buffer);
        uart_dev->cdc_tx_buffer = RT_NULL;
    }

    if (uart_dev->recv_buf) {
        rt_free(uart_dev->recv_buf);
        uart_dev->recv_buf = RT_NULL;
    }

    return RT_EOK;
}

static rt_ssize_t usb_serial_uart_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    cdc_uart_dev_t *uart_dev   = (cdc_uart_dev_t *)dev->user_data;
    rt_size_t       total_read = 0;
    rt_size_t       read_bytes = 0;
    rt_uint32_t     recv_event;

    while (size > 0) {
        read_bytes =
            rt_ringbuffer_get(&uart_dev->read_buffer, (uint8_t *)buffer + total_read, size);
        total_read += read_bytes;
        size -= read_bytes;

        if (read_bytes > 0) {
            USB_LOG_DBG("usb_serial_uart_read: read %d bytes from ringbuffer, total %d\n",
                        read_bytes, total_read);
        }

        if (size == 0) break;

        if (rt_event_recv(uart_dev->event_write_read, EVENT_READ,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, rt_tick_from_millisecond(100),
                          &recv_event) != RT_EOK) {
            break;
        }
    }

    return total_read;
}

static rt_ssize_t usb_serial_uart_write(rt_device_t dev, rt_off_t pos, const void *buffer,
                                        rt_size_t size)
{
    cdc_uart_dev_t *uart_dev = (cdc_uart_dev_t *)dev->user_data;
    rt_uint32_t     recv_event;

    if (size > CDC_MAX_RW_LEN) {
        size = CDC_MAX_RW_LEN;
        rt_kprintf("Warning: Truncated to CDC_MAX_RW_LEN\n");
    }

    rt_memcpy(uart_dev->cdc_tx_buffer, buffer, size);

    usbd_ep_start_write(cdc_uart_info.cdc_uart_in_ep.ep_addr, uart_dev->cdc_tx_buffer, size);

    if (rt_event_recv(uart_dev->event_write_read, EVENT_WRITE,
                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER,
                      &recv_event) == RT_EOK) {
        return size;
    }

    return 0;
}

static rt_err_t usb_serial_uart_control(rt_device_t dev, int cmd, void *args)
{
    cdc_uart_dev_t *uart_dev = (cdc_uart_dev_t *)dev->user_data;

    switch (cmd) {
    case USB_SERIAL_CTRL_CONFIG:
        break;

    case USB_SERIAL_CTRL_SET_TYPE:
        uart_dev->type = (int)(rt_ubase_t)args;
        break;

    case USB_SERIAL_CTRL_SET_BUFFER_SIZE:
        break;

    case USB_SERIAL_CTRL_SET_EVENT:
        uart_dev->write_event = (void (*)(rt_device_t, int, void *))args;
        break;

    default:
        return -RT_EINVAL;
    }

    return RT_EOK;
}

static struct rt_device_ops usb_serial_ops = {
    RT_NULL,
    usb_serial_uart_open,
    usb_serial_uart_close,
    usb_serial_uart_read,
    usb_serial_uart_write,
    usb_serial_uart_control,
};

static rt_err_t usb_serial_uart_init(rt_device_t dev)
{
    cdc_uart_dev_t *uart_dev = (cdc_uart_dev_t *)dev->user_data;

    uart_dev->event_write_read = rt_event_create("usb_evt", RT_IPC_FLAG_FIFO);
    if (!uart_dev->event_write_read) {
        rt_kprintf("create event failed\n");
        return -RT_ENOMEM;
    }

    uart_dev->recv_buf = rt_malloc(UART_RB_SIZE);
    if (!uart_dev->recv_buf) {
        rt_kprintf("no memory for recv buf\n");
        rt_event_delete(uart_dev->event_write_read);
        return -RT_ENOMEM;
    }

    uart_dev->cdc_tx_buffer = rt_malloc(CDC_MAX_RW_LEN);
    if (!uart_dev->cdc_tx_buffer) {
        rt_kprintf("no memory for tx buffer\n");
        rt_free(uart_dev->recv_buf);
        rt_event_delete(uart_dev->event_write_read);
        return -RT_ENOMEM;
    }

    uart_dev->cdc_rx_buffer = s_cdc_rx_buffer;

    rt_ringbuffer_init(&uart_dev->read_buffer, uart_dev->recv_buf, UART_RB_SIZE);
    return RT_EOK;
}

static void usbd_cdc_acm_bulk_out(uint8_t ep, uint32_t nbytes)
{
    if (ep != cdc_uart_info.cdc_uart_out_ep.ep_addr) {
        rt_kprintf("Invalid EP: %02x\n", ep);
        return;
    }

    cdc_uart_dev_t *ctx = dev_ctx();
    if (ctx) {
        rt_ringbuffer_put(&ctx->read_buffer, ctx->cdc_rx_buffer, nbytes);
        rt_event_send(ctx->event_write_read, EVENT_READ);
        usbd_ep_start_read(ep, ctx->cdc_rx_buffer, CDC_MAX_RW_LEN);
    }
}

static void usbd_cdc_acm_bulk_in(uint8_t ep, uint32_t nbytes)
{
    if (ep != cdc_uart_info.cdc_uart_in_ep.ep_addr) {
        rt_kprintf("Invalid EP: %02x\n", ep);
        return;
    }

    cdc_uart_dev_t *ctx = dev_ctx();
    if (ctx) {
        if ((nbytes % CDC_UART_MPS) == 0 && nbytes) {
            usbd_ep_start_write(ep, NULL, 0);
        } else {
            rt_event_send(ctx->event_write_read, EVENT_WRITE);
        }
    }
}

static void cdc_uart_desc_register_cb(void)
{
    if (cdc_uart_descriptor) {
        rt_free(cdc_uart_descriptor);
        cdc_uart_descriptor = NULL;
    }
}

static void cdc_uart_configure_done_callback(void)
{
    usbd_ep_start_read(cdc_uart_info.cdc_uart_out_ep.ep_addr, s_cdc_rx_buffer, CDC_MAX_RW_LEN);
}

static void drv_cdc_acm_uart_register(void)
{
    s_uart_ctx = rt_malloc(sizeof(cdc_uart_dev_t));
    if (!s_uart_ctx) {
        rt_kprintf("malloc for cdc context failed\n");
        return;
    }
    rt_memset(s_uart_ctx, 0, sizeof(cdc_uart_dev_t));

    s_uart_ctx->device = rt_device_create(RT_Device_Class_Char, 0);
    if (!s_uart_ctx->device) {
        rt_kprintf("create device failed\n");
        rt_free(s_uart_ctx);
        s_uart_ctx = NULL;
        return;
    }

    s_uart_ctx->device->ops       = &usb_serial_ops;
    s_uart_ctx->device->user_data = s_uart_ctx;

    if (rt_device_register(s_uart_ctx->device, "usb_serial", RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("register device failed\n");
        rt_device_destroy(s_uart_ctx->device);
        rt_free(s_uart_ctx);
        s_uart_ctx = NULL;
        return;
    }

    if (usb_serial_uart_init(s_uart_ctx->device) != RT_EOK) {
        rt_kprintf("usb serial uart init failed\n");
        rt_device_unregister(s_uart_ctx->device);
        rt_device_destroy(s_uart_ctx->device);
        rt_free(s_uart_ctx);
        s_uart_ctx = NULL;
    }
}

static void drv_cdc_acm_uart_unregister(void)
{
    if (s_uart_ctx) {
        if (s_uart_ctx->device) {
            rt_device_unregister(s_uart_ctx->device);
            rt_device_destroy(s_uart_ctx->device);
        }
        rt_free(s_uart_ctx);
        s_uart_ctx = NULL;
    }
}

void cdc_uart_desc_register(void)
{
    uint32_t desc_len;

    cdc_uart_info.cdc_uart_out_ep.ep_cb   = usbd_cdc_acm_bulk_out;
    cdc_uart_info.cdc_uart_out_ep.ep_addr = comp_get_available_ep(0);
    cdc_uart_info.cdc_uart_in_ep.ep_cb    = usbd_cdc_acm_bulk_in;
    cdc_uart_info.cdc_uart_in_ep.ep_addr  = comp_get_available_ep(1);
    cdc_uart_info.cdc_uart_int_ep.ep_addr = comp_get_available_ep(1);
    cdc_uart_info.interface_nums          = comp_get_interfaces_num();
    USB_LOG_DBG("cdc_uart out ep:%#x\n", cdc_uart_info.cdc_uart_out_ep.ep_addr);
    USB_LOG_DBG("cdc_uart in ep:%#x\n", cdc_uart_info.cdc_uart_in_ep.ep_addr);
    USB_LOG_DBG("cdc_uart int ep:%#x\n", cdc_uart_info.cdc_uart_int_ep.ep_addr);
    USB_LOG_DBG("interface_nums:%d\n", cdc_uart_info.interface_nums);

    cdc_uart_descriptor = cdc_uart_build_descriptor(&cdc_uart_info, &desc_len);
    comp_register_descriptors(USBD_TYPE_CDC_UART, cdc_uart_descriptor, desc_len, 2,
                              cdc_uart_desc_register_cb);
    comp_register_cfg_done(USBD_TYPE_CDC_UART, cdc_uart_configure_done_callback);

    usbd_add_interface(usbd_cdc_acm_init_intf(&cdc_uart_info.cdc_uart_intf0));
    usbd_add_interface(usbd_cdc_acm_init_intf(&cdc_uart_info.cdc_uart_intf1));
    usbd_add_endpoint(&cdc_uart_info.cdc_uart_out_ep);
    usbd_add_endpoint(&cdc_uart_info.cdc_uart_in_ep);
}

void cdc_uart_init(void)
{
    drv_cdc_acm_uart_register();
}

void cdc_uart_deinit(void)
{
    drv_cdc_acm_uart_unregister();
}