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
 *     are in the entry window at 0x80000000 and this page is mapped at 0xffff0000 - 2.14 GB away,
 *     far outside `b`'s ±32 MB - so each slot branches to a trampoline inside this same page (a few
 *     hundred bytes, comfortably in range). Before experiment 241's base move the handlers were at
 *     0x00200000 and the distance was 4.28 GB; either way it is out of range, which is the point.
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
/*
 * **467: the data abort slot is XNU's own handler, and it is the one slot that is not this file's.**
 *
 * The other seven branch to `entry_stubs.c`'s handlers, whose design is to record and leave - which
 * is what a fault deserves when the fault *is* the end of the run, and what every step since 236 has
 * used them for. 466's run produced the other kind. Its stop was `copyout`'s first store to the page
 * `load_init_program` had just allocated (`dfar=0x1000`, `dfsr=0x805`), which is the demand fault
 * `vm_fault` exists to answer: XNU's own `fleh_dataabt` (`osfmk/arm/locore.s:992`) reads the fault,
 * calls `sleh_abort(regs, T_DATA_ABT)` and - when the page is paged in - returns through
 * `load_and_go_sys`, which **retries the instruction**. Recording that fault instead of answering it
 * is what stopped the boot one statement into `load_init_program_at_path`.
 *
 * So this slot is `locore_fleh_dataabt`, the renamed copy of Apple's handler that 466 linked
 * (`locore_` because the twelve names it collides with are this image's own vector glue), and the
 * consequence is deliberate: a data abort in this image is now the kernel's decision and not a stop.
 *
 * **What keeps the report is that XNU's fatal paths end in an undefined instruction.** A `sleh_abort`
 * that cannot answer the fault panics (`trap.c:313`, `:393`, `:464`), and `panic()` reaches
 * `DebuggerTrapWithState`'s `udf`, which is slot 1 - still this file's handler, still writing the
 * trap's own buffer. That is the route 461 measured when it named the trap it was stuck on, and it
 * is why this slot can change without the image going silent about a fault that is genuinely fatal.
 * The measurement of what the kernel *decided* is `--wrap=sleh_abort` (`entry_trace.c`), one record
 * per entry and one per return.
 *
 * The stack load above is kept even though XNU's handler switches to SVC mode on both of its paths
 * and never dereferences this banked SP: it costs two words, and a slot whose trampoline is
 * different from the other seven is a slot that will be misread the next time this file is edited.
 */
    VECTOR_TRAMP vec_tramp_4, locore_fleh_dataabt
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
