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
 * **Every named slot points at a stand-in that stops the run and names itself** - with five exceptions,
 * each given a body by the step whose device run stopped in that slot and each with its own section
 * below: `pthread_init` (433), `pth_proc_hashinit` (465), `workqueue_mark_exiting` with
 * `workqueue_exit` (473), and `pth_proc_hashdelete` (507) - and this is the rule the whole walk has used
 * for missing symbols, applied to a table:
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
 * The live channel, which is the ram console itself. Declared through `entry_note_live` rather than
 * `entry_live_write` because the latter is inside `entry_stubs.c`'s `STAGE90_ENTRY_TRACE` guard and
 * this object is compiled by `tools/build_xnu_arm_kernel.sh` with flags that do not set it; the
 * wrapper is defined outside the guard and is a no-op when the trace is not compiled in.
 */
extern void entry_note_live(const char *key, uint32_t value);

/*
 * ================================================================================================
 * **465: the second slot with a body - `pth_proc_hashinit`, which is the slot 464's run stopped in.**
 *
 * 464 ended at `forkproc+0x5a4`'s `bl <pth_proc_hashinit>` (`bsd/kern/kern_fork.c:1393`, under
 * `#if PSYNCH`), reached from `bsd_init`'s last statement through `bsd_utaskbootstrap` ->
 * `cloneproc` -> `forkproc`. The shim is Apple's own twenty bytes, `pthread_shims.c:358-360` -
 * `movw`/`movt` of `pthread_functions`, `ldr r1, [r1]`, **`ldr r1, [r1, #32]`**, `bx r1` - so the
 * slot is table word **8** and the entry below is the one the kernel reads.
 *
 * **464's write-up reads that same instruction as `[r1, #0x18]` and calls it "word 6". It is wrong,
 * and it is worth naming the way it is wrong rather than quietly fixing it**: word 6 is
 * `workqueue_mark_exiting`, so the stated evidence contradicted the stated conclusion, and the
 * conclusion was right for a different reason - `nm` names `0x801f0a5c` `pth_proc_hashinit`, and the
 * caller is the `#if PSYNCH` line of `forkproc`. The offset is read off the disassembly by eye; the
 * identification is not. Recorded in the defects ledger.
 *
 * **What the real slot does, and what this image can honestly answer.** The kext's
 * `pth_proc_hashinit` builds the per-process psynch waitqueue hash - `hashinit()`-backed storage
 * reached through the proc's `p_pthhash` field via the *kernel's* half of the table
 * (`proc_set_pthhash`) - and the only readers of that storage are psynch's own syscalls, which are
 * seven more slots in the table above and are unreachable in a boot with no userland. There is no
 * kext here, so:
 *
 *   - the body **does not dereference `p`** (433's rule, and the reason is the same: a NULL or
 *     wrong pointer must be a stop that names itself, not a fault), it records the pointer;
 *   - it **does not call the callbacks table**: `proc_set_pthhash(p, NULL)` would write NULL over a
 *     field that `forkproc`'s own `bzero` has already made NULL, and every call into the kernel is a
 *     new way for this step to fault;
 *   - and it leaves `p->p_pthhash` NULL, which is the value the psynch slots would find anyway.
 *
 * **The record goes to the live channel as well as to the buffer, and that is a measurement rather
 * than a preference.** 464's report reads `xnu_entry_kv_written = xnu_entry_kv_in_dram = 0x2000`
 * (8192 of 8192) with `xnu_entry_kv_dropped = 0x8080` (32896 refusals), and the buffer's own
 * content is *early-boot* allocations only - `t268_kalloc_ret` from `0xc05c0520` to `0xc05d9800`,
 * against the frontier's own objects at `0xc06062e8`/`0xc06088c0`. So the tracer's 8 KB buffer has
 * been full since the IOKit bring-up, and **the `entry_kv` record 433 wrote for `pthread_init`
 * (`bsd_init.c:798`) is absent from 464's report for that reason and no other** - not because
 * `pthread_init` did not run. A body whose only record is an `entry_kv` record is a body nothing
 * will see in a boot this long, which is why the live records are written first here and kept.
 *
 * **Prediction, written before the build.** The live channel carries five new records at the slot:
 * `xnu_live_pth_hashinit_seq = 1`, `xnu_live_pth_hashinit_p` a `0xc0...` pointer in the band
 * `forkproc`'s other allocations occupy, `xnu_live_pth_hashinit_tbl` equal to whatever `nm` says
 * `stage90_pthread_functions` is **in the build that runs** (it was `0x80499670` in 464's image, and
 * the table moves with the image - the number to compare against is the new link's, `0x80499978`),
 * and then, because this slot is now retired, **no**
 * `stub_hit=stage90_pthread_functions.pth_proc_hashinit` anywhere in the log. The walk
 * (`tools/xnu_entry_callwalk.py --root forkproc`, and the same for `cloneproc` and
 * `bsd_utaskbootstrap`, against 464's ELF) answers **no stub on the straight-line path** from any of
 * the three, so the next stop - if the boot stops at all - is either a guarded branch the walk lists
 * (`forkproc+0x98 -> thread_call_allocate`, `forkproc+0xdc -> lck_mtx_lock`) or an indirect call it
 * cannot follow. The path the source gives is `bsd_utaskbootstrap`'s tail (`proc_find(1)`,
 * `proc_signalend`, `proc_transend`, `get_bsdthread_info`, `act_set_astbsd`,
 * `task_clear_return_wait`), then `bsd_init`'s tail (`pal_kernel_announce`, `mountroot_post_hook`),
 * then - on the AST that `act_set_astbsd` sets - `bsd_ast`'s `if (!bsd_init_done) bsdinit_task();`
 * (`kern_sig.c:3443-3446`), whose `load_init_program(p)` (`kern_exec.c:5119`) is the first place
 * this boot could print **new OS console text**: `load_init_program: attempting to load %s`
 * (`kern_exec.c:5141`), the line 464's own document named as "the next print on this path".
 *
 * Falsifiers, named in advance:
 *
 *   (a) a stop still at `stage90_pthread_functions.pth_proc_hashinit` - the slot is not the one the
 *       kernel reads, or the table was not relinked;
 *   (b) no `xnu_live_pth_hashinit_seq` **and** no `stub_hit` - the shim's `bx r1` went somewhere that
 *       is neither this slot nor a stand-in (which is what a wrong offset would look like);
 *   (c) an XNU `panic()` with no `stub_hit` at all - the fourth kind of stop, the one that names its
 *       own cause, which is what a body that returned into a broken caller would produce;
 *   (d) `xnu_live_capped` in the log, or `xnu_live_pth_hashinit_seq` absent with the boot otherwise
 *       continuing - the live channel is capped at `ENTRY_LIVE_CAP` records (8192 since 500; 4096 then) and 464's run wrote ~756, so this is
 *       the *instrument* failing rather than the step;
 *   (e) and, for the record this whole section exists for: the terminal `stub_hit` record naming a
 *       *truncated* symbol again - `xnu_live_stub_hit_name_w0`/`_w1` and `..._seq` are the fix, and a
 *       run that stops with a cut name and no live name words would say the fix did not take.
 */

