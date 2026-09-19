/*
 * `struct pthread_functions_s`, supplied by this image because the thing that supplies it in a real
 * kernel - `pthread.kext` - does not exist here (experiment 432).
 *
 * Why this file exists
 * --------------------
 * 431's run left `vfsinit` for the first time and went ten calls down `bsd_init`'s statement list,
 * then stopped on a `panic` rather than on a stub:
 *
 *     bsd/kern/pthread_shims.c:271   void pthread_init(void)
 *     bsd/kern/pthread_shims.c:275       if (!pthread_functions)
 *     bsd/kern/pthread_shims.c:276           panic("pthread kernel extension not loaded (function
 *                                                     table is NULL).");
 *     bsd/kern/pthread_shims.c:277       pthread_functions->pthread_init();
 *
 * `pthread_functions` (`pthread_shims.c:703`) is a real 4-byte `.bss` pointer whose only writer is
 * `pthread_kext_register` (`:712`), and that function's only caller in the whole source tree is
 * `pthread.kext`. No object in this project's 695-object pool defines or calls it, and no object
 * could: the kext is an Apple binary that is not in the tarball. So the frontier is a guard on a
 * value that nothing in this image writes, and *no link can move it* - the only way past it is to
 * write the value, which is what this file does.
 *
 * This is therefore the first step in this walk whose object is not a stand-in for a missing
 * *symbol* but the supply of a missing *table*: the shape a kext would have, minus the kext.
 *
 * The layout, and why it is Apple's header rather than a copy of it
 * ----------------------------------------------------------------
 * The table is declared as `struct pthread_functions_s` from `<sys/pthread_shims.h>` - the same
 * header `bsd/kern/pthread_shims.c` was compiled against, in the same tree, by the same compiler
 * with the same include roots (this object is built by `tools/build_xnu_arm_kernel.sh`'s platform
 * block for exactly that reason). **There is no hand-written mirror of the struct, and that is the
 * point**: a second definition of a layout is the defect this project has a memory about
 * (`mi4-one-value-two-definitions`), and a table whose slot offsets drifted from the kernel's view
 * would not fail - it would call a function with the wrong arguments, or call whatever the next
 * word happened to be.
 *
 * The layout was measured before this file was written, and two independent facts pin it:
 *
 *   `sizeof(struct pthread_functions_s)` = 508 = 0x1FC = (1 + 39 + 87) words
 *   `offsetof(struct pthread_functions_s, version)` = 0
 *   `offsetof(struct pthread_functions_s, pthread_init)` = 4
 *   `offsetof(struct pthread_functions_s, _pad)` = 160, i.e. 40 words of named members
 *
 * and, from the *kernel's own compiled code* rather than from the header, `pthread_init`'s body
 * (`out/xnu_kernel_obj/bsd_kern_pthread_shims.o`):
 *
 *     c:  ldr  r0, [r4]        r4 = &pthread_functions
 *    20:  bl   <panic>         the guard this file exists to pass
 *    28:  ldr  r0, [r0, #4]    <- the slot, at byte offset 4
 *    2c:  pop  {r4, lr}
 *    30:  bx   r0              a tail branch, so the callee's lr is pthread_init's caller
 *
 * Apple's own static assert in that file (`pthread_shims.c:71`) checks the trailing region the same
 * way this measurement does: `sizeof(...) - offsetof(..., psynch_rw_yieldwrlock) - sizeof(void *)`
 * is `sizeof(void *) * 100` - and 508 - 104 - 4 is 400. Two readings, one number.
 *
 * `<sys/eventvar.h>` is included first, and the order is load-bearing rather than tidy. Apple's tree
 * has a circular include here: `pthread_shims.h:40` includes `<sys/user.h>`, whose `:89` includes
 * `<sys/eventvar.h>`, whose `:71` includes `<sys/pthread_shims.h>` again - a no-op, because the
 * guard is already set - and then `eventvar.h:175` uses `workq_threadreq_t`, which only
 * `pthread_shims.h:56` defines and which therefore does not exist yet. Including `eventvar.h` first
 * lets its own `:71` complete the shims header before it reaches `:175`, which is the only order in
 * which this header can be included at all from a translation unit that does not already carry the
 * kernel's BSD include chain. Measured, not assumed: the other order fails with
 * "field has incomplete type 'struct workq_threadreq_s'" at `eventvar.h:175`.
 *
 * What fills the table
 * --------------------
 * **Every named slot points at a stand-in that stops the run and names itself** - with one exception,
 * `pthread_init`, which 433 gives a body (see the section at the end of this header, and why it is the
 * exception) - and this is the rule the whole walk has used for missing symbols, applied to a table:
 *
 *   - a **NULL** slot is a fault rather than a stop. It would be a data abort or a branch to 0,
 *     which this image reports as `abort_entries != 0` and a `first_dfar` - a shape that says
 *     something went wrong but not *what was expected to be there*;
 *   - a **silent no-op** would be the wrong-value hazard (`mi4-stand-in-size-is-not-value`): the
 *     kernel would be told the call succeeded and would proceed on work nobody did.
 *
 * A stand-in that stops converts both into the walk's ordinary report - `stub_hit=<slot>` and the
 * caller key, which names the call site since experiment 244. The name is prefixed with the table's
 * own name so that the report says which of the kernel's many missing things was reached:
 *
 *     stub_hit=stage90_pthread_functions.pthread_init
 *     xnu_entry_stub_caller_v=0x8003B1E8
 *
 * which is `bsd_init + 0x7F8` - the return address of `bl <pthread_init>` at `bsd_init + 0x7F4` -
 * because `pthread_init` reaches the slot through a tail branch (`pop {r4, lr}; bx r0` above), so
 * the stand-in is entered with `pthread_init`'s own caller in `lr`.
 *
 * `entry_stub_hit` is defined by this image's own `entry_stubs.c` and not by any pool object, which
 * makes this pool object the first one in the walk whose call returns into the entry image. That is
 * not a hazard: pass 1 of `build_entry.sh` links `entry_stubs.o` with the pool, so the name is
 * resolved there and no generated stub is made for it (a generated one would collide with the real
 * definition and fail the link - the same reason `panic` is deliberately not generated).
 *
 * The registration
 * ----------------
 * `pthread_kext_register(&table, &callbacks)` is called from an `.init_array` constructor, which is
 * how Apple's own kernel runs this class of work: `kernel_bootstrap` -> `PE_init_iokit` ->
 * `StartIOKit` -> `OSlibkernInit` -> `OSRuntimeInitializeCPP` walks `.init_array` in link order,
 * long before `bsd_init` calls `pthread_init`. This object is linked *before*
 * `ENTRY_LAST_KERNEL_CONSTRUCTOR_OBJ`, so it runs before `last_kernel_constructor()` and therefore
 * before `iokit_post_constructor_init()` - which is where the C++ machinery finishes; nothing here
 * needs it.
 *
 * `callbacks` must be non-NULL: `pthread_kext_register` panics on NULL (`pthread_shims.c:721`), and
 * what it does with it is write the kernel's address into the caller's variable
 * (`*callbacks = &pthread_callbacks`). A `.bss` variable in this object is the honest place for
 * that write to land - the kernel's own table is still where it was, and nothing in this boot reads
 * the copy.
 *
 * The check the file adds
 * -----------------------
 * Before registering, the constructor walks the table's named region (words 0..39) and stops if any
 * word is NULL, reporting **the word index** in the caller field. That is the check for the one
 * mistake this file can make but cannot see at compile time: **a slot forgotten in the list below**
 * is a missing initializer, which C silently zeroes. With the scan, a forgotten slot is a stop that
 * says `stage90_pthread_functions.null_slot` and names the word - at registration, before anything
 * can be called through it - rather than a branch to zero at whatever point the kernel first uses
 * that slot. `version` is word 0 and is 1, so a NULL there is reported the same way.
 *
 * The `__unused1`/`__unused2`/`__unused3` slots get stand-ins like the rest even though nothing in
 * this tree calls them: Apple's own kext leaves them NULL, so the scan would have to special-case
 * them, and a table with a hole in it invites exactly the fault this file is written to avoid.
 *
 * Compiled by `tools/build_xnu_arm_kernel.sh` (see its platform block) into
 * `out/xnu_platform_obj/stage90_pthread_functions.o`, and linked into the entry image by
 * `build_entry.sh` as `STAGE90_ENTRY_STAGE90_PTHREAD_FUNCTIONS_OBJ`.
 *
 * 433: the one slot with a body
 * ----------------------------
 * 432's run measured what the paragraph above says it would: it stopped at
 * `stage90_pthread_functions.pthread_init` with the caller key `0x8003B1E8` - the return address of
 * the `bl <pthread_init>` at `bsd_init + 0x7F4` - and **no panic**. The table therefore works, and
 * from here on it is itself the frontier: `bsd_init` cannot get past that call while the slot stops,
 * so *no object linked into `LINK_OBJS` can move the boot any further*, and the next name the walk
 * answers (`nwk_wq_init`, `bsd_init + 0x808`) is unreachable until this slot returns.
 *
 * So 433 gives exactly **one** slot a body: `pthread_init`. Every other slot keeps its stand-in and
 * its stop, and this is the rule the file is not breaking rather than the rule it is bending -
 * "a silent no-op is the wrong-value hazard" is about *state*, and the hazard is a kernel that is
 * told work happened when it did not, with nothing in the log to say so. The body below is not that:
 *
 *   - it **records** that it ran, with the one number that makes the record worth having - the value
 *     of the kernel's own `pthread_functions` pointer, which must be the address of this table if the
 *     registration of the step before this one reached the kernel;
 *   - and it **stops** if that pointer is not this table, rather than returning into a boot whose
 *     premise did not hold.
 *
 * What it does *not* do is pretend to be the kext's initializer: `pthread.kext`'s `pthread_init`
 * builds the kext's own hash tables and workqueue state, and none of that exists here - there is no
 * kext. The kernel-side contract of the call is `pthread_shims.c:277`, `pthread_functions->
 * pthread_init()`, and the function's return type is `void`: nothing in this image consumes a value
 * from it. The one kernel-side fact the call establishes is that the pthread subsystem is considered
 * initialised, and the honest implementation of that in a kernel with no pthread kext is an empty
 * body that says so in the log.
 *
 * **Prediction, written before the build: `stub_hit=nwk_wq_init`, caller key `0x8003B1FC`** - the
 * return address of the `bl <nwk_wq_init>` at `bsd_init + 0x808`, one call after `pshm_cache_init`
 * in the disassembly above. With the slot returning, the four calls between it and `nwk_wq_init` run
 * for the first time in this walk - `pshm_cache_init` (`bsd_init + 0x7F8`), `psem_cache_init`
 * (`+0x7FC`), `time_zone_slock_init` (`+0x800`), `select_waitq_init` (`+0x804`) - and all four are
 * real since 423 and clean on their straight-line paths, so the stop is the *next* stub on the line,
 * which is the name `--root bsd_init` has answered since 425.
 *
 * **And the count of what the run should *not* contain is the positive evidence**, in the same shape
 * as 432's: no `panic`, no `exception:`, and no `stage90_pthread_functions.pthread_init` line.
 *
 * Falsifiers, named in advance: a stop still at `stage90_pthread_functions.pthread_init`, which would
 * mean the slot is not the one the kernel reads - and is distinguishable from the constructor never
 * having registered the table by whether `xnu_entry_stage90_pthread_functions_ptr` came out as the
 * table's address; a stop at `stage90_pthread_functions.not_registered`, which would say the value in
 * the kernel is not this table; a stop inside one of the four bodies above, which 432's stop never let
 * run; a stop on `pshm_cache_init`'s or `psem_cache_init`'s own `hashinit`/`__MALLOC` guarded path
 * (both objects carry one); a stop at `select_waitq_init`'s `waitq_init + 0x3C -> hw_lock_init`,
 * which the walk cannot name; and a stop on a *different* stub entirely, which would mean the table's
 * slot order and the kernel's disagree.
 *
 * **Measured on hardware: exactly as predicted.**
 *
 *     xnu_entry_stage90_pthread_functions_ptr=0x80231228      <- the table's own linked address
 *     stub_hit=nwk_wq_init
 *     xnu_entry_stub_caller=0x8003b1fc                        <- bsd_init + 0x80C
 *
 * `0x80231228` is what the host's `nm` says `stage90_pthread_functions` is, so the pointer the kernel
 * holds is this table and `not_registered` did not fire - two routes to the same fact, the image's own
 * read and the host's symbol table. Five calls of `bsd_init` were retired by the one word this step
 * changed, and the run contains neither 431's `panic` nor 432's `stage90_pthread_functions.pthread_init`
 * line. The other 38 slots keep their stand-ins and will stay a stop until this project implements
 * them; nothing on the boot's path calls one yet, and `pthread_shims.c`'s shims are the call sites that
 * will.
 *
 * **And the alternative was measured rather than assumed.** The other candidate - keep the stop and
 * link the object that defines `nwk_wq_init` - was checked against the image and does not work:
 * `bsd_init`'s `bl <pthread_init>` is four `bl`s *before* `bl <nwk_wq_init>`, so the run stops at the
 * slot every time and the frontier never reaches `nwk_wq_init`. 432's write-up says what 433 links;
 * this file is where that turns out to be wrong, and the reason is the one this paragraph is about.
 */

