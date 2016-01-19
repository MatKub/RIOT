/*
 * Copyright (C) 2015 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     cpu_cortexm_common
 * @{
 *
 * @file
 * @brief       Default implementations for Cortex-M specific interrupt and
 *              exception handlers
 *
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Daniel Krebs <github@daniel-krebs.net>
 * @author      Joakim Gebart <joakim.gebart@eistec.se>
 *
 * @}
 */

#include <stdint.h>
#include <stdio.h>

#include "cpu.h"
#include "board.h"
#include "panic.h"
#include "kernel_internal.h"
#include "vectors_cortexm.h"
#include "periph/hard_communication.h"

#define HARD_FAULT_DESCRIPTIONS 1

/**
 * @brief Interrupt stack canary value
 *
 * @note 0xe7fe is the ARM Thumb machine code equivalent of asm("bl #-2\n") or
 * 'while (1);', i.e. an infinite loop.
 */
#define STACK_CANARY_WORD 0xE7FEE7FEu

/**
 * @brief   Memory markers, defined in the linker script
 * @{
 */
extern uint32_t _sfixed;
extern uint32_t _efixed;
extern uint32_t _etext;
extern uint32_t _srelocate;
extern uint32_t _erelocate;
extern uint32_t _szero;
extern uint32_t _ezero;
extern uint32_t _sstack;
extern uint32_t _estack;
extern uint32_t _sram;
extern uint32_t _eram;
/** @} */

/**
 * @brief   Allocation of the interrupt stack
 */
__attribute__((used,section(".isr_stack")))     uint8_t isr_stack[ISR_STACKSIZE];

/**
 * @brief   Pre-start routine for CPU-specific settings
 */
__attribute__((weak)) void pre_startup(void)
{
}

/**
 * @brief   Post-start routine for CPU-specific settings
 */
__attribute__((weak)) void post_startup(void)
{
}

void reset_handler_default(void)
{
    uint32_t *dst;
    uint32_t *src = &_etext;

    pre_startup();

#ifdef DEVELHELP
    uint32_t *top;
    /* Fill stack space with canary values up until the current stack pointer */
    /* Read current stack pointer from CPU register */
    asm volatile ("mov %[top], sp" : [top] "=r" (top) : : );
    dst = &_sstack;
    while(dst < top) {
        *(dst++) = STACK_CANARY_WORD;
    }
#endif

    /* load data section from flash to ram */
    for(dst = &_srelocate; dst < &_erelocate;) {
        *(dst++) = *(src++);
    }
    /* default bss section to zero */
    for(dst = &_szero; dst < &_ezero;) {
        *(dst++) = 0;
    }

    post_startup();

    /* initialize the board (which also initiates CPU initialization) */
    board_init();

#if MODULE_NEWLIB
    /* initialize std-c library (this must be done after board_init) */
    extern void __libc_init_array(void);
    __libc_init_array();
#endif

    /* startup the kernel */
    kernel_init();
}

void nmi_default(void)
{
    core_panic(PANIC_NMI_HANDLER, "NMI HANDLER");
}

#ifdef DEVELHELP

/* The hard fault handler requires some stack space as a working area for local
 * variables and printf function calls etc. If the stack pointer is located
 * closer than HARDFAULT_HANDLER_REQUIRED_STACK_SPACE from the lowest address of
 * RAM we will reset the stack pointer to the top of available RAM.
 * Measured from trampoline entry to breakpoint:
 *  - Cortex-M0+ 344 Byte
 *  - Cortex-M4  344 Byte
 */
#define HARDFAULT_HANDLER_REQUIRED_STACK_SPACE          (344U)

static inline int _stack_size_left(uint32_t required)
{
    uint32_t* sp;
    asm volatile ("mov %[sp], sp" : [sp] "=r" (sp) : : );
    return ((int)((uint32_t)sp - (uint32_t)&_sstack) - required);
}

/* Trampoline function to save stack pointer before calling hard fault handler */
__attribute__((naked)) void hard_fault_default(void)
{
    /* Get stack pointer where exception stack frame lies */
    __ASM volatile
    (
            /* Check that msp is valid first because we want to stack all the
             * r4-r11 registers so that we can use r0, r1, r2, r3 for other things. */
            "mov r0, sp                         \n" /* r0 = msp                   */
            "cmp r0, %[eram]                    \n" /* if(msp > &_eram) {         */
            "bhi fix_msp                        \n" /*   goto fix_msp }           */
            "cmp r0, %[sram]                    \n" /* if(msp <= &_sram) {        */
            "bls fix_msp                        \n" /*   goto fix_msp }           */
            "mov r1, #0                         \n" /* else { corrupted = false   */
            "b   test_sp                        \n" /*   goto test_sp     }       */
            " fix_msp:                          \n" /*                            */
            "mov r1, %[estack]                  \n" /*     r1 = _estack           */
            "mov sp, r1                         \n" /*     sp = r1                */
            "mov r1, #1                         \n" /*     corrupted = true       */
            " test_sp:                          \n" /*                            */
            "movs r0, #4                        \n" /* r0 = 0x4                   */
            "mov r2, lr                         \n" /* r2 = lr                    */
            "tst r2, r0                         \n" /* if(lr & 0x4)               */
            "bne use_psp                        \n" /* {                          */
            "mrs r0, msp                        \n" /*   r0 = msp                 */
            "b out                              \n" /* }                          */
            " use_psp:                          \n" /* else {                     */
            "mrs r0, psp                        \n" /*   r0 = psp                 */
            " out:                              \n" /* }                          */
#if (__CORTEX_M == 0)
            "push {r4-r7}                       \n" /* save r4..r7 to the stack   */
            "mov r3, r8                         \n" /*                            */
            "mov r4, r9                         \n" /*                            */
            "mov r5, r10                        \n" /*                            */
            "mov r6, r11                        \n" /*                            */
            "push {r3-r6}                       \n" /* save r8..r11 to the stack  */
#else
            "push {r4-r11}                      \n" /* save r4..r11 to the stack  */
#endif
            "mov r3, sp                         \n" /* r4_to_r11_stack parameter  */
            "b hard_fault_handler               \n" /* hard_fault_handler(r0)     */
            :
            : [sram] "r" (&_sram + HARDFAULT_HANDLER_REQUIRED_STACK_SPACE),
            [eram] "r" (&_eram),
            [estack] "r" (&_estack)
            : "r0","r4","r5","r6","r8","r9","r10","r11","lr"
    );
}

#if (__CORTEX_M == 0)
/* Cortex-M0 and Cortex-M0+ lack the extended fault status registers found in
 * Cortex-M3 and above. */
#define CPU_HAS_EXTENDED_FAULT_REGISTERS 0
#else
#define CPU_HAS_EXTENDED_FAULT_REGISTERS 1
#endif

