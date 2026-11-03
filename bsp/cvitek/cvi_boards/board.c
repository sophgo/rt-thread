 /*
 * Copyright (C) 2017-2024 Alibaba Group Holding Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <board.h>
#include <unistd.h>
#include <rthw.h>
#include <rtthread.h>
#include <drv_uart.h>

extern unsigned long __heap_start;
extern unsigned long __heap_end;

void rt_hw_board_init(void)
{
    /* initalize interrupt */
    rt_hw_interrupt_init();

#ifdef RT_USING_HEAP
    /* initialize memory system */
    rt_system_heap_init((void *)&__heap_start, (void *)&__heap_end);
#endif

#ifdef RT_USING_SERIAL
    rt_hw_uart_init();
#endif
    /* Set the shell console output device */
#if defined(RT_USING_CONSOLE) && defined(RT_USING_DEVICE)
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif

#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif
}


