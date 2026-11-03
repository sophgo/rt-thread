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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <core/core_rv64.h>
#include <core/csi_rv64_gcc.h>
#include <rtdef.h>

void (*trap_c_callback)(void);

struct exception_stack_frame
{
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t x4;
    uint64_t x5;
    uint64_t x6;
    uint64_t x7;
    uint64_t x8;
    uint64_t x9;
    uint64_t x10;
    uint64_t x11;
    uint64_t x12;
    uint64_t x13;
    uint64_t x14;
    uint64_t x15;
    uint64_t x16;
    uint64_t x17;
    uint64_t x18;
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29;
    uint64_t x30;
    uint64_t x31;
};

static void print_stack_frame(uintptr_t * sp)
{
    struct exception_stack_frame * esf = (struct exception_stack_frame *)(sp);

    rt_kprintf("\n=================================================================\n");
    rt_kprintf("x1 (ra   : Return address                ) ==> 0x%016lX\n", esf->x1);
    rt_kprintf("x2 (sp   : Stack pointer                 ) ==> 0x%016lX\n", esf->x2);
    rt_kprintf("x3 (gp   : Global pointer                ) ==> 0x%016lX\n", esf->x3);
    rt_kprintf("x4 (tp   : Thread pointer                ) ==> 0x%016lX\n", esf->x4);
    rt_kprintf("x5 (t0   : Temporary                     ) ==> 0x%016lX\n", esf->x5);
    rt_kprintf("x6 (t1   : Temporary                     ) ==> 0x%016lX\n", esf->x6);
    rt_kprintf("x7 (t2   : Temporary                     ) ==> 0x%016lX\n", esf->x7);
    rt_kprintf("x8 (s0/fp: Save register,frame pointer   ) ==> 0x%016lX\n", esf->x8);
    rt_kprintf("x9 (s1   : Save register                 ) ==> 0x%016lX\n", esf->x9);
    rt_kprintf("x10(a0   : Function argument,return value) ==> 0x%016lX\n", esf->x10);
    rt_kprintf("x11(a1   : Function argument,return value) ==> 0x%016lX\n", esf->x11);
    rt_kprintf("x12(a2   : Function argument             ) ==> 0x%016lX\n", esf->x12);
    rt_kprintf("x13(a3   : Function argument             ) ==> 0x%016lX\n", esf->x13);
    rt_kprintf("x14(a4   : Function argument             ) ==> 0x%016lX\n", esf->x14);
    rt_kprintf("x15(a5   : Function argument             ) ==> 0x%016lX\n", esf->x15);
    rt_kprintf("x16(a6   : Function argument             ) ==> 0x%016lX\n", esf->x16);
    rt_kprintf("x17(a7   : Function argument             ) ==> 0x%016lX\n", esf->x17);
    rt_kprintf("x18(s2   : Save register                 ) ==> 0x%016lX\n", esf->x18);
    rt_kprintf("x19(s3   : Save register                 ) ==> 0x%016lX\n", esf->x19);
    rt_kprintf("x20(s4   : Save register                 ) ==> 0x%016lX\n", esf->x20);
    rt_kprintf("x21(s5   : Save register                 ) ==> 0x%016lX\n", esf->x21);
    rt_kprintf("x22(s6   : Save register                 ) ==> 0x%016lX\n", esf->x22);
    rt_kprintf("x23(s7   : Save register                 ) ==> 0x%016lX\n", esf->x23);
    rt_kprintf("x24(s8   : Save register                 ) ==> 0x%016lX\n", esf->x24);
    rt_kprintf("x25(s9   : Save register                 ) ==> 0x%016lX\n", esf->x25);
    rt_kprintf("x26(s10  : Save register                 ) ==> 0x%016lX\n", esf->x26);
    rt_kprintf("x27(s11  : Save register                 ) ==> 0x%016lX\n", esf->x27);
    rt_kprintf("x28(t3   : Temporary                     ) ==> 0x%016lX\n", esf->x28);
    rt_kprintf("x29(t4   : Temporary                     ) ==> 0x%016lX\n", esf->x29);
    rt_kprintf("x30(t5   : Temporary                     ) ==> 0x%016lX\n", esf->x30);
    rt_kprintf("x31(t6   : Temporary                     ) ==> 0x%016lX\n", esf->x31);
    rt_kprintf("=================================================================\n");
}

