/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023/06/25     flyingcys    first version
 */
#include "drv_uart.h"

#include <rtdevice.h>
#include <rthw.h>
#include <rtthread.h>

#include "board.h"
#include "hal_dma.h"

#define DBG_TAG "DRV.UART"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

/*
 * Divide positive or negative dividend by positive divisor and round
 * to closest integer. Result is undefined for negative divisors and
 * for negative dividends if the divisor variable type is unsigned.
 */
#define DIV_ROUND_CLOSEST(x, divisor)                                   \
    ({                                                                  \
        typeof(x)       __x = x;                                        \
        typeof(divisor) __d = divisor;                                  \
        (((typeof(x))-1) > 0 || ((typeof(divisor))-1) > 0 || (__x) > 0) \
            ? (((__x) + ((__d) / 2)) / (__d))                           \
            : (((__x) - ((__d) / 2)) / (__d));                          \
    })

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

#define BSP_DEFINE_UART_DEVICE(no)           \
    static cvi_uart_t _uart##no##_device = { \
        .dev     = NULL,                     \
        .name    = "uart" #no,               \
        .hw_base = UART##no##_BASE,          \
        .irqno   = UART##no##_IRQ,           \
        .tx_data = NULL,                     \
        .tx_size = 0,                        \
        .rx_data = NULL,                     \
        .rx_size = 0,                        \
        .tx_dma  = NULL,                     \
        .rx_dma  = NULL,                     \
        .send    = NULL,                     \
        .receive = NULL,                     \
        .state   = 0,                        \
        .priv    = NULL,                     \
    };                                       \
    static struct rt_serial_device _serial##no;

#ifdef BSP_USING_UART0
BSP_DEFINE_UART_DEVICE(0);
#endif

#ifdef BSP_USING_UART1
BSP_DEFINE_UART_DEVICE(1);
#endif

#ifdef BSP_USING_UART2
BSP_DEFINE_UART_DEVICE(2);
#endif

#ifdef BSP_USING_UART3
BSP_DEFINE_UART_DEVICE(3);
#endif

#ifdef BSP_USING_DMA
static uint8_t find_max_prime_num(uint32_t num)
{
    uint8_t ret;

    if (!(num % 8U)) {
        ret = 8U;
    } else if (!(num % 4U)) {
        ret = 4U;
    } else {
        ret = 1U;
    }

    return ret;
}
static rt_err_t dw8250_uart_dma_event_callback(hal_dma_ch_t *dma_ch, dma_event_t event, void *arg)
{
    RT_ASSERT(dma_ch != RT_NULL);
    cvi_uart_t *uart = (cvi_uart_t *)dma_ch->parents;

    if (event == DMA_EVENT_TRANSFER_ERROR) { /* DMA transfer ERROR */
        LOG_D("Uart DMA callback: transfer error\n");
        if ((uart->tx_dma != NULL) && (uart->tx_dma->ch_id == dma_ch->ch_id)) {
            // hal_dma_ch_free(dma_ch);
            // dw8250_write32(uart->hw_base, UART_FCR, UART_FCR_FIFO_EN | UART_FCR_RXSR |
            //                UART_FCR_TXSR | UART_FCR_RCVRT_1_2_FULL);

            uart->state.writeable = 1U;

            if (uart->callback) {
                uart->callback(uart, UART_EVENT_ERROR_OVERFLOW, uart->arg);
            }
        } else {
            // hal_dma_ch_free(dma_ch);
            // dw8250_write32(uart->hw_base, UART_FCR, UART_FCR_FIFO_EN | UART_FCR_RXSR |
            //                UART_FCR_TXSR | UART_FCR_RCVRT_1_2_FULL);
            // /* enable received data available */
            // dw8250_write32(uart->hw_base, UART_IER, UART_IER_ERBFI | UART_IER_ELSI);

            uart->state.readable = 1U;

            if (uart->callback) {
                uart->callback(uart, UART_EVENT_ERROR_FRAMING, uart->arg);
            }
        }
    } else if (event == DMA_EVENT_TRANSFER_DONE) { /* DMA transfer complete */
        LOG_D("Uart DMA callback: transfer done\n");
        if ((uart->tx_dma != NULL) && (uart->tx_dma->ch_id == dma_ch->ch_id)) {
            // rt_thread_mdelay(100);
            // hal_dma_ch_free(dma_ch);
            // dw8250_write32(uart->hw_base, UART_FCR, UART_FCR_FIFO_EN | UART_FCR_RXSR |
            //                UART_FCR_TXSR | UART_FCR_RCVRT_1_2_FULL);

            uart->state.writeable = 1U;

            if (uart->callback) {
                uart->callback(uart, UART_EVENT_SEND_COMPLETE, uart->arg);
            }
        } else {
            // rt_thread_mdelay(1);
            // hal_dma_ch_free(dma_ch);
            // dw8250_write32(uart->hw_base, UART_FCR, UART_FCR_FIFO_EN | UART_FCR_RXSR |
            //                UART_FCR_TXSR | UART_FCR_RCVRT_1_2_FULL);
            // /* enable received data available */
            // dw8250_write32(uart->hw_base, UART_IER, UART_IER_ERBFI | UART_IER_ELSI);
            uart->state.readable = 1U;

            if (uart->callback) {
                uart->callback(uart, UART_EVENT_RECEIVE_COMPLETE, uart->arg);
            }
        }
    }
}