/*
 * ================================================================================================
 * **473: the third and fourth slots with bodies - the exec path's two workqueue notifications, which
 * 472's run stopped in and which are the last thing between this boot and process 1 running in User
 * mode.**
 *
 * What 472 changed and what it measured
 * -------------------------------------
 * 472 cancelled `SECURE_KERNEL` (`-USECURE_KERNEL` in `build_xnu_arm_kernel.sh`'s cancellation list),
 * which 471 had identified as the reason `load_machfile` refused the RAM disk's Mach-O: `cs_enforcement`
 * is `const int cs_enforcement_enable = 1` in a secure kernel and that kernel cannot activate a file
 * with no `LC_CODE_SIGNATURE`. The rebuild moved that object from `R` to `B` (the build now checks it:
 * `cs_enforcement_enable is B (writable) and cs_enforcement_disable is a boot arg`), and the run moved
 * with it:
 *
 *     xnu_entry_stub_hit_count = 0x00000001
 *     xnu_live_stub_hit_name_ptr = 0x804b3da9
 *     xnu_live_stub_hit_name_w0/_w1 = 0x67617473 / 0x5f303965     "stag" / "e90_" -> the string below
 *     xnu_live_stub_hit_caller   = 0x8029e764
 *     xnu_entry_stub_caller_v    = 0x8029e764
 *
 * and `0x804b3da9` reads out of `out/stage90/xnu_arm_entry.elf`'s `.text` as
 * **`stage90_pthread_functions.workqueue_mark_exiting`**. The caller resolves in the same ELF to
 * `load_machfile + 0x354` (471's `0x80284bb4` was the same instruction in 471's shorter image, which is
 * why the key is an address and never a name).
 *
 * **The caller is `load_machfile` and not this table's own shim, because the shim is a tail branch.**
 * `bsd/kern/pthread_shims.c:337-340` is
 *
 *     void workqueue_mark_exiting(struct proc *p) { pthread_functions->workqueue_mark_exiting(p); }
 *
 * which the compiler emits as a load of the table then `bx` - no `bl`, so nothing writes `lr` and the
 * stand-in is entered with `load_machfile`'s return address still in it. 433 measured the same shape
 * for `pthread_init`'s slot (`pop {r4, lr}; bx r0`, so the caller key is `bsd_init`'s return address).
 *
 * Where in the exec that is, and why it is the measurement that matters
 * ------------------------------------------------------------------
 * The call site is `bsd/kern/mach_loader.c:511`, inside the block that begins with the comment "If this
 * is an exec, then we are going to destroy the old task, and it's correct to halt it; if it's spawn,
 * the task is not yet running, and it makes no sense" and reads
 *
 *     if (in_exec) { ... task_start_halt(task); proc_transcommit(p, 0);
 *                     workqueue_mark_exiting(p); task_complete_halt(task); workqueue_exit(p);
 *                     task_rollup_accounting_info(get_threadtask(thread), task); }
 *     *mapp = map;
 *     return (LOAD_SUCCESS);
 *
 * (`:493-519`) - **the block immediately before `load_machfile` returns `LOAD_SUCCESS`.** 471's run
 * returned `LOAD_FAILURE` from inside `parse_machfile` and never reached this code: its `lmf_ret` was
 * `0x04` with `lmf_caller` set. In 472 the two `load_machfile` keys are **both zero**, and that is not
 * `LOAD_SUCCESS` - it is the wrapper's record never happening, because the run stopped inside the
 * function the wrapper wraps. The distinction is the one the keys cannot make on their own and the
 * stub report makes instead: with `lmf_caller = 0` there is no call to read, and
 * `stub_hit = ...workqueue_mark_exiting` at `load_machfile + 0x354` is the evidence that the call
 * arrived. **So the file was accepted**: the magic, the segments, the thread state and the signature
 * gate all passed, and the exec got as far as the transition that replaces the old task.
 *
 * **And nothing else on that path is a generated stub.** `tools/xnu_entry_callwalk.py --root
 * load_machfile` reports "reached no stub on the straight-line path", and so do `--root bsd_ast` and
 * `--root load_init_program`; the calls after the two workqueue slots in `exec_mach_imgact` are
 * `cpu_type`, `vm_map_exec`, `fdexec`, `exec_handle_sugid`, `swap_task_map`, `activate_exec_state`,
 * `thread_set_mach_voucher`, `create_unix_stack`, `exec_add_apple_strings`, `exec_copyout_strings`,
 * `thread_setuserstack` and `copyoutptr`, every one of them real. The calls *between* the two slots -
 * `task_complete_halt` and, after them, `get_threadtask` and `task_rollup_accounting_info` - are real
 * too, and none of the remaining 26 names in `out/stage90/xnu_arm_entry_undef.txt` is on this path.
 * The only stops left in front of user mode are these two words of this table.
 *
 * What the two real slots do, and why a body here is the honest answer
 * --------------------------------------------------------------------
 * `workqueue_mark_exiting(p)` and `workqueue_exit(p)` are the kernel's *notifications* to libpthread's
 * workqueue bookkeeping: the first tells it that no new work should be queued for `p`, the second that
 * it may drop what is left. Both return `void`, both are reached by name through the table above, and
 * their callers in this kernel are `mach_loader.c:511/:513` and `kern_exit.c:1067/:1081` as statements
 * with no error path and no value read back. The state they are supposed to touch - the per-proc
 * workqueue lists libpthread owns - does not exist in this image, because the kext that owns it does
 * not exist, which is the whole reason this file exists (432). So the honest implementation of "there
 * is no workqueued work to mark or to release" is a body that records the call and returns, exactly as
 * 433's is for `pthread_init` and 465's for `pth_proc_hashinit`.
 *
 * Neither body dereferences `p`, for 433's reason: a NULL or a wrong pointer must be a stop that names
 * itself rather than a fault, and a fault here would be `abort_entries != 0` with a `first_dfar`, which
 * says something went wrong but not what was expected to be there.
 *
 * **Both words, not one, and here is why this is one step rather than two.** `workqueue_mark_exiting`
 * and `workqueue_exit` are two calls in one basic block with only real code between them: retiring the
 * first would stop the boot at the second, one device run later, having measured nothing new - the
 * sibling is already named by the disassembly above and by the same `xnu_live_*` records. The records
 * are per-slot and counted, so the log still says which of the two was entered and how many times, and
 * a run that stops at either is still named by name. (The one thing this step does *not* do is retire
 * the other 33 slots: `workqueue_thread_yielded`, `fill_procworkqueue`,
 * `thread_qos_from_pthread_priority`, `pthread_priority_canonicalize` and
 * `workqueue_get_sched_callback` all have call sites in the pool - measured with `objdump -r` over
 * `out/xnu_kernel_obj/*.o` - but none of them is in `osfmk/kern/syscall_subr.c`'s, `kern_event.c`'s or
 * `ipc_pthread_priority.c`'s code on this path, and a slot retired before its call site is reached is
 * a body nothing measures.)
 *
 * **Prediction, written before the build: the two live records appear and the run does not stop on
 * either slot.**
 *
 *     xnu_live_pth_wqmark_seq = 1        (then, with the block completed, ...)
 *     xnu_live_pth_wqmark_p   = a 0xc0... pointer, the process 1 proc
 *     xnu_live_pth_wqmark_tbl = whatever `nm` says `stage90_pthread_functions` is in the new link
 *     xnu_live_pth_wqexit_seq = 1
 *     xnu_live_pth_wqexit_p   = the same pointer
 *
 * **and then, if nothing else stops, process 1 runs its `udf #0` in User mode** - the two-sided
 * measurement `entry_ramdisk.s` was written for, reported as `xnu_entry_undef_pc = 0x10e0` with
 * `xnu_entry_undef_spsr = 0x10`, where `0x10e0` is the Mach-O's entry point and `0x10` is
 * `PSR_USERDFLT`. The OS console's next line would be the one `load_init_program` prints after a
 * *successful* exec - nothing, because `kern_exec.c:5146` only prints `failed loading` on failure -
 * and the process-1 `udf` is then the trap rather than the panic.
 *
 * Falsifiers, named in advance:
 *
 *   (a) a stop whose `xnu_live_stub_hit_name_ptr` reads back as one of these two names - the slot is
 *       not the word the kernel reads, or the table was not relinked;
 *   (b) no `xnu_live_pth_wqmark_seq` **and** no `stub_hit` at all - the shim's `bx` went somewhere that
 *       is neither a body nor a stand-in, which is what a wrong slot offset looks like;
 *   (c) an XNU `panic()` with no `stub_hit` - the fourth kind of stop, the one that names its own
 *       cause, which is what a body that returned into a broken caller would produce;
 *   (d) `xnu_live_pth_wqmark_seq` present but `xnu_live_pth_wqexit_seq` absent with the run continuing
 *       - `task_complete_halt` between them returning without a trap would be the interesting finding,
 *       and the live channel is capped at `ENTRY_LIVE_CAP` records (8192 since 500) with ~756 written by 464's run, so a missing
 *       record is not the cap.
 */
