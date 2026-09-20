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
/*
 * **477: slot 1 splits on the interrupted mode, here, before any C code has run.**
 *
 * 475 gave the user case to Apple's handler *from inside* this image's `fleh_undef` - record the
 * user's `udf`, then `bl locore_fleh_undef` - and 476's run measured what that costs, so this slot no
 * longer does it in C.
 *
 * **The defect is one register and it is in Apple's own code.** `locore_fleh_undef` derives the saved
 * program counter from `lr`: `mrs sp, SPSR`, `tst sp, #32`, then `subeq lr, lr, #4` / `subne lr, lr,
 * #2` (`0x8001402c`/`0x80014030` in 476's image), and `undef_from_user` stores that `lr` into
 * `SS_PC`. On a vector entry that is right - the hardware leaves `lr_und = pc + 4`, so the
 * subtraction recovers the trapping instruction. On a `bl` from C it is wrong in both halves: `lr`
 * is the *return address* (so the subtraction points one instruction before it), and it is this
 * function's own address, not the user's. 476's run measured exactly that: the fifth abort is a
 * **user-mode instruction fetch of `0x80006d04`**, which is the `bl locore_fleh_undef` in 475's
 * `fleh_undef` (`IFSR = 0xd`, permission fault, section - a user fetch of a supervisor-only
 * section), with `lr = 0` and `cpsr = 0x10`. The kernel had resumed process 1 at this image's own
 * instruction, and process 1 could not execute it. So the user case cannot be reached by a call: it
 * has to be reached by a branch, with the interrupted registers untouched.
 *
 * **The test is Apple's own, and so is the register it uses.** `mrs sp, spsr` puts the interrupted
 * `SPSR` into the *banked* stack pointer - the one register an exception both saves and banks, so
 * nothing of the interrupted context is lost, and Apple's own first-level bodies do the same thing
 * (`mrs sp, SPSR` is `fleh_undef`'s first instruction and the second of both abort handlers, after
 * their `sub lr, lr, #8` and `sub lr, lr, #4`). `PSR_MODE_MASK` and `PSR_USER_MODE` are
 * `osfmk/arm/proc_reg.h`'s; `and`/`cmp` cannot carry a symbol, so the two numbers are written here
 * and checked where they can be: `entry_saved_state.h` holds the same pair for the C side, and
 * `build_entry.sh` requires this trampoline's two literals to be the two handlers.
 *
 * **The kernel branch keeps the stack load the macro does**, because this image's handler is C code
 * and needs a stack; the user branch does not, because Apple's `undef_from_user` re-derives `sp` from
 * `TPIDRPRW` and the thread's `kstackptr` and never reads this one. That asymmetry is the reason this
 * slot is written out rather than passed to `VECTOR_TRAMP`.
 */
vec_tramp_1:
    mrs     sp, spsr
    and     sp, sp, #0x1f
    cmp     sp, #0x10
    beq     vec_tramp_1_user
    ldr     sp, vec_tramp_1_stack
    ldr     pc, vec_tramp_1_handler
vec_tramp_1_user:
    ldr     pc, vec_tramp_1_user_handler
    .align  2
vec_tramp_1_stack:
    .word   entry_vectors_stack_top
vec_tramp_1_handler:
    .word   fleh_undef
vec_tramp_1_user_handler:
    .word   locore_fleh_undef
    .size vec_tramp_1, . - vec_tramp_1

/*
 * **479: the SWI slot is XNU's own handler, and the reason is that process 1 now asks the kernel for
 * something.** 478's run stopped because the RAM disk Mach-O's first instruction was `udf #0`: the
 * kernel triaged the bad instruction, killed pid 1 with SIGILL and panicked in
 * `launchd_crashed_panic`, which `proc_prepareexit` makes unconditional for `initproc`
 * (`bsd/kern/kern_exit.c:846`) - so no init exit is survivable and the fixture's first instruction has
 * to be one the kernel can *answer*.
 *
 * That instruction is a `svc` (`entry_ramdisk.s`'s five words, `getpid`), and this is the slot it
 * arrives through. Until now the slot was this image's `fleh_swi` - record and stop, the shape every
 * one of these handlers had before 467 - which would have stopped the boot at process 1's *first*
 * syscall and measured nothing else about it. So the slot goes to Apple's `locore_fleh_swi` (the
 * renamed copy of `osfmk/arm/locore.s:483`, `locore_` for the same reason 466 renamed the two abort
 * handlers: the twelve names it collides with are this image's own vector glue), exactly as 467 moved
 * slots 3 and 4 and 477 moved slot 1.
 *
 * **What that handler does with r12 decides which half of the syscall ABI a run measures.** It
 * computes `r5 = -r12` and branches to `fleh_swi_unix` when that is `<= 0`, so a *positive* r12 is a
 * BSD syscall - `bl unix_syscall`, which takes the state, the thread, the uthread and the process, and
 * never returns (it ends in `thread_exception_return`) - and a negative one is a mach trap looked up in
 * `mach_trap_table`. The fixture's r12 is `+20` (`SYS_getpid`), so this step measures the BSD side,
 * whose entry is the one `--wrap=getpid` is installed on: the wrapper sits in `sysent[20].sy_call`
 * and records the number the kernel wrote back into the process's own return slot. `getpid` is the
 * smallest syscall that *returns* instead of blocking - the alternative this step rejected,
 * `thread_switch` (mach trap 61), ends in `thread_block_reason` on every option and has no timer and
 * no sender to wake it in this image.
 *
 * **The call is not a leap, and the names it needs are the evidence.** All five of the names
 * `fleh_swi` calls - `mach_kauth_cred_uthread_update`, `mach_trap_table`, `kern_invalid`,
 * `throttle_lowpri_io`, `thread_exception_return` - are real definitions in this image, and so are the
 * ones the unix arm adds: `unix_syscall` is `0x8027d148`, `sysent` `0x804ef44c`, `nsysent` a data word
 * at `0x80527078`, and `getpid` `0x80292154`. `tools/check_sysent_table.py` reads the table itself:
 * entry 20 must be `getpid`, the entries around it must be the master's own numbering, and the
 * wrapper must be in the slot.
 *
 * **What is given up is the report this image's handler wrote.** `fleh_swi` published the syscall
 * number, the caller and the mode; the replacement publishes the *answer* instead (the wrapper on
 * `sysent[20]`), which is the value the fixture's `cmp r0, #1` tests. `fleh_swi` is kept in
 * `entry_stubs.c` beside `fleh_dataabt` and `fleh_prefabt`, and the build check asserts that none of
 * the three is in this table: a slot that quietly kept the old handler looks exactly like a kernel
 * that refused the syscall.
 *
 * The stack load the macro does is kept for 467's reason. On the user path Apple's handler reads
 * `TPIDRPRW` into `sp` itself and never dereferences this one; on the kernel path - `swi_from_kernel`,
 * which panics - it uses the banked ABT stack as a scratch register, so this SP is not read there
 * either. Keeping all eight slots the same shape is what 476's own last paragraph asked for.
 */
    VECTOR_TRAMP vec_tramp_2, locore_fleh_swi