int dw8250_uart_putc_dma(cvi_uart_t *uart, void *tx_buf, rt_size_t size)
{
    RT_ASSERT(uart != RT_NULL);

    int                 ret = RT_EOK;
    hal_dma_ch_config_t config;
    memset(&config, 0, sizeof(hal_dma_ch_config_t));
    uint32_t      FCR    = UART_FCR_FIFO_EN | UART_FCR_TXSR;
    hal_dma_ch_t *dma_ch = (hal_dma_ch_t *)uart->tx_dma;

    uart->tx_data = (uint8_t *)tx_buf;
    uart->tx_size = size;
    /* Disable rx/tx/ls irq*/
    dw8250_write32(uart->hw_base, UART_IER,
                   dw8250_read32(uart->hw_base, UART_IER) &
                       ~(UART_IER_ERBFI | UART_IER_ETBEI | UART_IER_ELSI));
    config.src_inc = DMA_ADDR_INC;
    config.dst_inc = DMA_ADDR_CONSTANT;
    config.src_tw  = DMA_DATA_WIDTH_8_BITS;
    config.dst_tw  = DMA_DATA_WIDTH_8_BITS;

    /* config for wj_dma */
    config.group_len = 2;  // find_max_prime_num(size);  test param
    config.trans_dir = DMA_MEM2DEV;

    /* config for etb */
    config.handshake = 1;  // FIXME:
    ret              = hal_dma_ch_config(dma_ch, &config);

    if (ret == RT_EOK) {
        FCR &= ~(UART_FCR_TET_1_2_FULL);

        if (config.group_len >= (UART_MAX_FIFO / 2U)) {
            FCR |= UART_FCR_TET_1_2_FULL;
        } else if (config.group_len >= (UART_MAX_FIFO / 4U)) {
            FCR |= UART_FCR_TET_1_4_FULL;
        } else if (config.group_len >= (UART_MAX_FIFO / 8U)) {
            FCR |= UART_FCR_TET_2_CHAR;
        } else {
            FCR |= UART_FCR_TET_EMPTY;
        }
        rt_hw_cpu_dcache_ops(RT_HW_CACHE_FLUSH | RT_HW_CACHE_INVALIDATE, (void *)uart->tx_data,
                             uart->tx_size);
        dw8250_write32(uart->hw_base, UART_FCR, FCR);
        hal_dma_ch_start(uart->tx_dma, (void *)uart->tx_data, (uint8_t *)uart->hw_base,
                         uart->tx_size);
    }

    return ret;
}