/*
 * **The five slots with bodies, declared here and defined below the table**, which is where they have
 * to be: every one of them names `pthread_functions` or the table itself. 433's is `pthread_init` and
 * its contract is in this file's header; 465's is `pth_proc_hashinit`, whose section is above; 473's are
 * `workqueue_mark_exiting` and `workqueue_exit`, whose section is above as well; 507's is
 * `pth_proc_hashdelete`, whose section is above too. None of the five bodies dereferences the pointer it
 * is given - a NULL or wrong pointer must be a stop that names itself, not a fault - and 465's, 473's
 * and 507's keep 433's rule for their own arguments.
 */
static void stage90_pthread_functions_init(void);
static void stage90_pthread_slot_pth_proc_hashinit(proc_t p);
static void stage90_pthread_slot_pth_proc_hashdelete(proc_t p);
static void stage90_pthread_slot_workqueue_mark_exiting(proc_t p);
static void stage90_pthread_slot_workqueue_exit(proc_t p);

#define STAGE90_PTHREAD_SLOT_DEF(name)                                          \
    static void stage90_pthread_slot_##name(void)                               \
    {                                                                           \
        entry_stub_hit("stage90_pthread_functions." #name,                      \
                       (uint32_t)(uintptr_t)__builtin_return_address(0));       \
    }