__attribute__((used)) void hard_fault_handler(uint32_t* sp, uint32_t corrupted,
        uint32_t exc_return, uint32_t* r4_to_r11_stack)
{
    /* Check if the ISR stack overflowed previously. Not possible to detect
     * after output may also have overflowed it. */
    if(*(&_sstack) != STACK_CANARY_WORD) {
        h_puts("\nISR stack overflowed");
    }
    /* Sanity check stack pointer and give additional feedback about hard fault */
    if(corrupted) {
        h_puts("Stack pointer corrupted, reset to top of stack");
    } else {
#if CPU_HAS_EXTENDED_FAULT_REGISTERS
        /* Copy status register contents to local stack storage, this must be
         * done before any calls to other functions to avoid corrupting the
         * register contents. */
        uint32_t bfar = SCB->BFAR;
        uint32_t mmfar = SCB->MMFAR;
        uint32_t cfsr = SCB->CFSR;
        uint32_t hfsr = SCB->HFSR;
        uint32_t dfsr = SCB->DFSR;
        uint32_t afsr = SCB->AFSR;
#endif

        uint32_t r0 = sp[0];
        uint32_t r1 = sp[1];
        uint32_t r2 = sp[2];
        uint32_t r3 = sp[3];
        uint32_t r12 = sp[4];
        uint32_t lr = sp[5]; /* Link register. */
        uint32_t pc = sp[6]; /* Program counter. */
        uint32_t psr = sp[7]; /* Program status register. */

        /* Reconstruct original stack pointer before fault occurred */
        uint32_t* orig_sp = sp + 8;
        if(psr & SCB_CCR_STKALIGN_Msk) {
            /* Stack was not 8-byte aligned */
            orig_sp += 1;
        }

        h_puts("\nContext before hardfault:");

        /* TODO: printf in ISR context might be a bad idea */
        h_printf("   r0: 0x%08" PRIx32 "\n"
        "   r1: 0x%08" PRIx32 "\n"
        "   r2: 0x%08" PRIx32 "\n"
        "   r3: 0x%08" PRIx32 "\n", r0, r1, r2, r3);
        h_printf("  r12: 0x%08" PRIx32 "\n"
        "   lr: 0x%08" PRIx32 "\n"
        "   pc: 0x%08" PRIx32 "\n"
        "  psr: 0x%08" PRIx32 "\n\n", r12, lr, pc, psr);
#if CPU_HAS_EXTENDED_FAULT_REGISTERS
        h_puts("FSR/FAR:");
        h_printf(" CFSR: 0x%08" PRIx32 "\n", cfsr);
        h_printf(" HFSR: 0x%08" PRIx32 "\n", hfsr);
        h_printf(" DFSR: 0x%08" PRIx32 "\n", dfsr);
        h_printf(" AFSR: 0x%08" PRIx32 "\n", afsr);

        h_printf("\tUFSR bit assignments:\n");
       if(cfsr & 0x02000000)
           h_printf("\t\tDIVBYZERO - the processor has executed an SDIV or UDIV instruction with a divisor of 0\n");
       if(cfsr & 0x01000000)
           h_printf("\t\tUNALIGNED -  the processor has made an unaligned memory access\n");
       if(cfsr & 0x00080000)
           h_printf("\t\tNOCP - the processor has attempted to access a coprocessor\n");
       if(cfsr & 0x00040000)
           h_printf("\t\tINVPC - the processor has attempted an illegal load of EXC_RETURN to the PC, as a result of an invalid context, or an invalid EXC_RETURN value\n");
       if(cfsr & 0x00020000)
           h_printf("\t\tINVSTATE - a data bus error has occurred, and the PC value stacked for the exception return points to the instruction that caused the fault\n");
       if(cfsr & 0x00010000)
           h_printf("\t\tUNDEFINSTR -  the processor has attempted to execute an undefined instructio.\n");

        h_printf("\tBusFault Status Register:\n");
        if(cfsr & 0x00008000)
            h_printf("\t\tBFARVALID - BFAR holds a valid fault address\n");
        if(cfsr & 0x00002000)
            h_printf("\t\tLSPERR - bus fault occurred during floating point lazy state preservation\n");
        if(cfsr & 0x00001000)
            h_printf("\t\tSTKERR - stacking for an exception entry has caused one or more BusFaults\n");
        if(cfsr & 0x00000800)
            h_printf("\t\tUNSTKERR - unstack for an exception return has caused one or more BusFaults\n");
        if(cfsr & 0x00000400)
            h_printf("\t\tIMPRECISERR - a data bus error has occurred, but the return address in the stack frame is not related to the instruction that caused the error\n");
        if(cfsr & 0x00000200)
            h_printf("\t\tPRECISERR - a data bus error has occurred, and the PC value stacked for the exception return points to the instruction that caused the fault\n");
        if(cfsr & 0x00000100)
            h_printf("\t\tIBUSERR - instruction bus error.\n");

        h_printf("\tMemManage Status Register:\n");
        if(cfsr & 0x00000080)
            h_printf("\t\tMMARVALID - MMAR holds a valid fault address\n");
        if(cfsr & 0x00000020)
            h_printf("\t\tMLSPERR - MemManage fault occurred during floating-point lazy state preservation\n");
        if(cfsr & 0x00000010)
            h_printf("\t\tMSTKERR - stacking for an exception entry has caused one or more access violations\n");
        if(cfsr & 0x00000008)
            h_printf("\t\tMUNSTKERR - unstack for an exception return has caused one or more access violations\n");
        if(cfsr & 0x00000002)
            h_printf("\t\tDACCVIOL - the processor attempted a load or store at a location that does not permit the operation\n");
        if(cfsr & 0x00000001)
            h_printf("\t\tIACCVIOL - the processor attempted an instruction fetch from a location that does not permit execution\n");

        h_printf("\tHardFault Status Register:\n");
        if(hfsr & 0x40000000)
            h_printf("\t\tFORCED - forced HardFault\n");
        if(hfsr & 0x00000002)
            h_printf("\t\tVECTTBL - BusFault on vector table read\n");

        if(((cfsr & SCB_CFSR_BUSFAULTSR_Msk) >> SCB_CFSR_BUSFAULTSR_Pos)
                & 0x80) {
            /* BFAR valid flag set */
            h_printf(" BFAR: 0x%08" PRIx32 "\n", bfar);
        }
        if(((cfsr & SCB_CFSR_MEMFAULTSR_Msk) >> SCB_CFSR_MEMFAULTSR_Pos)
                & 0x80) {
            /* MMFAR valid flag set */
            h_printf("MMFAR: 0x%08" PRIx32 "\n", mmfar);
        }
#endif
        h_puts("Misc");
        h_printf("EXC_RET: 0x%08" PRIx32 "\n", exc_return);
        h_puts("Attempting to reconstruct state for debugging...");
        h_printf("In GDB:\n  set $pc=0x%" PRIx32 "\n  frame 0\n  bt\n", pc);
        int stack_left = _stack_size_left(
        HARDFAULT_HANDLER_REQUIRED_STACK_SPACE);
        if(stack_left < 0) {
            h_printf("\nISR stack overflowed by at least %d bytes.\n",
                    (-1 * stack_left));
        }
        __ASM volatile (
                "mov r0, %[sp]\n"
                "ldr r2, [r0, #8]\n"
                "ldr r3, [r0, #12]\n"
                "ldr r1, [r0, #16]\n"
                "mov r12, r1\n"
                "ldr r1, [r0, #20]\n"
                "mov lr, r1\n"
                "mov sp, %[orig_sp]\n"
                "mov r1, %[extra_stack]\n"
#if (__CORTEX_M == 0)
                "ldm r1!, {r4-r7}\n"
                "mov r8, r4\n"
                "mov r9, r5\n"
                "mov r10, r6\n"
                "mov r11, r7\n":c
                "ldm r1!, {r4-r7}\n"
#else
                "ldm r1, {r4-r11}\n"
#endif
                "ldr r1, [r0, #4]\n"
                "ldr r0, [r0, #0]\n"
                :
                : [sp] "r" (sp),
                [orig_sp] "r" (orig_sp),
                [extra_stack] "r" (r4_to_r11_stack)
                : "r0","r1","r2","r3","r12"
        );

        __BKPT(1);
    }

    core_panic(PANIC_HARD_FAULT, "HARD FAULT HANDLER");
}

#else

void hard_fault_default(void)
{
    core_panic(PANIC_HARD_FAULT, "HARD FAULT HANDLER");
}

#endif /* DEVELHELP */

#if defined(CPU_ARCH_CORTEX_M3) || defined(CPU_ARCH_CORTEX_M4) || \
    defined(CPU_ARCH_CORTEX_M4F)
void mem_manage_default(void)
{
    core_panic(PANIC_MEM_MANAGE, "MEM MANAGE HANDLER");
}

void bus_fault_default(void)
{
    core_panic(PANIC_BUS_FAULT, "BUS FAULT HANDLER");
}

void usage_fault_default(void)
{
    core_panic(PANIC_USAGE_FAULT, "USAGE FAULT HANDLER");
}

void debug_mon_default(void)
{
    core_panic(PANIC_DEBUG_MON, "DEBUG MON HANDLER");
}
#endif

void dummy_handler_default(void)
{
    core_panic(PANIC_DUMMY_HANDLER, "DUMMY HANDLER");
}