int dw8250_uart_getc_dma(cvi_uart_t *uart, void *rx_buf, rt_size_t size)
{
    RT_ASSERT(uart != RT_NULL);
    hal_dma_ch_config_t config;
    memset(&config, 0, sizeof(hal_dma_ch_config_t));
    int           ret = RT_EOK;
    uint32_t      FCR = UART_FCR_FIFO_EN | UART_FCR_RXSR;
    hal_dma_ch_t *dma = (hal_dma_ch_t *)uart->rx_dma;

    /* Disable rx/tx/ls irq*/
    dw8250_write32(uart->hw_base, UART_IER,
                   dw8250_read32(uart->hw_base, UART_IER) &
                       ~(UART_IER_ERBFI | UART_IER_ETBEI | UART_IER_ELSI));
    uart->rx_data    = (uint8_t *)rx_buf;
    uart->rx_size    = size;
    config.src_inc   = DMA_ADDR_CONSTANT;
    config.dst_inc   = DMA_ADDR_INC;
    config.src_tw    = DMA_DATA_WIDTH_8_BITS;
    config.dst_tw    = DMA_DATA_WIDTH_8_BITS;
    config.group_len = 2;  // find_max_prime_num(size);  test param
    config.trans_dir = DMA_DEV2MEM;
    config.handshake = 0;  // FIXME:
    ret              = hal_dma_ch_config(dma, &config);

    if (ret == RT_EOK) {
        FCR &= ~(UART_FCR_RCVRT_2_LTF);

        if (config.group_len >= (UART_MAX_FIFO / 2U)) {
            FCR |= UART_FCR_RCVRT_1_2_FULL;
        } else if (config.group_len >= (UART_MAX_FIFO / 4U)) {
            FCR |= UART_FCR_RCVRT_1_4_FULL;
        } else {
            FCR |= UART_FCR_RCVRT_1_CHAR;
        }
        rt_hw_cpu_dcache_ops(RT_HW_CACHE_FLUSH | RT_HW_CACHE_INVALIDATE, (void *)uart->rx_data,
                             uart->rx_size);
        dw8250_write32(uart->hw_base, UART_FCR, FCR);
        hal_dma_ch_start(uart->rx_dma, (uint8_t *)uart->hw_base, (void *)uart->rx_data,
                         uart->rx_size);
    }

    return ret;
}

static rt_err_t dw8250_uart_alloc_txrx_td(cvi_uart_t *uart, void *arg)
{
    RT_ASSERT(uart != RT_NULL);
    rt_err_t ret = RT_EOK;

    if (arg == RT_DEVICE_FLAG_DMA_TX) {
        LOG_D("DAM alloc TX td\n");
        uart->tx_dma = (hal_dma_ch_t *)rt_malloc(sizeof(hal_dma_ch_t));
        if (uart->tx_dma == NULL) {
            rt_kprintf("rt_malloc tx_dma failed!\n");
            return -1;
        }
        ret                   = hal_dma_ch_alloc(uart->tx_dma, 1, 0);
        uart->tx_dma->parents = uart;
        uart->send            = dw8250_uart_putc_dma;

        if (ret != RT_EOK) {
            rt_kprintf("dma allocate tx channel failed!\n");
            return ret;
        }
        hal_dma_ch_attach_callback(uart->tx_dma, dw8250_uart_dma_event_callback, NULL);
    }
    if (arg == RT_DEVICE_FLAG_DMA_RX) {
        // uart->dev->config.bufsz = 0;
        LOG_D("DAM alloc RX td\n");
        uart->rx_dma = (hal_dma_ch_t *)rt_malloc(sizeof(hal_dma_ch_t));
        if (uart->rx_dma == NULL) {
            rt_kprintf("rt_malloc tx_dma failed!\n");
            return -1;
        }
        ret                   = hal_dma_ch_alloc(uart->rx_dma, 0, 0);
        uart->rx_dma->parents = uart;
        uart->receive         = dw8250_uart_getc_dma;

        if (ret != RT_EOK) {
            rt_kprintf("dma allocate rx channel failed!\n");
            return ret;
        }
        hal_dma_ch_attach_callback(uart->rx_dma, dw8250_uart_dma_event_callback, NULL);
    }
    return ret;
}

static rt_err_t dw8250_uart_free_txrx_td(cvi_uart_t *uart, void *arg)
{
    if (arg == RT_DEVICE_FLAG_DMA_TX) { /* DMA transfer ERROR */
        if ((uart->tx_dma != NULL)) {
            hal_dma_ch_free(uart->tx_dma);
            uart->state.writeable = 0U;
        }
    } else if (arg == RT_DEVICE_FLAG_DMA_RX) { /* DMA transfer complete */
        if ((uart->rx_dma != NULL)) {
            hal_dma_ch_free(uart->rx_dma);

            uart->state.writeable = 0U;
        }
    }
}

static rt_err_t dw8250_uart_config_dma(cvi_uart_t *uart, void *arg)
{
    rt_err_t ret = RT_EOK;

    RT_ASSERT(uart != RT_NULL);

    /* Allocate DMA channel for Uartx*/
    ret = dw8250_uart_alloc_txrx_td(uart, arg);

    return ret;
}

static rt_err_t dw8250_uart_unconfig_dma(cvi_uart_t *uart, void *arg)
{
    rt_err_t ret = RT_EOK;

    RT_ASSERT(uart != RT_NULL);

    /* Allocate DMA channel for Uartx*/
    ret = dw8250_uart_free_txrx_td(uart, arg);

    return ret;
}

static int dw8250_uart_dma_transmit(struct rt_serial_device *serial, rt_uint8_t *buf,
                                    rt_size_t size, int direction)
{
    RT_ASSERT(serial != RT_NULL);

    cvi_uart_t *uart = (cvi_uart_t *)serial->parent.user_data;

    if (direction == RT_SERIAL_DMA_TX) {
        LOG_D("Direction TX using DMA\n");
        dw8250_uart_putc_dma(uart, (void *)buf, size);
    } else if (direction == RT_SERIAL_DMA_RX) {
        LOG_D("Direction RX using DMA\n");
        dw8250_uart_getc_dma(uart, (void *)buf, size);
    } else {
        return -RT_ERROR;
    }

    return 0;
}
#endif

static rt_err_t dw8250_uart_configure(struct rt_serial_device *serial, struct serial_configure *cfg)
{
    rt_ubase_t           base;
    cvi_uart_t          *uart;
    int                  clock_divisor;
    int                  last_ier_state;
    cvi_uart_data_bits_t data_bits;
    cvi_uart_parity_t    parity;
    cvi_uart_stop_bits_t stop_bits;
    cvi_uart_flowctrl_t  flowctrl;
    RT_ASSERT(serial != RT_NULL);
    uart = (cvi_uart_t *)serial->parent.user_data;
    base = uart->hw_base;

    while (!(dw8250_read32(base, UART_LSR) & UART_LSR_TEMT));

    /* Disable all interrupts before setting baud rate */
    last_ier_state = dw8250_read32(base, UART_IER);
    dw8250_write32(base, UART_IER, 0);

    /* initialize serial config to 8bits before writing baudrate */
    dw8250_write32(base, UART_LCR, UART_LCR_WLS_8);

    clock_divisor = DIV_ROUND_CLOSEST(UART_INPUT_CLK, 16 * serial->config.baud_rate);
    dw8250_uart_setbrg(base, clock_divisor);

    /* restore interrupt state */
    dw8250_write32(base, UART_IER, last_ier_state);

    switch (cfg->data_bits) {
    case DATA_BITS_5:
        data_bits = UART_DATA_BITS_5;
        break;
    case DATA_BITS_6:
        data_bits = UART_DATA_BITS_6;
        break;
    case DATA_BITS_7:
        data_bits = UART_DATA_BITS_7;
        break;
    case DATA_BITS_8:
        data_bits = UART_DATA_BITS_8;
        break;
    default:
        data_bits = UART_DATA_BITS_8;
        break;
    }

    switch (cfg->parity) {
    case PARITY_NONE:
        parity = UART_PARITY_NONE;
        break;
    case PARITY_ODD:
        parity = UART_PARITY_ODD;
        break;
    case PARITY_EVEN:
        parity = UART_PARITY_EVEN;
        break;
    default:
        parity = UART_PARITY_NONE;
        break;
    }

    switch (cfg->stop_bits) {
    case STOP_BITS_1:
        stop_bits = UART_STOP_BITS_1;
        break;
    case STOP_BITS_2:
        stop_bits = UART_STOP_BITS_2;
        break;
    default:
        stop_bits = UART_STOP_BITS_1;
        break;
    }

    switch (cfg->flowcontrol) {
    case RT_SERIAL_FLOWCONTROL_NONE:
        flowctrl = UART_FLOWCTRL_NONE;
        break;
    case RT_SERIAL_FLOWCONTROL_CTSRTS:
        flowctrl = UART_FLOWCTRL_RTS_CTS;
        break;
    }

    if (dw8250_uart_format(base, data_bits, parity, stop_bits, flowctrl) != 0) {
        return -RT_ERROR;
    }
    /* Set uart ready */
    dw8250_write32(base, UART_MCR, UART_MCRVAL);
    /* Enable & clear FIFO*/
    dw8250_write32(base, UART_FCR, UART_FCR_DEFVAL);
    return RT_EOK;
}

