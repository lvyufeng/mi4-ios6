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
 *   - The trampoline needs an absolute address for the handler, so it does the usual
 *     `ldr pc, [pc, #-4]` over a literal. The literal holds the handler's link address, which is
 *     correct at runtime because XNU's page tables map the entry window identically.
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

/* Each trampoline: load the handler address and branch. Eight bytes each, so a page holds them
 * with room to spare. The order matches the ARM vector table: reset, undef, svc, prefetch abort,
 * data abort, address exception (unused on ARMv7), irq, fiq. */
.macro VECTOR_TRAMP name, handler
\name:
    ldr pc, [pc, #-4]
    .word \handler
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
