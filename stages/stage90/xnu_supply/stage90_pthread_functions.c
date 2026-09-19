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
 * **Every named slot points at a stand-in that stops the run and names itself**, and this is the
 * rule the whole walk has used for missing symbols, applied to a table:
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

#define STAGE90_PTHREAD_SLOT_DEF(name)                                          \
    static void stage90_pthread_slot_##name(void)                               \
    {                                                                           \
        entry_stub_hit("stage90_pthread_functions." #name,                      \
                       (uint32_t)(uintptr_t)__builtin_return_address(0));       \
    }

#define STAGE90_PTHREAD_SLOT_ENTRY(name) .name = (void *)&stage90_pthread_slot_##name,

/* Every name below is a member of `struct pthread_functions_s`, in declaration order. */
STAGE90_PTHREAD_SLOT_DEF(pthread_init)
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
    STAGE90_PTHREAD_SLOT_ENTRY(pthread_init)
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