#include <sys/eventvar.h>          /* first: see the circular-include note in the header comment */
#include <sys/pthread_shims.h>     /* Apple's own definition of the table - the layout, not a copy */

#include <stddef.h>
#include <stdint.h>

/*
 * Reporting, exactly as the generated stand-ins in `build_entry.sh` do it: the name is a string
 * literal copied into the entry image's own buffer, and the caller is `lr` read before anything else
 * has touched it, so the call site is `caller - 4`. Marked `noreturn`-free on purpose - the
 * generated stubs are not, and this file matches their shape rather than improving on it.
 */
extern void entry_stub_hit(const char *name, uint32_t caller);

/*
 * The entry image's own record writer, for the same reason and by the same mechanism: it is defined
 * by `entry_stubs.c`, so pass 1 of `build_entry.sh` resolves it and no stub is generated for it.
 */
extern void entry_kv(const char *key, uint32_t value);

/*
 * **433: the only slot in this table with a body** - see the file header, and the definition below
 * the table, which is where it has to be: it compares `pthread_functions` against the table, so the
 * table has to be declared before it. It cannot dereference `pthread_functions` (a NULL or wrong
 * pointer must be a stop, not a fault), so it reads the pointer and compares it, and it writes the
 * pointer into the report as the evidence that the registration reached the kernel.
 */
static void stage90_pthread_functions_init(void);

#define STAGE90_PTHREAD_SLOT_DEF(name)                                          \
    static void stage90_pthread_slot_##name(void)                               \
    {                                                                           \
        entry_stub_hit("stage90_pthread_functions." #name,                      \
                       (uint32_t)(uintptr_t)__builtin_return_address(0));       \
    }

#define STAGE90_PTHREAD_SLOT_ENTRY(name) .name = (void *)&stage90_pthread_slot_##name,

/*
 * Every name below is a member of `struct pthread_functions_s`, in declaration order - **except
 * `pthread_init`**, which is defined by hand below the table and set explicitly in it. The lists here
 * therefore hold 38 of the table's 39 named slots, and the constructor's scan still covers all 40
 * named words (39 slots plus `version`), so a slot missing from either place is still a stop that
 * names its word index rather than a branch to zero.
 */
STAGE90_PTHREAD_SLOT_DEF(fill_procworkqueue)
STAGE90_PTHREAD_SLOT_DEF(__unused1)
STAGE90_PTHREAD_SLOT_DEF(__unused2)
STAGE90_PTHREAD_SLOT_DEF(workqueue_exit)
STAGE90_PTHREAD_SLOT_DEF(workqueue_mark_exiting)
STAGE90_PTHREAD_SLOT_DEF(workqueue_thread_yielded)
STAGE90_PTHREAD_SLOT_DEF(pth_proc_hashinit)
STAGE90_PTHREAD_SLOT_DEF(pth_proc_hashdelete)
STAGE90_PTHREAD_SLOT_DEF(bsdthread_create)
STAGE90_PTHREAD_SLOT_DEF(bsdthread_register)
STAGE90_PTHREAD_SLOT_DEF(bsdthread_terminate)
STAGE90_PTHREAD_SLOT_DEF(thread_selfid)
STAGE90_PTHREAD_SLOT_DEF(workq_kernreturn)
STAGE90_PTHREAD_SLOT_DEF(workq_open)
STAGE90_PTHREAD_SLOT_DEF(psynch_mutexwait)
STAGE90_PTHREAD_SLOT_DEF(psynch_mutexdrop)
STAGE90_PTHREAD_SLOT_DEF(psynch_cvbroad)
STAGE90_PTHREAD_SLOT_DEF(psynch_cvsignal)
STAGE90_PTHREAD_SLOT_DEF(psynch_cvwait)
STAGE90_PTHREAD_SLOT_DEF(psynch_cvclrprepost)
STAGE90_PTHREAD_SLOT_DEF(psynch_rw_longrdlock)
STAGE90_PTHREAD_SLOT_DEF(psynch_rw_rdlock)
STAGE90_PTHREAD_SLOT_DEF(psynch_rw_unlock)
STAGE90_PTHREAD_SLOT_DEF(psynch_rw_wrlock)
STAGE90_PTHREAD_SLOT_DEF(psynch_rw_yieldwrlock)
STAGE90_PTHREAD_SLOT_DEF(workqueue_get_sched_callback)
STAGE90_PTHREAD_SLOT_DEF(bsdthread_register2)
STAGE90_PTHREAD_SLOT_DEF(bsdthread_ctl)
STAGE90_PTHREAD_SLOT_DEF(workq_reqthreads)
STAGE90_PTHREAD_SLOT_DEF(thread_qos_from_pthread_priority)
STAGE90_PTHREAD_SLOT_DEF(get_pwq_state_kdp)
STAGE90_PTHREAD_SLOT_DEF(__unused3)
STAGE90_PTHREAD_SLOT_DEF(pthread_priority_canonicalize2)
STAGE90_PTHREAD_SLOT_DEF(workq_thread_has_been_unbound)
STAGE90_PTHREAD_SLOT_DEF(pthread_find_owner)
STAGE90_PTHREAD_SLOT_DEF(pthread_get_thread_kwq)
STAGE90_PTHREAD_SLOT_DEF(workq_threadreq)
STAGE90_PTHREAD_SLOT_DEF(workq_threadreq_modify)

static const struct pthread_functions_s stage90_pthread_functions = {
    .version = PTHREAD_FUNCTIONS_TABLE_VERSION,
    /* 433: the one slot with a body - see the file header. Not a stand-in. */
    .pthread_init = &stage90_pthread_functions_init,
    STAGE90_PTHREAD_SLOT_ENTRY(fill_procworkqueue)
    STAGE90_PTHREAD_SLOT_ENTRY(__unused1)
    STAGE90_PTHREAD_SLOT_ENTRY(__unused2)
    STAGE90_PTHREAD_SLOT_ENTRY(workqueue_exit)
    STAGE90_PTHREAD_SLOT_ENTRY(workqueue_mark_exiting)
    STAGE90_PTHREAD_SLOT_ENTRY(workqueue_thread_yielded)
    STAGE90_PTHREAD_SLOT_ENTRY(pth_proc_hashinit)
    STAGE90_PTHREAD_SLOT_ENTRY(pth_proc_hashdelete)
    STAGE90_PTHREAD_SLOT_ENTRY(bsdthread_create)
    STAGE90_PTHREAD_SLOT_ENTRY(bsdthread_register)
    STAGE90_PTHREAD_SLOT_ENTRY(bsdthread_terminate)
    STAGE90_PTHREAD_SLOT_ENTRY(thread_selfid)
    STAGE90_PTHREAD_SLOT_ENTRY(workq_kernreturn)
    STAGE90_PTHREAD_SLOT_ENTRY(workq_open)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_mutexwait)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_mutexdrop)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_cvbroad)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_cvsignal)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_cvwait)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_cvclrprepost)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_rw_longrdlock)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_rw_rdlock)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_rw_unlock)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_rw_wrlock)
    STAGE90_PTHREAD_SLOT_ENTRY(psynch_rw_yieldwrlock)
    STAGE90_PTHREAD_SLOT_ENTRY(workqueue_get_sched_callback)
    STAGE90_PTHREAD_SLOT_ENTRY(bsdthread_register2)
    STAGE90_PTHREAD_SLOT_ENTRY(bsdthread_ctl)
    STAGE90_PTHREAD_SLOT_ENTRY(workq_reqthreads)
    STAGE90_PTHREAD_SLOT_ENTRY(thread_qos_from_pthread_priority)
    STAGE90_PTHREAD_SLOT_ENTRY(get_pwq_state_kdp)
    STAGE90_PTHREAD_SLOT_ENTRY(__unused3)
    STAGE90_PTHREAD_SLOT_ENTRY(pthread_priority_canonicalize2)
    STAGE90_PTHREAD_SLOT_ENTRY(workq_thread_has_been_unbound)
    STAGE90_PTHREAD_SLOT_ENTRY(pthread_find_owner)
    STAGE90_PTHREAD_SLOT_ENTRY(pthread_get_thread_kwq)
    STAGE90_PTHREAD_SLOT_ENTRY(workq_threadreq)
    STAGE90_PTHREAD_SLOT_ENTRY(workq_threadreq_modify)
};