#define STAGE90_PTHREAD_SLOT_ENTRY(name) .name = (void *)&stage90_pthread_slot_##name,

/*
 * Every name below is a member of `struct pthread_functions_s`, in declaration order - **except the
 * five slots that have bodies of their own** (`pthread_init`, 433; `pth_proc_hashinit`, 465;
 * `workqueue_mark_exiting` and `workqueue_exit`, 473; `pth_proc_hashdelete`, 507), which are defined by
 * hand below the table and set explicitly in it. The two lists below therefore hold **34** of the
 * table's 39 named slots, and the constructor's scan still covers all 40 named words (39 slots plus
 * `version`), so a slot missing from either place is a stop that names its word index rather than a
 * branch to zero.
 *
 * (This paragraph said "38 ... except `pthread_init`" between 465 and 473, which was wrong in one word
 * for two steps: 465 stopped defining `pth_proc_hashinit` by the macro and did not move the number. The
 * constructor's NULL scan is what makes the error harmless - a slot in neither list is NULL and named
 * at registration - and the count is written out here so that the next step does not have to re-derive
 * it from the 39 members. **507 moved it again, 35 -> 34, and that is the arithmetic to copy: the count
 * is 39 minus the bodies, and `tools/check_pthread_table_slots.py` now reads it back out of the object
 * rather than out of this paragraph.**)
 */
