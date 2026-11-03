/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023/06/25     flyingcys    first version
 */
#ifndef __DRV_USART_H__
#define __DRV_USART_H__

#include <rthw.h>
#include <rtthread.h>

#include "dw_uart.h"
#include "hal_dma.h"
#include "mmio.h"
#include "pinctrl.h"
#include "rtdevice.h"

#define UART0_IRQ (BSP_UART_IRQ_BASE + 0)
#define UART1_IRQ (BSP_UART_IRQ_BASE + 1)
#define UART2_IRQ (BSP_UART_IRQ_BASE + 2)
#define UART3_IRQ (BSP_UART_IRQ_BASE + 3)
#define UART4_IRQ (BSP_UART_IRQ_BASE + 4)

/****** UART Event *****/
typedef enum {
    UART_EVENT_SEND_COMPLETE = 0,      ///< Send data completed.
    UART_EVENT_RECEIVE_COMPLETE,       ///< Receive data completed.
    UART_EVENT_RECEIVE_FIFO_READABLE,  ///< Data in uart fifo, call cvi_uart_receive() get the data.
    UART_ENENT_BREAK_INTR,  ///< the serial input,sin, is held in a logic '0' state for longer than
                            ///< the sum of start time+data bits+parity+stop bits.
    UART_EVENT_ERROR_OVERFLOW,  ///< A new data character was received before the previous data was
                                ///< read.
    UART_EVENT_ERROR_PARITY,    ///< Occur parity error in the receiver.
    UART_EVENT_ERROR_FRAMING    ///< the receiver does not detect a valid STOP bit in the received
                                ///< data.
} cvi_uart_event_t;

typedef struct {
    rt_uint8_t readable;
    rt_uint8_t writeable;
    rt_uint8_t error;
} cvi_uart_state_t;

typedef struct dw_uart cvi_uart_t;

struct dw_uart {
    struct rt_serial_device *dev;
    char                     name[RT_NAME_MAX];
    void (*callback)(cvi_uart_t *uart, cvi_uart_event_t event, void *arg);
    void         *arg;
    rt_ubase_t    hw_base;
    rt_uint32_t   irqno;
    rt_uint8_t   *tx_data;
    rt_uint32_t   tx_size;
    rt_uint8_t   *rx_data;
    rt_uint32_t   rx_size;
    hal_dma_ch_t *tx_dma;
    hal_dma_ch_t *rx_dma;
    rt_err_t (*send)(cvi_uart_t *uart, const void *data, rt_uint32_t size);
    rt_err_t (*receive)(cvi_uart_t *uart, void *data, rt_uint32_t size);
    cvi_uart_state_t state;
    void            *priv;
};

int  rt_hw_uart_init(void);
void csi_uart_dump_registers(cvi_uart_t *uart);

#endif /* __DRV_USART_H__ */