/*
 * **433: the only slot with a body.** Defined here, below the table, because it names the table.
 *
 * What it does is the whole of what this image can honestly do for `pthread_init`: record that the
 * call arrived, with the one number that makes the record worth having - `pthread_functions`, the
 * kernel's own pointer, which is the address of the table above exactly when the registration of the
 * previous step reached the kernel. If it is not, the run stops rather than returning into a boot
 * whose premise did not hold.
 *
 * It does not dereference the pointer. A NULL `pthread_functions` is a stop that names itself; a
 * fault here would be `abort_entries != 0` and a `first_dfar`, which says something went wrong but
 * not what was expected to be there - the same distinction every stand-in in this table is built on.
 *
 * `void` return, and nothing in the kernel consumes a value from the call (`pthread_shims.c:277` is
 * `pthread_functions->pthread_init();` as a statement). The kext's own initializer builds the kext's
 * hash tables and workqueue state; there is no kext here, so the honest implementation of "the
 * pthread subsystem is initialised" is a body that says so in the log and returns.
 */
static void stage90_pthread_functions_init(void)
{
    entry_kv("xnu_entry_stage90_pthread_functions_ptr", (uint32_t)(uintptr_t)pthread_functions);

    if (pthread_functions != &stage90_pthread_functions) {
        /* The pointer is already in the report, one record above: 0 means it was never registered. */
        entry_stub_hit("stage90_pthread_functions.not_registered", 0u);
    }
}

/* Where `pthread_kext_register` writes the kernel's own callbacks table. Nothing here reads it. */
static pthread_callbacks_t stage90_pthread_callbacks;

/*
 * The named region is `version` plus the 39 slots; `_pad` is Apple's reserved tail and is the one
 * part of the table that is legitimately zero.
 */
#define STAGE90_PTHREAD_NAMED_WORDS (offsetof(struct pthread_functions_s, _pad) / sizeof(void *))

static void stage90_pthread_functions_register(void) __attribute__ ((constructor));
static void stage90_pthread_functions_register(void)
{
    const void *const *word = (const void *const *)&stage90_pthread_functions;
    size_t i;

    for (i = 0; i < STAGE90_PTHREAD_NAMED_WORDS; i++) {
        if (word[i] == NULL) {
            entry_stub_hit("stage90_pthread_functions.null_slot", (uint32_t)i);
        }
    }

    pthread_kext_register(&stage90_pthread_functions, &stage90_pthread_callbacks);
}
