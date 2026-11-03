/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023/06/25     flyingcys    first version
 */
#ifndef __DW_UART_H__
#define __DW_UART_H__

#include <rthw.h>
#include <rtthread.h>

#define UART_REG_SHIFT 0x2 /* Register Shift*/
#define UART_INPUT_CLK 25000000

#define UART0_BASE 0x04140000
#define UART1_BASE 0x04150000
#define UART2_BASE 0x04160000
#define UART3_BASE 0x04170000
#define UART4_BASE 0x041C0000

/*
 * The Synopsys DesignWare 8250 has an extra feature whereby it detects if the
 * LCR is written whilst busy. If it is, then a busy detect interrupt is
 * raised, the LCR needs to be rewritten and the uart status register read.
 */

#define UART_RX 0 /* In: Receive buffer */
#define UART_TX 0 /* Out: Transmit buffer */

#define UART_DLL 0 /* Out: Divisor Latch Low */
#define UART_DLM 1 /* Out: Divisor Latch High */

#define UART_IER        1    /* Out: Interrupt Enable Register */
#define UART_IER_ERBFI  0x01 /* Enable Received Data Available Interrupt. */
#define UART_IER_ETBEI  0x02 /* Enable Transmit Holding Register Empty Interrupt */
#define UART_IER_ELSI   0x04 /* Enable Line Status Interrupt */
#define UART_IERE_EDSSI 0x08 /* Enable Modem Status Interrupt. */

#define UART_SSR 0x22 /* In: Software Reset Register */

#define UART_IIR            2    /* In: Interrupt ID Register */
#define UART_IIR_NO_INT     0x01 /* No interrupts pending */
#define UART_IIR_BUSY       0x07 /* DesignWare APB Busy Detect */
#define UART_IIR_RX_TIMEOUT 0x0c /* OMAP RX Timeout interrupt */

#define UART_FCR                2                        /* Out: FIFO Control Register */
#define UART_FCR_FIFO_EN        0x01                     /* Fifo enable */
#define UART_FCR_RXSR           0x02                     /* Receiver soft reset */
#define UART_FCR_TXSR           0x04                     /* Transmitter soft reset */
#define UART_FCR_DMA_EN         0x08                     /* DMA Mode enable */
#define UART_FCR_TET            0x10                     /* TX Empty Trigger */
#define UART_FCR_TET_EMPTY      (0x00 << UART_FCR_TET)   /* FIFO empty */
#define UART_FCR_TET_2_CHAR     (0x01 << UART_FCR_TET)   /* 2 characters in the FIFO */
#define UART_FCR_TET_1_4_FULL   (0x02 << UART_FCR_TET)   /* 1/4 full */
#define UART_FCR_TET_1_2_FULL   (0x03 << UART_FCR_TET)   /* 1/2 full */
#define UART_FCR_RCVRT          0x40                     /* RCVR Trigger */
#define UART_FCR_RCVRT_1_CHAR   (0x00 << UART_FCR_RCVRT) /* 1 character in the FIFO */
#define UART_FCR_RCVRT_1_4_FULL (0x01 << UART_FCR_RCVRT) /* 1/4 full in the FIFO */
#define UART_FCR_RCVRT_1_2_FULL (0x02 << UART_FCR_RCVRT) /* 1/2 full in the FIFO */
#define UART_FCR_RCVRT_2_LTF    (0x03 << UART_FCR_RCVRT) /*  FIFO 2 less than full */

#define UART_LCR         3    /* Out: Line Control Register */
#define UART_LCR_WLS_MSK 0x03 /* character length select mask */
#define UART_LCR_WLS_5   0x00 /* 5 bit character length */
#define UART_LCR_WLS_6   0x01 /* 6 bit character length */
#define UART_LCR_WLS_7   0x02 /* 7 bit character length */
#define UART_LCR_WLS_8   0x03 /* 8 bit character length */
#define UART_LCR_STB     0x04 /* # stop Bits, off=1, on=1.5 or 2) */
#define UART_LCR_PEN     0x08 /* Parity eneble */
#define UART_LCR_EPS     0x10 /* Even Parity Select */
#define UART_LCR_STKP    0x20 /* Stick Parity */
#define UART_LCR_SBRK    0x40 /* Set Break */
#define UART_LCR_BKSE    0x80 /* Bank select enable */
#define UART_LCR_DLAB    0x80 /* Divisor latch access bit */