STAGE90_PTHREAD_SLOT_DEF(fill_procworkqueue)
STAGE90_PTHREAD_SLOT_DEF(__unused1)
STAGE90_PTHREAD_SLOT_DEF(__unused2)
/* 473: `workqueue_exit` and `workqueue_mark_exiting` are *not* defined by the macro either - they have
 * hand-written bodies below, the way `pthread_init` and `pth_proc_hashinit` do, because their slots are
 * the two this step retires. They stay in this position in the list as comments so the file still reads
 * as Apple's declaration order. */
STAGE90_PTHREAD_SLOT_DEF(workqueue_thread_yielded)
/* 465: `pth_proc_hashinit` is *not* defined by the macro - it has a hand-written body below, the
 * way `pthread_init` does, because its slot is the one this step retires. */
/* 507: `pth_proc_hashdelete` is *not* defined by the macro either - it has a hand-written body below,
 * for the same reason, and this is the second slot in this list 506's run walked past a stand-in for.
 * It stays in this position so the list still reads as Apple's declaration order. */
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
    /* 433: the first slot with a body - see the file header. Not a stand-in. */
    .pthread_init = &stage90_pthread_functions_init,
    /* 465: the second - see the section above the table. Not a stand-in either. The initializer is
     * type-correct (`void (*)(proc_t)`), so no cast is needed here, unlike the macro's entries. */
    .pth_proc_hashinit = &stage90_pthread_slot_pth_proc_hashinit,
    /* 473: the third and fourth - see the section above the table. Both are type-correct
     * (`void (*)(struct proc *)`) and so are written the same way as 465's. */
    .workqueue_mark_exiting = &stage90_pthread_slot_workqueue_mark_exiting,
    .workqueue_exit = &stage90_pthread_slot_workqueue_exit,
    /* 507: the fifth - see the section above the table. Type-correct (`void (*)(proc_t)`) like the
     * three before it, so no cast. */
    .pth_proc_hashdelete = &stage90_pthread_slot_pth_proc_hashdelete,
    STAGE90_PTHREAD_SLOT_ENTRY(fill_procworkqueue)
    STAGE90_PTHREAD_SLOT_ENTRY(__unused1)
    STAGE90_PTHREAD_SLOT_ENTRY(__unused2)
    STAGE90_PTHREAD_SLOT_ENTRY(workqueue_thread_yielded)
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
    /* 465: and on the live channel, because 464 measured the `entry_kv` record above to be
     * *invisible* in a boot this long - see the section above the table, and the buffer's own
     * `kv_dropped = 0x8080`. The value is the same one; the channel is the one that survives. */
    entry_note_live("xnu_live_pthread_init_ptr", (uint32_t)(uintptr_t)pthread_functions);

    if (pthread_functions != &stage90_pthread_functions) {
        /* The pointer is already in the report, one record above: 0 means it was never registered. */
        entry_stub_hit("stage90_pthread_functions.not_registered", 0u);
    }
}