static rt_err_t dw8250_uart_control(struct rt_serial_device *serial, int cmd, void *arg)
{
    cvi_uart_t *uart;
    rt_err_t    ret = RT_EOK;

    RT_ASSERT(serial != RT_NULL);
    uart = (cvi_uart_t *)serial->parent.user_data;

    switch (cmd) {
    case RT_DEVICE_CTRL_CLR_INT:
        /* Disable rx irq */
#ifdef BSP_USING_DMA
        dw8250_uart_unconfig_dma(uart, arg);
#endif
        dw8250_write32(uart->hw_base, UART_IER, !UART_IER_ERBFI);
        rt_hw_interrupt_mask(uart->irqno);
        break;

    case RT_DEVICE_CTRL_SET_INT:
        /* Enable rx irq */
        dw8250_write32(uart->hw_base, UART_IER, UART_IER_ERBFI);
        rt_hw_interrupt_umask(uart->irqno);
        break;

    case RT_DEVICE_CTRL_CONFIG:
        /* Configure flow control */
#ifdef BSP_USING_DMA
        dw8250_uart_config_dma(uart, arg);
#endif
        break;
    }

    return ret;
}

static int dw8250_uart_putc(struct rt_serial_device *serial, char c)
{
    rt_ubase_t  base;
    cvi_uart_t *uart;

    RT_ASSERT(serial != RT_NULL);
    uart = (cvi_uart_t *)serial->parent.user_data;
    base = uart->hw_base;

    while ((dw8250_read32(base, UART_LSR) & BOTH_EMPTY) != BOTH_EMPTY);

    dw8250_write32(base, UART_TX, c);

    return 1;
}

static int dw8250_uart_getc(struct rt_serial_device *serial)
{
    int         ch = -1;
    rt_ubase_t  base;
    cvi_uart_t *uart;

    RT_ASSERT(serial != RT_NULL);
    uart = (cvi_uart_t *)serial->parent.user_data;
    base = uart->hw_base;

    if (dw8250_read32(base, UART_LSR) & UART_LSR_DR) {
        ch = dw8250_read32(base, UART_RX) & 0xff;
    }

    return ch;
}

static const struct rt_uart_ops _uart_ops = {
    dw8250_uart_configure,    dw8250_uart_control, dw8250_uart_putc, dw8250_uart_getc,
#ifdef BSP_USING_DMA
    dw8250_uart_dma_transmit,
#endif
};

static void rt_hw_uart_isr(int irqno, void *param)
{
    unsigned int             iir, status;
    struct rt_serial_device *serial = (struct rt_serial_device *)param;
    cvi_uart_t              *uart   = (cvi_uart_t *)serial->parent.user_data;

    iir = dw8250_read32(uart->hw_base, UART_IIR);

    /* If don't do this in non-DMA mode then the "RX TIMEOUT" interrupt will fire forever. */
    if ((iir & 0x3f) == UART_IIR_RX_TIMEOUT) {
        status = dw8250_read32(uart->hw_base, UART_LSR);

        if (!(status & (UART_LSR_DR | UART_LSR_BI))) {
            dw8250_read32(uart->hw_base, UART_RX);
        }
    }

    if (!(iir & UART_IIR_NO_INT)) {
        rt_hw_serial_isr(serial, RT_SERIAL_EVENT_RX_IND);
    }

    if ((iir & UART_IIR_BUSY) == UART_IIR_BUSY) {
        /* Clear the USR */
        dw8250_read32(uart->hw_base, UART_USR);

        return;
    }
}

