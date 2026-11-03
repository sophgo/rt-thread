/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023/06/25     flyingcys    first version
 */
#include "dw_uart.h"

#include <rtthread.h>

#define DBG_TAG "DRV.UART"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

static rt_int32_t dw8250_wait_idle(rt_ubase_t base)
{
    rt_uint32_t timecount = 0U;
    rt_int32_t  ret       = 0;

    while ((dw8250_read32(base, UART_USR) & UART_USR_BUSY) && (timecount < UART_BUSY_TIMEOUT)) {
        timecount++;
    }

    if (timecount >= UART_BUSY_TIMEOUT) {
        ret = -1;
    }

    return ret;
}

static rt_int32_t dw8250_uart_wait_timeout(rt_ubase_t base)
{
    rt_uint32_t timecount = 0U;
    rt_int32_t  ret       = 0;

    while ((dw8250_read32(base, UART_LSR) & 0x81U) || (dw8250_read32(base, UART_USR) & 0x1U)) {
        dw8250_read32(base, UART_LSR);
        dw8250_read32(base, UART_RX);
        timecount++;

        if (timecount >= UART_BUSY_TIMEOUT) {
            ret = -1;
            break;
        }
    }

    if (ret == 0) {
        ret = dw8250_wait_idle(base);
    }

    return ret;
}

static void dw8250_enable_auto_flow_control(rt_ubase_t base)
{
    rt_uint32_t MCR = dw8250_read32(base, UART_MCR);
    dw8250_write32(base, UART_MCR, MCR | UART_MCR_AFCE | UART_MCR_RTS);
}

static rt_int32_t dw8250_uart_config_parity_none(rt_ubase_t base)
{
    rt_int32_t  ret;
    rt_uint32_t LCR;

    ret = dw8250_uart_wait_timeout(base);

    if (ret == 0) {
        LCR = dw8250_read32(base, UART_LCR);
        LCR &= ~UART_LCR_PEN;
        dw8250_write32(base, UART_LCR, LCR);
    }

    return ret;
}

static rt_int32_t dw8250_uart_config_parity_odd(rt_ubase_t base)
{
    rt_int32_t  ret;
    rt_uint32_t LCR;

    ret = dw8250_uart_wait_timeout(base);

    if (ret == 0) {
        LCR = dw8250_read32(base, UART_LCR);
        LCR &= ~(UART_LCR_EPS);
        LCR |= UART_LCR_PEN;
        dw8250_write32(base, UART_LCR, LCR);
    }

    return ret;
}

static rt_int32_t dw8250_uart_config_parity_even(rt_ubase_t base)
{
    rt_int32_t  ret;
    rt_uint32_t LCR;

    ret = dw8250_uart_wait_timeout(base);

    if (ret == 0) {
        LCR = dw8250_read32(base, UART_LCR) | UART_LCR_PEN | UART_LCR_EPS;
        dw8250_write32(base, UART_LCR, LCR);
    }

    return ret;
}

static rt_int32_t dw8250_uart_config_data_bits(rt_ubase_t base, rt_uint32_t data_bits)
{
    rt_int32_t  ret;
    rt_uint32_t LCR;

    ret = dw8250_uart_wait_timeout(base);

    if (ret == 0) {
        LCR = dw8250_read32(base, UART_LCR);
        LCR &= 0xFCU;
        LCR |= (data_bits - 5U);
        dw8250_write32(base, UART_LCR, LCR);
    }

    return ret;
}

static rt_int32_t dw8250_uart_config_stop_bits(rt_ubase_t base, rt_uint32_t stop_bits)
{
    rt_int32_t  ret;
    rt_uint32_t LCR;

    ret = dw8250_uart_wait_timeout(base);

    if (ret == RT_EOK) {
        if (stop_bits == 1U) {
            LCR = dw8250_read32(base, UART_LCR);
            LCR &= ~UART_LCR_STB;
            dw8250_write32(base, UART_LCR, LCR);
        } else if (stop_bits == 2U) {
            LCR = dw8250_read32(base, UART_LCR);
            LCR |= UART_LCR_STB;
            dw8250_write32(base, UART_LCR, LCR);
        }
    }

    return ret;
}

void dw8250_uart_setbrg(rt_ubase_t addr, int baud_divisor)
{
    /* to keep serial format, read lcr before writing BKSE */
    int lcr_val = dw8250_read32(addr, UART_LCR) & ~UART_LCR_BKSE;

    dw8250_write32(addr, UART_LCR, UART_LCR_BKSE | lcr_val);
    dw8250_write32(addr, UART_DLL, baud_divisor & 0xff);

    dw8250_write32(addr, UART_DLM, (baud_divisor >> 8) & 0xff);
    dw8250_write32(addr, UART_LCR, lcr_val);
}

static rt_err_t cvi_uart_flowctrl(rt_ubase_t base, cvi_uart_flowctrl_t flowctrl)
{
    rt_err_t ret = RT_EOK;

    switch (flowctrl) {
    case UART_FLOWCTRL_CTS:
        dw8250_wait_idle(base);
        dw8250_enable_auto_flow_control(base);
        break;

    case UART_FLOWCTRL_RTS_CTS:
        dw8250_wait_idle(base);
        dw8250_enable_auto_flow_control(base);
        break;

    case UART_FLOWCTRL_NONE:
        dw8250_wait_idle(base);
        break;

    case UART_FLOWCTRL_RTS:
    default:
        ret = RT_ERROR;
        LOG_E("Unsupported flow control mode: %d", flowctrl);
        break;
    }

    return ret;
}

rt_err_t dw8250_uart_format(rt_ubase_t base, cvi_uart_data_bits_t data_bits,
                            cvi_uart_parity_t parity, cvi_uart_stop_bits_t stop_bits,
                            cvi_uart_flowctrl_t flowctrl)
{
    rt_err_t ret = RT_EOK;

    switch (data_bits) {
    case UART_DATA_BITS_5:
        ret = dw8250_uart_config_data_bits(base, 5U);
        break;

    case UART_DATA_BITS_6:
        ret = dw8250_uart_config_data_bits(base, 6U);
        break;

    case UART_DATA_BITS_7:
        ret = dw8250_uart_config_data_bits(base, 7U);
        break;

    case UART_DATA_BITS_8:
        ret = dw8250_uart_config_data_bits(base, 8U);
        break;

    default:
        ret = -1;
        break;
    }

    if (ret == 0) {
        switch (parity) {
        case UART_PARITY_NONE:
            ret = dw8250_uart_config_parity_none(base);
            break;

        case UART_PARITY_ODD:
            ret = dw8250_uart_config_parity_odd(base);
            break;

        case UART_PARITY_EVEN:
            ret = dw8250_uart_config_parity_even(base);
            break;

        default:
            ret = -1;
            break;
        }

        if (ret == 0) {
            switch (stop_bits) {
            case UART_STOP_BITS_1:
                ret = dw8250_uart_config_stop_bits(base, 1U);
                break;

            case UART_STOP_BITS_2:
                ret = dw8250_uart_config_stop_bits(base, 2U);
                break;

            case UART_STOP_BITS_1_5:
                if (data_bits == UART_DATA_BITS_5) {
                    ret = dw8250_uart_config_stop_bits(base, 2U);
                    break;
                }

            default:
                ret = -1;
                break;
            }
        }

        if (ret == 0) {
            ret = cvi_uart_flowctrl(base, flowctrl);
        }
    }

    return ret;
}
