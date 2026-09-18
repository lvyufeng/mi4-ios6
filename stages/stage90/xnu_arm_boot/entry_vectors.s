/*
 * The exception vector page for the entry image.
 *
 * `start.s` maps this page at HIGH_EXC_VECTORS (0xffff0000) and turns on SCTLR.HIGHVEC, so after
 * its page-table switch an exception vectors here instead of at the payload's VBAR. Without real
 * code in this page an exception would execute zeros and fall through forever, which is a hang
 * whose only symptom is that the watchdog fires - so what this file is for is making an exception
 * *say which one it was*.
 *
 * Two constraints shape it:
 *
 *   - An ARM vector slot is four bytes, which fits a `b` and not an absolute load. Our handlers
 *     are ~1.5 GB away from 0xffff0000, far outside `b`'s ±32 MB, so each slot branches to a
 *     trampoline inside this same page (a few hundred bytes, comfortably in range).
 *   - The trampoline needs an absolute address for the handler, so it loads one from a literal in
 *     this page. The literal holds the handler's link address, which is correct at runtime because
 *     XNU's page tables map the entry window identically.
 *
 *   - It also loads SP from `__entry_vectors_stack_top` before branching, for the reason given at
 *     the macro: the banked stack pointer an exception inherits is not inside XNU's page tables,
 *     so without this the handler cannot run and the fault is silent.
 *
 * The displacements are all relative, so they are unaffected by the page being mapped somewhere
 * else at runtime than where it was linked - which is exactly what happens.
 */

    .syntax unified
    .arm

    .section .text.vectors, "ax", %progbits
    .align 12                          /* 4 KB: start.s masks this address with ARM_PTE_PAGE_MASK */

    .global ExceptionVectorsBase
    .type ExceptionVectorsBase, %function
ExceptionVectorsBase:
    b   vec_tramp_0
    b   vec_tramp_1
    b   vec_tramp_2
    b   vec_tramp_3
    b   vec_tramp_4
    b   vec_tramp_5
    b   vec_tramp_6
    b   vec_tramp_7

    .space 0xF00 - (. - ExceptionVectorsBase), 0

/* Each trampoline: load a stack pointer, then load the handler address and branch. Sixteen bytes
 * each, so a page holds them with room to spare. The order matches the ARM vector table: reset,
 * undef, svc, prefetch abort, data abort, address exception (unused on ARMv7), irq, fiq.
 *
 * The stack load is not decoration. An exception does not change the SVC stack pointer, it banks
 * to the mode's own - and `_start` never writes the banked ones, so on entry SP is whatever the
 * bootloader or the payload left there, which under XNU's page tables is unmapped. The handler's
 * first push would abort, the abort would be taken again on the same stack, and the CPU would
 * recurse until the watchdog fired: a fault with no message, indistinguishable from a hang. SP is
 * loaded from a literal in this page rather than `mov sp, #imm` because the address is far outside
 * any immediate, and from a literal rather than a second branch because a branch would clobber the
 * link register the handler wants. */
.macro VECTOR_TRAMP name, handler
\name:
    ldr     sp, \name\()_stack
    ldr     pc, \name\()_handler
    .align  2
\name\()_stack:
    .word   entry_vectors_stack_top
\name\()_handler:
    .word   \handler
    .size \name, . - \name
.endm

    VECTOR_TRAMP vec_tramp_0, fleh_reset
    VECTOR_TRAMP vec_tramp_1, fleh_undef
    VECTOR_TRAMP vec_tramp_2, fleh_swi
    VECTOR_TRAMP vec_tramp_3, fleh_prefabt
    VECTOR_TRAMP vec_tramp_4, fleh_dataabt
    VECTOR_TRAMP vec_tramp_5, fleh_addrexc
    VECTOR_TRAMP vec_tramp_6, fleh_irq
    VECTOR_TRAMP vec_tramp_7, fleh_decirq

    .size ExceptionVectorsBase, . - ExceptionVectorsBase

/*
 * The stack every handler runs on, in this file rather than in `entry_stubs.c` because what the
 * literal above needs is the stack top's *address*, and a C variable holding that address would be
 * one dereference away from it. Here the symbol itself is the address, so `.word` resolves it at
 * link time with nothing to load at runtime.
 *
 * It has to be in the window, which is why it is `.bss` and not space in the vector page: only the
 * one page holding `ExceptionVectorsBase` is mapped at 0xffff0000, and the entry window is the
 * only other thing XNU's page tables map. The reason a handler cannot simply use the stack it
 * inherits is in the macro above.
 */
    .section .bss.entry_vectors_stack, "aw", %nobits
    .align  3
entry_vectors_stack:
    .space  0x1000
entry_vectors_stack_top:
