/*
 * The six words of `struct arm_saved_state` the abort record reads - and the one number in this
 * image that is neither Apple's nor this project's but the build's own.
 *
 * Why a header and not six numbers inside `entry_stubs.c`. One reason is the layout itself: the frame
 * is `struct arm_saved_state`, and the one place in this image that says so should be a file that is
 * about `struct arm_saved_state` and nothing else. The other is that the numbers have to be
 * *compared* against two sources this project does not own -
 * `tools/check_saved_state_offsets.py` reads them, Apple's own header, and this configuration's
 * generated `assym.s` - and a check wants one stable place to read a definition from, not a regex
 * into the middle of a four-thousand-line file. So: one definition, and a build step that refuses to
 * link the image if it disagrees with either source. (`entry_trace.c` needs none of them - it hands
 * the pointer over and never dereferences it.)
 *
 * **Where these numbers come from.** Not from a copy of Apple's struct: from
 * `out/xnu_assym/$XNU_KERNEL_CONFIG/assym.s`, which `tools/gen_assym.sh` writes by compiling
 * Apple's own `osfmk/arm/genassym.c` against Apple's own `osfmk/mach/arm/thread_status.h`. That file
 * declares exactly these, and this is what it gives for this configuration:
 *
 *     #define SS_R0      0     #define SS_SP     52     #define SS_PC     60
 *     #define SS_CPSR   64     #define SS_STATUS 68     #define SS_VADDR  72
 *     #define SS_LR     56     #define SS_EXC    76     #define SS_SIZE   80
 *
 * - i.e. `uint32_t r[13]` (thirteen words, 0..48), then `sp`, `lr`, `pc`, `cpsr`, `fsr`, `far`,
 * `exception`. `tools/check_saved_state_offsets.py` parses that header, this file and the generated
 * `assym.s`, and refuses the build if any of the three disagrees - including the *cross-derivation*
 * `ACT_PCBDATA_PC - ACT_PCBDATA == SS_PC`, which is the same statement reached from the thread's
 * PCB instead of from the stack frame.
 *
 * **Why reading the frame is worth six offsets at all (474).** Experiment 473's run is served four
 * data aborts and returns from all four, and then records a fifth and nothing more: an abort whose
 * `DFAR` the log never names, because the record that would have carried it was cut by
 * `SLEH_LIVE_MAX`. 467's record reads `DFSR`/`DFAR` out of `cp15`, and those two numbers are the
 * only part of the frame it can name without a layout. The other part is the one that answers the
 * question: `regs->pc` is the *instruction* that faulted, and `regs->cpsr` says which mode it was
 * running in - which is exactly the distinction 473 could not make between a kernel `copyin` of a
 * user address and the process-1 thread faulting in user mode.
 *
 * The vector code fills all six, on both paths (`osfmk/arm/locore.s`): `dataabt_from_kernel` builds
 * the frame on the kernel stack and `dataabt_from_user` uses the current thread's PCB, and both do
 * `str lr, [., SS_PC]` (with the `sub lr, lr, #8` the abort vector has already applied, so `SS_PC`
 * is the *faulting* instruction and not the next one), `mrs r4, spsr` into `SS_CPSR`, and
 * `mrc p15,0,r5,c5,c0` / `mrc p15,0,r6,c6,c0` into `SS_STATUS`/`SS_VADDR`.
 *
 * **That last pair is the check.** Those two instructions read the same two coprocessor registers
 * 467's record reads for itself, so a frame read at the right offsets must report the same `DFSR` and
 * `DFAR` - and 472's and 473's first four entries are aborts whose two numbers are already known
 * (`0x805/0x1000`, `0x807/0xc8105000`, `0x807/0xc8146000`, `0x805/0x00101f28`). So entries 1 to 4 are
 * the *control*: if the offsets were wrong, the run says so before the frontier is reported, on
 * aborts whose answer is already in hand. `xnu_live_sleh_frame_ok` carries that comparison into the
 * log, one per entry, so the reading does not depend on a reader noticing two numbers agree.
 *
 * **476 adds two more numbers here, and they are a different kind**: `STAGE90_T_PREFETCH_ABT` and
 * `STAGE90_T_DATA_ABT`, the two abort classes whose vectors now belong to Apple's own handlers. They
 * are not offsets, but they decide **which coprocessor pair fills two of the six** - see the comment
 * on them at the bottom - and a header about the frame is the right place for the number that says
 * which class of fault the frame is a frame of.
 */
#ifndef STAGE90_ENTRY_SAVED_STATE_H
#define STAGE90_ENTRY_SAVED_STATE_H

/* `struct arm_saved_state`, `osfmk/mach/arm/thread_status.h` - via genassym's `SS_*` (see above). */
#define STAGE90_SS_SP      52
#define STAGE90_SS_LR      56
#define STAGE90_SS_PC      60
#define STAGE90_SS_CPSR    64
#define STAGE90_SS_STATUS  68
#define STAGE90_SS_VADDR   72

/* `sizeof(struct arm_saved_state)` (`SS_SIZE`), for the word count the frame is indexed in. */
#define STAGE90_SS_SIZE    80

/* `osfmk/arm/proc_reg.h`: `cpsr & PSR_MODE_MASK == PSR_USER_MODE` is Apple's own test, in
 * `sleh_abort` (`osfmk/arm/trap.c`: `if ((spsr & PSR_MODE_MASK) != PSR_USER_MODE)`), and the frame's
 * `cpsr` is that same `spsr`. So the mode the fault was taken in is the same expression here. */
#define STAGE90_PSR_MODE_MASK   0x0000001Fu
#define STAGE90_PSR_USER_MODE   0x00000010u

/*
 * **480 adds two words that are not frame offsets, and they are the two the abort handler
 * dereferences on its way to servicing a fault.** They are here for the same reason the six are: the
 * image cannot include Apple's header, so the numbers are transcribed, and a transcribed number is
 * the defect this project has paid for repeatedly.
 *
 * `ACT_MAP` is `offsetof(struct thread, map)` from the same generated `assym.s` - `genassym.c:148`
 * declares it - and `MAP_PMAP` is `offsetof(struct vm_map, pmap)` (`genassym.c`'s `MAP_PMAP`).
 * `sleh_abort` reads them in exactly this order: `trap.c:446` picks the map a fault is serviced in
 * (`map = thread->map` for a user address, `kernel_map` otherwise) and `trap.c:449` hands
 * `map->pmap` to `arm_fast_fault`.
 *
 * **Why 480 reads them rather than trusting them.** 474's run stopped on those two loads with the
 * pointer *zero*: the record's fault was at `far 0x00000028`, which is `MAP_PMAP` - a load through a
 * NULL map - and the same fault re-entered the handler 0x250 bytes further down the kernel stack
 * until the stack ran out and the boot stopped without writing a report. That happened in the
 * *kernel*, on the exec's own `copyin`. 480 makes process 1 fault **in user mode** on a page of its
 * own, so the same two words are on the path, and this image now measures `thread->map` and
 * `map->pmap` immediately before the fixture's first access. A zero is then a reading in the log
 * instead of a silent recursion - and `entry_stubs.c` says what the run does with it.
 */
#define STAGE90_ACT_MAP        692
#define STAGE90_MAP_PMAP       40

/*
 * **490 adds the word that says whether a fault was a copy's designed fault or a fault the kernel
 * had no plan for**, and it is the same kind of number as the two above: an `offsetof(struct thread,
 * ...)` out of the generated `assym.s`, transcribed here because the image cannot include Apple's
 * header.
 *
 * `thread->recover` is the *recovery address* of Apple's fault-driven copy: `COPYIO_SET_RECOVER`
 * (`osfmk/arm/machine_routines_asm.s:542-550`) arms it with an `adr` to the copy's own error label
 * before entering the copy loop, `sleh_abort` (`trap.c:290-291`) reads it and **zeroes it** before
 * doing anything else, and if the page cannot be paged in it points `regs->pc` at it
 * (`trap.c:456-461`) - so the faulting instruction is replaced by the copy's error exit and the
 * caller sees `EFAULT` instead of the instruction being retried forever or the kernel panicking.
 *
 * **So a non-zero value here at the moment of a fault is the reading that the fault was planned.**
 * 489's document closed by calling the two `Lcopyin_wordwise_loop` faults at `far = 0` the
 * frontier, and inferred from `far` and the faulting function that "copyin faults rather than
 * returning EFAULT" - which is the opposite of what the code does: it faults *in order that* it can
 * return EFAULT. Publishing this word turns that from an inference about the source into a number
 * the run prints, and it is read in the wrapper *before* `__real_sleh_abort` because the handler
 * consumes it - a read after the call would report 0 for every entry, which is a value a reader
 * would believe.
 */
#define STAGE90_TH_RECOVER     664

/*
 * **510 adds the third `offsetof(struct thread, ...)`, and it is the one that reaches the saved state
 * itself.**
 *
 * `get_user_regs(thread)` is `&thread->machine.PcbData` (`osfmk/arm/status.c:502-505`) and `PcbData`
 * is the **first** member of `struct machine_thread` (`osfmk/arm/thread.h`), so `ACT_PCBDATA` is the
 * offset of the user's saved state inside the thread - and the PC inside it is at
 * `ACT_PCBDATA + SS_PC`, because `struct arm_saved_state` and `struct arm_thread_state` lay their
 * members out the same way (`__r[13]`, `sp`, `lr`, `pc`, `cpsr`). **That second half is not a claim
 * made here**: `ACT_PCBDATA_PC - ACT_PCBDATA == SS_PC` is one of the agreements
 * `tools/check_saved_state_offsets.py` has checked since 474, and this step's check extends it to
 * the header's own constant - `STAGE90_ACT_PCBDATA` against the generated `assym.s`'s `ACT_PCBDATA`,
 * the same two-way comparison the three words above get.
 *
 * **What it is for.** `thread_setentrypoint(thread, entry)` (`osfmk/arm/status.c:662-675`) is where
 * the kernel tells a thread at what PC its user code begins: its whole body is
 * `sv = get_user_regs(thread); sv->pc = entry;`. So the argument is the kernel's *decision* and the
 * word at `thread + ACT_PCBDATA + SS_PC` is the kernel's *state* - and a wrapper that reads one
 * before the call and the other after it has a before/after pair on the same word, one of whose ends
 * is written by the kernel rather than by the instrument.
 *
 * The alternative would be to publish only the argument, and the defect that would hide is the one
 * this project keeps paying for: a function that returns without doing anything looks exactly like a
 * correct call from the argument's side. The read-back is what says the store happened.
 */
#define STAGE90_ACT_PCBDATA    848

/* `osfmk/arm/trap.h:69-74`, the two abort classes this image can see once 467 and 476 have given
 * slots 4 and 3 to Apple's own first-level handlers. **The class is what says which coprocessor pair
 * is the fault pair** - a prefetch abort's is IFSR (`c5,c0,1`) and IFAR (`c6,c0,2`), a data abort's
 * is DFSR (`c5,c0,0`) and DFAR (`c6,c0,0`) - so the wrapper that reads them (`entry_trace.c`) has to
 * know it, and the frame's own `SS_STATUS`/`SS_VADDR` are filled from the same pair by the vector
 * (`locore.s`'s `dataabt_from_*` and `prefabt_from_*`). Reading the other class's pair would report
 * two stale numbers and make `xnu_live_sleh_frame_ok` false on every abort - i.e. turn the offset
 * control into a class indicator. These two constants are here rather than in `entry_trace.c` because
 * `entry_stubs.c`'s comment names them too, and because a value two files reason about is written
 * once. */
#define STAGE90_T_PREFETCH_ABT  3u
#define STAGE90_T_DATA_ABT      4u

#endif /* STAGE90_ENTRY_SAVED_STATE_H */