#define UART_MCR          4    /* Out: Modem Control Register */
#define UART_MCR_DTR      0x01 /* DTR   */
#define UART_MCR_RTS      0x02 /* RTS   */
#define UART_MCR_OUT1     0x04 /* OUT1  */
#define UART_MCR_OUT2     0x08 /* OUT2  */
#define UART_MCR_LOOPBACK 0x10 /* Loopback mode */
#define UART_MCR_AFCE     0x20 /* Auto flow control enable */

#define UART_LSR      5    /* In: Line Status Register */
#define UART_LSR_BI   0x10 /* Break interrupt indicator */
#define UART_LSR_DR   0x01 /* Receiver data ready */
#define UART_LSR_TEMT 0x40 /* Transmitter empty */
#define UART_LSR_THRE 0x20 /* Transmit-hold-register empty */

#define UART_USR      0x1f /* UART Status Register (RO)*/
#define UART_USR_BUSY 0x01 /* UART Busy */
#define UART_USR_TFNF 0x02 /* Transmit FIFO Not Full */
#define UART_USR_TFE  0x04 /* Transmit FIFO Empty */
#define UART_USR_RFNE 0x08 /* Receive FIFO Not Empty */
#define UART_USR_RFF  0x10 /* Receive FIFO Full */

#define UART_MCRVAL (UART_MCR_DTR | UART_MCR_RTS) /* RTS/DTR */

/* Clear & enable FIFOs */
#define UART_FCR_DEFVAL (UART_FCR_FIFO_EN | UART_FCR_RXSR | UART_FCR_TXSR)

#define UART_MAX_FIFO 0x10U
#define UART_BUSY_TIMEOUT 0x70000000U

/*----- UART Control Codes: Mode Parameters: Data Bits -----*/
typedef enum {
    UART_DATA_BITS_5 = 0,  ///< 5 Data bits
    UART_DATA_BITS_6,      ///< 6 Data bit
    UART_DATA_BITS_7,      ///< 7 Data bits
    UART_DATA_BITS_8,      ///< 8 Data bits (default)
    UART_DATA_BITS_9       ///< 9 Data bits
} cvi_uart_data_bits_t;

/*----- UART Control Codes: Mode Parameters: Parity -----*/
typedef enum {
    UART_PARITY_NONE = 0,  ///< No Parity (default)
    UART_PARITY_EVEN,      ///< Even Parity
    UART_PARITY_ODD,       ///< Odd Parity
} cvi_uart_parity_t;

/*----- UART Control Codes: Mode Parameters: Stop Bits -----*/
typedef enum {
    UART_STOP_BITS_1 = 0,  ///< 1 Stop bit (default)
    UART_STOP_BITS_2,      ///< 2 Stop bits
    UART_STOP_BITS_1_5,    ///< 1.5 Stop bits
} cvi_uart_stop_bits_t;

/*----- UART Control Codes: Mode Parameters: Flow Control -----*/
typedef enum {
    UART_FLOWCTRL_NONE = 0,  ///< none flowctrl
    UART_FLOWCTRL_RTS,       ///< RTS
    UART_FLOWCTRL_CTS,       ///< CTS
    UART_FLOWCTRL_RTS_CTS    ///< RTS & CTS
} cvi_uart_flowctrl_t;

rt_inline rt_uint32_t dw8250_read32(rt_ubase_t addr, rt_ubase_t offset)
{
    return *((volatile rt_uint32_t *)(addr + (offset << UART_REG_SHIFT)));
}

rt_inline void dw8250_write32(rt_ubase_t addr, rt_ubase_t offset, rt_uint32_t value)
{
    *((volatile rt_uint32_t *)(addr + (offset << UART_REG_SHIFT))) = value;

    if (offset == UART_LCR) {
        int tries = 1000;

        /* Make sure LCR write wasn't ignored */
        while (tries--) {
            unsigned int lcr = dw8250_read32(addr, UART_LCR);

            if ((value & ~UART_LCR_STKP) == (lcr & ~UART_LCR_STKP)) {
                return;
            }

            dw8250_write32(addr, UART_FCR, UART_FCR_DEFVAL);
            dw8250_read32(addr, UART_RX);

            *((volatile rt_uint32_t *)(addr + (offset << UART_REG_SHIFT))) = value;
        }
    }
}

void dw8250_uart_setbrg(rt_ubase_t addr, int baud_divisor);
rt_err_t dw8250_uart_format(rt_ubase_t base, cvi_uart_data_bits_t data_bits,
                            cvi_uart_parity_t parity, cvi_uart_stop_bits_t stop_bits,
                            cvi_uart_flowctrl_t flowctrl);

#endif /* __DW_UART_H__ */
