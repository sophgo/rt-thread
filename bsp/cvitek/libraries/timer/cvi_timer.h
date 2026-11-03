/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2024/08/08     ShichengChu    first version
 */
#ifndef __CVI_TIMER_H__
#define __CVI_TIMER_H__

#include <rtthread.h>

/*! Timer1 Control Reg,     offset: 0x08 */
#define DW_TIMER_CTL_ENABLE_SEL_Pos (0U)
#define DW_TIMER_CTL_ENABLE_SEL_Msk (0x1U << DW_TIMER_CTL_ENABLE_SEL_Pos)
#define DW_TIMER_CTL_ENABLE_SEL_EN  DW_TIMER_CTL_ENABLE_SEL_Msk

#define DW_TIMER_CTL_MODE_SEL_Pos (1U)
#define DW_TIMER_CTL_MODE_SEL_Msk (0x1U << DW_TIMER_CTL_MODE_SEL_Pos)
#define DW_TIMER_CTL_MODE_SEL_EN  DW_TIMER_CTL_MODE_SEL_Msk

#define DW_TIMER_CTL_INT_MASK_Pos (2U)
#define DW_TIMER_CTL_INT_MASK_Msk (0x1U << DW_TIMER_CTL_INT_MASK_Pos)
#define DW_TIMER_CTL_INT_MAKS_EN  DW_TIMER_CTL_INT_MASK_Msk

#define DW_TIMER_CTL_HARD_TRIG_Pos (4U)
#define DW_TIMER_CTL_HARD_TRIG_Msk (0x1U << DW_TIMER_CTL_HARD_TRIG_Pos)
#define DW_TIMER_CTL_HARD_TRIG_EN  DW_TIMER_CTL_HARD_TRIG_Msk

/*! Timer EOI,            offset: 0x0c */
#define DW_TIMER_EOI_REG_Pos (0U)
#define DW_TIMER_EOI_REG_Msk (0x1U << DW_TIMER_EOI_REG_Pos)
#define DW_TIMER_EOI_REG_EN  DW_TIMER_EOI_REG_Msk

/*! Timer Int Status,     offset: 0x10 */
#define DW_TIMER_INT_STATUS_Pos (0U)
#define DW_TIMER_INT_STATUS_Msk (0x1U << DW_TIMER_INT_STATUS_Pos)
#define DW_TIMER_INT_STATUS_EN  DW_TIMER_INT_STATUS_Msk

/*! Timers Int Status,    offset: 0xa0 */
#define DW_TIMERS_INT_STATUS_Pos (0U)
#define DW_TIMERS_INT_STATUS_Msk (0x2U << DW_TIMERS_INT_STATUS_Pos)
#define DW_TIMERS_INT_STATUS_EN  DW_TIMERS_INT_STATUS_Msk

/*! Timers EOI,           offset: 0xa4 */
#define DW_TIMERS_EOI_REG_Pos (0U)
#define DW_TIMERS_EOI_REG_Msk (0x2U << DW_TIMERS_EOI_REG_Pos)
#define DW_TIMERS_EOI_REG_EN  DW_TIMERS_EOI_REG_Msk

/*! Timers Raw Int Status,offset: 0xa8 */
#define DW_TIMERS_RAW_INT_STA_Pos (0U)
#define DW_TIMERS_RAW_INT_STA_Msk (0x2U << DW_TIMERS_RAW_INT_STA_Pos)
#define DW_TIMERS_RAW_INT_STA_EN  DW_TIMERS_RAW_INT_STA_Msk

typedef struct {
    volatile uint32_t       TLC;  /* Offset: 0x000 (R/W) TimerLoadCount */
    volatile const uint32_t TCV;  /* Offset: 0x004 (R/ ) TimerCurrentValue */
    volatile uint32_t       TCR;  /* Offset: 0x008 (R/W) TimerControlReg */
    volatile const uint32_t TEOI; /* Offset: 0x00c (R/ ) TimerEOI */
    volatile const uint32_t TIS;  /* Offset: 0x010 (R/ ) TimerIntStatus */
} dw_timer_regs_t;

typedef struct {
    dw_timer_regs_t         timer[8];
    volatile const uint32_t TSIS;  /* Offset: 0x0a0 (R/ ) TimersIntStatus */
    volatile const uint32_t TSEOI; /* Offset: 0x0a4 (R/ ) TimersEOI */
    volatile const uint32_t TSRIS; /* Offset: 0x0a8 (R/ ) TimersRawIntStatus */
} dw_timer_general_regs_t;

uint32_t hal_timer_read_load(dw_timer_regs_t *timer_base);
void hal_timer_write_load(dw_timer_regs_t *timer_base, uint32_t value);
uint32_t hal_timer_get_current(dw_timer_regs_t *timer_base);
void hal_timer_set_enable(dw_timer_regs_t *timer_base);
void hal_timer_set_disable(dw_timer_regs_t *timer_base);
uint32_t hal_timer_get_enable(dw_timer_regs_t *timer_base);
void hal_timer_set_mode_free(dw_timer_regs_t *timer_base);
void hal_timer_set_mode_load(dw_timer_regs_t *timer_base);
uint32_t hal_timer_get_model(dw_timer_regs_t *timer_base);
void hal_timer_set_mask(dw_timer_regs_t *timer_base);
void hal_timer_set_unmask(dw_timer_regs_t *timer_base);
uint32_t hal_timer_get_mask(dw_timer_regs_t *timer_base);
void hal_timer_set_hardtrigger_en(dw_timer_regs_t *timer_base);
void hal_timer_set_hardtrigger_dis(dw_timer_regs_t *timer_base);
uint32_t hal_timer_get_hardtrigger(dw_timer_regs_t *timer_base);
uint32_t hal_timer_clear_irq(dw_timer_regs_t *timer_base);
uint32_t hal_timer_get_int_status(dw_timer_regs_t *timer_base);
void hal_timer_reset_register(dw_timer_regs_t *timer_base);
uint32_t hal_timer_general_active_after_mask(dw_timer_general_regs_t *timer_base);
uint32_t hal_timer_general_clear_irq(dw_timer_general_regs_t *timer_base);
uint32_t hal_timer_general_active_prior_mask(dw_timer_general_regs_t *timer_base);

#endif /* __CVI_TIMER_H__ */