/*
 * **465: the body of `pth_proc_hashinit`.** What it does, what it deliberately does not do, and why
 * the record goes to two channels is in the section above the table; this is the code.
 *
 * `entry_kv` is kept as well as the live records because the epilogue prints `g_kv_buf` as a block:
 * in a boot short enough for the tracer not to have filled it, the dump is where this call appears.
 * Neither channel's record dereferences `p`, and the call itself cannot change what the boot does -
 * `forkproc` ignores the return (`void`) and the statement has no error path (`kern_fork.c:1393`),
 * which is what makes a body here safe to add at all.
 */
static uint32_t stage90_pth_hashinit_calls;

static void
stage90_pthread_slot_pth_proc_hashinit(proc_t p)
{
    stage90_pth_hashinit_calls++;
    entry_note_live("xnu_live_pth_hashinit_seq", stage90_pth_hashinit_calls);
    entry_note_live("xnu_live_pth_hashinit_p", (uint32_t)(uintptr_t)p);
    entry_note_live("xnu_live_pth_hashinit_tbl", (uint32_t)(uintptr_t)pthread_functions);
    entry_kv("xnu_entry_pth_hashinit_p", (uint32_t)(uintptr_t)p);
}

/*
 * **473: the bodies of `workqueue_mark_exiting` and `workqueue_exit`.** What the two real slots do, what
 * these deliberately do not do, and why retiring both words in one step is one step, is in the section
 * above the table; this is the code, and it is 465's shape with 465's reasons.
 *
 * The one thing worth repeating here is that neither of these bodies changes what the exec does.
 * `mach_loader.c:511/:513` are statements in the `in_exec` block and both slots return `void`; the
 * `task_start_halt`/`task_complete_halt` pair around them is what actually halts the old task, and both
 * of those are real XNU functions in this image. The workqueue state the real slots maintain belongs to
 * `pthread.kext`, which does not exist here, so "there is nothing queued for `p`" is the true value and
 * an empty body is the honest way to say it - with the record, so the log says the call arrived.
 */
static uint32_t stage90_pth_wqmark_calls;
static uint32_t stage90_pth_wqexit_calls;

static void
stage90_pthread_slot_workqueue_mark_exiting(proc_t p)
{
    stage90_pth_wqmark_calls++;
    entry_note_live("xnu_live_pth_wqmark_seq", stage90_pth_wqmark_calls);
    entry_note_live("xnu_live_pth_wqmark_p", (uint32_t)(uintptr_t)p);
    entry_note_live("xnu_live_pth_wqmark_tbl", (uint32_t)(uintptr_t)pthread_functions);
    entry_kv("xnu_entry_pth_wqmark_p", (uint32_t)(uintptr_t)p);
}