#if 0
#define CAUSE_MISALIGNED_FETCH 0x0
#define CAUSE_FAULT_FETCH 0x1
#define CAUSE_ILLEGAL_INSTRUCTION 0x2
#define CAUSE_BREAKPOINT 0x3
#define CAUSE_MISALIGNED_LOAD 0x4
#define CAUSE_FAULT_LOAD 0x5
#define CAUSE_MISALIGNED_STORE 0x6
#define CAUSE_FAULT_STORE 0x7
#define CAUSE_USER_ECALL 0x8
#define CAUSE_SUPERVISOR_ECALL 0x9
#define CAUSE_HYPERVISOR_ECALL 0xa
#define CAUSE_MACHINE_ECALL 0xb
void print_exception_cause(unsigned long cause)
{
    rt_kprintf("\nException: ");
    switch (cause)
        {
            case CAUSE_MISALIGNED_FETCH:
                rt_kprintf("Instruction address misaligned.\n");
                break;
            case CAUSE_FAULT_FETCH:
                rt_kprintf("Instruction access fault.\n");
                break;
            case CAUSE_ILLEGAL_INSTRUCTION:
                rt_kprintf("Illegal instruction.\n");
                break;
            case CAUSE_BREAKPOINT:
                rt_kprintf("Breakpoint.\n");
                break;
            case CAUSE_MISALIGNED_LOAD:
                rt_kprintf("Load address misaligned.\n");
                break;
            case CAUSE_FAULT_LOAD:
                rt_kprintf("Load access fault.\n");
                break;
            case CAUSE_MISALIGNED_STORE:
                rt_kprintf("Store address misaligned.\n");
                break;
            case CAUSE_FAULT_STORE:
                rt_kprintf("Store access fault.\n");
                break;
            case CAUSE_USER_ECALL:
                rt_kprintf("Environment call from U-mode.\n");
                break;
            case CAUSE_SUPERVISOR_ECALL:
                rt_kprintf("Environment call from S-mode.\n");
                break;
            case CAUSE_HYPERVISOR_ECALL:
                rt_kprintf("Environment call from H-mode.\n");
                break;
            case CAUSE_MACHINE_ECALL:
                rt_kprintf("Environment call from M-mode.\n");
                break;
            default:
                rt_kprintf("Uknown exception : %08lX\n", cause);
                break;
        }
}
#endif
void trap_c(uintptr_t *regs)
{
    int i;
    unsigned long vec = 0;
    rt_thread_t tid = RT_NULL;
    rt_kprintf("++--------------------------- Exception ----------------------------++\n");

    vec = __get_MCAUSE();

    rt_kprintf("CPU Exception(mcause): NO.0x%lx\n", vec);
    //print_exception_cause(vec);
    //
    tid = rt_thread_self();
    rt_kprintf("current thread: %.*s\n", RT_NAME_MAX, tid->parent.name);
    //
    print_stack_frame(regs);
    rt_kprintf("mepc   : %p\n", (void *)regs[31]);
    rt_kprintf("mstatus: %p\n", (void *)regs[32]);
    rt_kprintf("\n");
    //
    #if defined(RT_USING_FINSH) && defined(MSH_USING_BUILT_IN_COMMANDS)
    extern long list_thread(void);
    list_thread();
    #endif


    if (trap_c_callback) {
        trap_c_callback();
    }

    while (1);
}

__attribute__((weak)) void exceptionHandler(void *context)
{
    trap_c((uintptr_t *)context);
}