#ifdef BSP_USING_DMA
#define BSP_INSTALL_UART_DEVICE(no)                                                             \
    uart               = &_uart##no##_device;                                                   \
    _serial##no.ops    = &_uart_ops;                                                            \
    _serial##no.config = config;                                                                \
    rt_hw_serial_register(&_serial##no, "uart" #no,                                             \
                          RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_DMA_TX | \
                              RT_DEVICE_FLAG_DMA_RX,                                            \
                          uart);                                                                \
    rt_hw_interrupt_install(uart->irqno, rt_hw_uart_isr, &_serial##no, "uart" #no);             \
    uart->dev = &_serial##no;
#else
#define BSP_INSTALL_UART_DEVICE(no)                                                              \
    uart               = &_uart##no##_device;                                                    \
    _serial##no.ops    = &_uart_ops;                                                             \
    _serial##no.config = config;                                                                 \
    rt_hw_serial_register(&_serial##no, "uart" #no, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX, \
                          uart);                                                                 \
    rt_hw_interrupt_install(uart->irqno, rt_hw_uart_isr, &_serial##no, "uart" #no);              \
    uart->dev = &_serial##no;
#endif

int rt_hw_uart_init(void)
{
    cvi_uart_t             *uart;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

#ifdef BSP_USING_UART0
    BSP_INSTALL_UART_DEVICE(0);
#if defined(ARCH_ARM)
    uart->hw_base = (rt_size_t)rt_ioremap((void *)uart->hw_base, 0x10000);
#endif /* defined(ARCH_ARM) */
#endif

#ifdef BSP_USING_UART1
    BSP_INSTALL_UART_DEVICE(1);
#if defined(ARCH_ARM)
    uart->hw_base = (rt_size_t)rt_ioremap((void *)uart->hw_base, 0x10000);
#endif /* defined(ARCH_ARM) */
#endif

#ifdef BSP_USING_UART2
    BSP_INSTALL_UART_DEVICE(2);
#if defined(ARCH_ARM)
    uart->hw_base = (rt_size_t)rt_ioremap((void *)uart->hw_base, 0x10000);
#endif /* defined(ARCH_ARM) */
#endif

#ifdef BSP_USING_UART3
    BSP_INSTALL_UART_DEVICE(3);
#if defined(ARCH_ARM)
    uart->hw_base = (rt_size_t)rt_ioremap((void *)uart->hw_base, 0x10000);
#endif /* defined(ARCH_ARM) */
#endif

#ifdef BSP_USING_UART4
    BSP_INSTALL_UART_DEVICE(4);
#if defined(ARCH_ARM)
    uart->hw_base = (rt_size_t)rt_ioremap((void *)uart->hw_base, 0x10000);
#endif /* defined(ARCH_ARM) */
#endif

    return 0;
}

/**
 * @brief Dump UART registers for debugging.
 */
void csi_uart_dump_registers(cvi_uart_t *uart)
{
    if (!uart) {
        rt_kprintf("  [UART REG DUMP] Error: UART handle is NULL.\n");
        return;
    }

    // Note: FCR is write-only, we read IIR to check the interrupt status.
    // Read and print the values of key registers
    uint32_t ier = dw8250_read32(uart->hw_base, UART_IER);
    uint32_t iir = dw8250_read32(uart->hw_base, UART_IIR);
    uint32_t lcr = dw8250_read32(uart->hw_base, UART_LCR);
    uint32_t lsr = dw8250_read32(uart->hw_base, UART_LSR);
    uint32_t usr = dw8250_read32(uart->hw_base, UART_USR);
    rt_kprintf("  [UART REG DUMP] IER=0x%02x, IIR=0x%02x, LCR=0x%02x, LSR=0x%02x, USR=0x%02x\n",
               ier, iir, lcr, lsr, usr);

    // Decode the key bits of LSR (Line Status Register)
    rt_kprintf(
        "    - LSR Decode: DR(DataReady)=%d, OE(Overrun)=%d, PE(Parity)=%d, FE(Framing)=%d, "
        "BI(Break)=%d, THRE(TXEmpty)=%d, TEMT(TXAllEmpty)=%d\n",
        (lsr & 0x01) ? 1 : 0, (lsr & 0x02) ? 1 : 0, (lsr & 0x04) ? 1 : 0, (lsr & 0x08) ? 1 : 0,
        (lsr & 0x10) ? 1 : 0, (lsr & 0x20) ? 1 : 0, (lsr & 0x40) ? 1 : 0);

    // Decode the key bits of USR (UART Status Register)
    rt_kprintf(
        "    - USR Decode: BUSY=%d, TFNF(TXNotFull)=%d, TFE(TXEmpty)=%d, RFNE(RXNotEmpty)=%d, "
        "RFF(RXFull)=%d\n",
        (usr & 0x01) ? 1 : 0, (usr & 0x02) ? 1 : 0, (usr & 0x04) ? 1 : 0, (usr & 0x08) ? 1 : 0,
        (usr & 0x10) ? 1 : 0);
}