static void
stage90_pthread_slot_workqueue_exit(proc_t p)
{
    stage90_pth_wqexit_calls++;
    entry_note_live("xnu_live_pth_wqexit_seq", stage90_pth_wqexit_calls);
    entry_note_live("xnu_live_pth_wqexit_p", (uint32_t)(uintptr_t)p);
    entry_note_live("xnu_live_pth_wqexit_tbl", (uint32_t)(uintptr_t)pthread_functions);
    entry_kv("xnu_entry_pth_wqexit_p", (uint32_t)(uintptr_t)p);
}

/*
 * **507: the body of `pth_proc_hashdelete` - the slot 506's run stopped in, 338 source lines before the
 * line that would have said the OS had told a parent its child was gone.**
 *
 * Where 506 stopped, and why that is this slot
 * -------------------------------------------
 * 506 made the fixture test `r1`, so the child `fork` made took its own arm and called `exit`: the run
 * measured `xnu_live_exit_seq = 1`, `_caller = 0x80285848`, `_pid = 2`, `_rval = 3`, with the parent
 * still in its loop (`getpid_count` 14 records to `0x4000`, `_last = 1` every time) and the console
 * never printing `pid 1 exited`. Then the run's *last* live record was a stub hit, and it was this one:
 *
 *     xnu_live_stub_hit_seq = 1
 *     xnu_live_stub_hit_name_ptr = 0x804c157a          <- "stage90_pthread_functions.pth_proc_hashdelete"
 *     xnu_live_stub_hit_caller   = 0x80294150          <- proc_exit + 0x188
 *
 * `proc_exit` calls `pth_proc_hashdelete(p)` at `bsd/kern/kern_exit.c:1105` under `#if PSYNCH`, and a
 * stand-in hit is terminal - `entry_stub_hit` ends in `entry_epilogue`, `entry_stubs.c:6107` - so the
 * child's exit path stopped there. Everything the step predicted downstream of it is therefore still
 * unmeasured, and the first of those is `psignal(pp, SIGCHLD)` at `kern_exit.c:1443`, **338 source lines
 * further down the same function** (`:1105` to `:1443`; 506's own documents said 275, which was an
 * estimate quoted four times rather than a distance computed from the two lines - see 507's document):
 * the OS telling process 1 that process 2 is gone, which is the reading 505's document named as "the
 * first inter-process event in this walk".
 *
 * What the real slot does, and what this image can honestly answer
 * ---------------------------------------------------------------
 * The kext's `pth_proc_hashdelete` frees the per-process psynch hash that `pth_proc_hashinit` built -
 * the storage `p->p_pthhash` points at, reached through the kernel's own `proc_set_pthhash` - and its
 * only readers are psynch's own syscalls, seven more slots in the table above. **465's body left
 * `p->p_pthhash` NULL** (it records the call and does not reach into the callbacks table, for reasons
 * its own section gives), which is exactly the state a delete would find and have nothing to free in.
 * So the honest implementation of "there is no psynch hash for this proc to drop" is a body that records
 * the call and returns - the same answer 433, 465 and 473 gave for their slots, with 465's consequence
 * stated rather than assumed: `p_pthhash` is NULL because 465 wrote nothing there, and this body leaves
 * it NULL for the same reason.
 *
 * **Like `workqueue_mark_exiting`, the shim is a tail branch** - `pthread_shims.c:364` is
 * `pthread_functions->pth_proc_hashdelete(p);`, which is the `ldr r1, [r1, #36]` / `bx r1` at
 * `0x80205d0c..0x80205d1c` in the linked image - so this body is entered with `proc_exit`'s return
 * address in the link register, which is why 506's record names `proc_exit + 0x188` rather than a
 * shim. The body does not dereference `p` (433's rule) and does not call the callbacks table.
 *
 * Prediction, written before the build: the record appears, and the run does not stop on this slot.
 *
 *     xnu_live_pth_delete_seq = 1                (the child's proc, so a 0xc0... pointer)
 *     xnu_live_pth_delete_p   = the same pointer 506's `fork` record implies, i.e. the child's proc
 *     xnu_live_pth_delete_tbl = whatever `nm` says `stage90_pthread_functions` is in the new link
 *
 * and then, if nothing else stops on the teardown, the two records this step exists for:
 *
 *     xnu_live_sigchld_signal = 20   _to = 1   _from = 2   _count >= 1
 *     xnu_live_getpid_count still climbing (pid 1 in its loop, now with a zombie child)
 *
 * **Measured: everything but `_from`.** The record, the `_to`, the signal number 20 and the climbing
 * counter all came back as written, and the run has **no `stub_hit` at all** - so the exit path is
 * retired end to end and the boot's first run without a stop ended on the hardware watchdog instead.
 * `_from` is **0**, and the reason is 72 lines above the call rather than in the wrapper:
 * `set_bsdtask_info(task, NULL)` (`kern_exit.c:1371`, in the block that clears `p->task`) means the
 * dying process is no longer what `current_proc()` answers - its fallback arm returns `kernproc`
 * (`bsd_stubs.c:104`, "Never returns a NULL"), whose pid is 0. The wrapper's null guard, written for a
 * signal with no BSD process behind it, fired for a real one on its first use. The correction is the
 * reading: the sender of a `psignal` taken in `proc_exit` cannot name the process that died, and the
 * pid is on the other side - the `exit` record's `_pid = 2`.
 *
 * Falsifiers, named in advance:
 *
 *   (a) a stop whose `xnu_live_stub_hit_name_ptr` reads back as this name again - the slot is not the
 *       word the kernel reads, or the table was not relinked;
 *   (b) no `xnu_live_pth_delete_seq` **and** no `stub_hit` at all - the shim's `bx` went somewhere that
 *       is neither this body nor a stand-in, which is what a wrong slot offset looks like;
 *   (c) a `stub_hit` on a *different* name after this record - a second slot on the teardown path, which
 *       the next step retires; the names to expect are the table's own, and the walk's rule is that the
 *       name this run reports is the next step's subject;
 *   (d) `xnu_live_pth_delete_seq` present and no `xnu_live_sigchld_*` with the boot otherwise continuing
 *       - a second stop between `:1105` and `:1443`, or the SIGCHLD path taking the arm that does not
 *       signal. `proc_exit`'s `notify parent` block's `psignal(pp, SIGCHLD)` is reached when the parent's
 *       `p_flag & P_NOCLDWAIT` is clear (nothing here sets it: `kern_sig.c:694` is the only writer and
 *       it is `sigaction`), and it is *not* reached on the `P_NOCLDWAIT` arm (`:1409-1411`, which marks
 *       the child `P_LIST_DEADPARENT` and reaps it without a signal) - so an absent record here would
 *       say something set that flag, which would be a finding of its own;
 *   (e) `xnu_entry_abort_entries != 0` with a `first_dfar` at a page-group or session offset - a fault
 *       inside `proc_exit`'s own teardown (`pg = proc_pgrp(p)` / `fixjobc(p, pg, 0)` at `:1196`/`:1197`,
 *       fifty lines after this slot), which would say the child was never put in a process group.
 *       `pinsertchild` (`kern_fork.c:556`/`:1000`) is what puts it in the parent's, and every line of
 *       that is real code in this image.
 */
static uint32_t stage90_pth_delete_calls;

static void
stage90_pthread_slot_pth_proc_hashdelete(proc_t p)
{
    stage90_pth_delete_calls++;
    entry_note_live("xnu_live_pth_delete_seq", stage90_pth_delete_calls);
    entry_note_live("xnu_live_pth_delete_p", (uint32_t)(uintptr_t)p);
    entry_note_live("xnu_live_pth_delete_tbl", (uint32_t)(uintptr_t)pthread_functions);
    entry_kv("xnu_entry_pth_delete_p", (uint32_t)(uintptr_t)p);
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