/*
 * **476: the prefetch slot joins the data slot, and the reason is 475's run.** The other six branch to
 * `entry_stubs.c`'s handlers, whose design is to record and leave - which is what a fault deserves
 * when the fault *is* the end of the run, and what every step since 236 has used them for.
 *
 * 475's run entered user mode, had its `udf` handled by the kernel, and then stopped on a prefetch
 * abort - recorded, and nothing else. That record was refused by the full trace channel, so the run
 * has a stop and no address: `xnu_entry_kv_written = 8170` of 8192 with 33292 refusals. Two questions
 * were riding on one slot, and this step answers both by handing the slot to the kernel:
 *
 *   - **is the fault serviceable?** 466's data-abort stop was the demand fault `vm_fault` exists to
 *     answer, and 467's fix - Apple's own first-level handler in the slot - is what carried the boot
 *     through the whole exec path. A prefetch abort on an instruction fetch is the same kind of event,
 *     and the instrument cannot tell a demand fault from a fatal one: it can only record and stop.
 *   - **and if it is not serviceable, the record must survive the fault.** It does, and it does not
 *     need a new buffer: XNU's handler calls `sleh_abort(regs, T_PREFETCH_ABT)`, which 467 already
 *     wraps, and that wrapper's record goes to the **live channel** - the one 474 and 475 both arrived
 *     through, and the one that is captured whether or not the epilogue runs. It carries the fault
 *     class, the two fault numbers, the thread, and (since 474) the frame's `pc`, `lr`, `sp`, `cpsr`
 *     and its own copy of the fault pair.
 *
 * **What is given up is named rather than dropped**: this image's `fleh_prefabt` reported IFAR, IFSR,
 * LR_abt, PC, SPSR, TTBR0, TTBR1, TTBCR and SCTLR, and the live record carries five of those nine
 * outright (`pc`/`lr`/`cpsr` and the fault pair, read from the instruction-side coprocessor registers -
 * see `entry_trace.c`'s wrapper, which was reading the *data* pair until this step) plus the thread.
 * **TTBR0, TTBR1, TTBCR and SCTLR are not in it** - 241 and 242 read them to tell which page tables
 * were live - so a future step that needs the mapping state again has to add it to the wrapper rather
 * than expect it from a handler that is no longer installed. `fleh_prefabt` is kept in
 * `entry_stubs.c` beside `fleh_dataabt` (uninstalled since 467) and the build check asserts that
 * neither is in the table: a slot that quietly kept the old handler looks exactly like a kernel that
 * refused to service the fault.
 *
 * The stack load is kept for 467's reason: XNU's handler switches to SVC mode on both of its paths and
 * never dereferences this banked SP, but a slot whose trampoline differs from the other seven is a
 * slot that will be misread the next time this file is edited.
 */
    VECTOR_TRAMP vec_tramp_3, locore_fleh_prefabt
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
/*
 * **483: slot 6 moves from this image's reporting stub to Apple's own IRQ entry.**
 *
 * From 308 until 482 the IRQ vector ended in `entry_stubs.c`'s `fleh_irq`, which sets a flag and
 * calls `entry_epilogue("exception: irq")` - a report and a stop, chosen because there was nothing
 * to dispatch *to*: the dispatcher `fleh_irq_handler` reads `cpu_data->interrupt_handler` and
 * `blx`es it, and this image had never stored one, so Apple's entry would branch to address 0.
 * `entry_irq.c` now stores one, and this slot is what makes it reachable.
 *
 * `locore_fleh_irq` is Apple's vector entry, not the dispatcher: it splits on the interrupted mode
 * (`fleh_irq_user` saves into the thread's PCB, `fleh_irq_kernel` onto the interrupted stack), swaps
 * `sp` to `cpu_data->istackptr` for the second level, and falls through into `fleh_irq_handler`.
 * `LEXT` is why the symbol carries a `locore_` prefix in the image while the source says
 * `fleh_irq` - and why `fleh_irq` here is this image's own and not a clash.
 *
 * The trampoline's `ldr sp` above stays even though it is no longer load-bearing for the *handler*:
 * it loads `sp_irq`, which Apple's entry overwrites with the SVC bank before it touches a stack, so
 * for this slot it is a dead store - and it is left in place because the macro is shared with the
 * seven slots that still need it.
 */
    VECTOR_TRAMP vec_tramp_6, locore_fleh_irq
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
