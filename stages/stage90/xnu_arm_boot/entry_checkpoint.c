/*
 * A one-variable terminal checkpoint, built only when STAGE90_ENTRY_CHECKPOINT names a symbol.
 *
 * Why this exists. Experiment 280 linked `bsd/kern/kern_event.o` - the step 279's run pointed at,
 * because `ipc_mqueue_init` tail-branches to `klist_init` and `klist_init` is one of that object's
 * three instructions - and the run went silent: the payload's whole ladder completes, the jump line
 * is written, and then no third line. No `stub_hit`, no `exception: <vector>`, no epilogue. The two
 * other ways a run can end are both visible in this log (a missing symbol reports
 * `stub_hit=<name>`; a fault reports its vector, its DFAR/DFSR and the instruction, as 236-242
 * established), so silence means the image's reporting path never produced a byte.
 *
 * What the host side had ruled out, before this file was written. The fifteen kilobytes of
 * `bsd_kern_kern_event.o` sit *after* `osfmk_ipc_ipc_mqueue.o` in the link, so `kern_event.o` and
 * what follows it moved and nothing before it did: the disassembly of
 * `[0x80000000, 0x800bbbc0)` - `_start` through the end of `ipc_mqueue.o` - is identical in the 279
 * and 280 images once `movw`/`movt` pairs are resolved (14927 differing halfwords, every one of
 * them an address that legitimately moved; no instruction differs). `ipc_mqueue_init+0x58` is
 * `b 800c0788 <klist_init>`, `klist_init` is `mov r1, #0` / `str r1, [r0]` / `bx lr`, and its `bx lr`
 * returns to `ipc_port_alloc_special+0x94`, which pops to `ipc_host_init+0x30`. From there
 * `ipc_host_init` runs `bl ipc_kobject_set` (+0x5c) and `bl ipc_port_make_send` (+0x64), both real
 * and neither ever executed, and then `bl kernel_set_special_port` (+0x74), which **is** a stub -
 * 24 bytes at 0x800dd29c, name string, `mov r1, lr`, `b entry_stub_hit` - so the run has to report
 * from there. `ipc_kobject_set` locks `port+8` and `ipc_port_alloc_special` initialises `port+8`.
 * Every callee was disassembled: `lck_spin_lock` is `b hw_lock_lock`, whose spin is the same
 * `ldrex`/`strex` shape as `lck_mtx_lock_spin_always` - which `zalloc` executed successfully in
 * 278, and `lck_mtx_lock_spin_always` proves the `mrc 15,0,rX,cr13,cr0,{4}` per-CPU read and the
 * `[TPIDRPRW+0x5c0]` preemption-counter increment both work - and `OSCompareAndSwap` is a bounded
 * `ldrex`/`strex` with no unbounded loop. All 94 storage stand-ins match a pool definition's size
 * exactly, `bpfread_filtops` is the only hand-sized one, and nothing on this path touches it.
 *
 * So the boot path is provably the 279 image's and the report path is provably reachable, and the
 * run is still silent. The remaining question is *which half* is at fault, and it is the question
 * this file answers: it turns the named symbol into a terminal stop through the same
 * `entry_stub_hit` every generated stub uses.
 *
 *   - If the checkpoint reports, the image's reporting path works in *this* build and the boot
 *     reached that point - so the silence of the plain run is about code *after* it.
 *   - If the checkpoint is also silent, the reporting path is what failed, and the frontier is not
 *     where the problem is at all. That would be a fifth instance of this project's oldest defect -
 *     a measurement that is the thing that is wrong - and the prior evidence for it is on the
 *     record: 271's `&&label` build "ran and then, three times in a row, produced no report at all",
 *     with the payload reaching `jumping to XNU's _start` and the entry image never writing another
 *     byte, and the 88-word dump probe reporting `g_kv_len` as 1 against a register that held 0x5e.
 *
 * Why `--wrap` rather than a stub in the generated-stub list: `--wrap` redirects *references* to the
 * symbol and leaves the definition alone, so the image still contains the object's real code - the
 * traced image runs the same code, with one call site redirected. Nothing about the step's own
 * result changes except which function reports it.
 *
 * Why a `b`-reached symbol reports its caller's caller. `ipc_mqueue_init`'s last instruction is a
 * tail branch, so the wrapper's `lr` is the one `ipc_mqueue_init`'s own `pop` restored: the report
 * from `klist_init` is expected to read `0x800ba300` = `ipc_port_alloc_special+0x94`, *the value
 * 279's run measured*. That is the consistency check this instrument carries with it - a checkpoint
 * at `klist_init` is the closest available reproduction of 279's run, one instruction earlier.
 *
 * What the instrument then measured, in three runs on the 280 configuration, is that the answer is
 * neither of those two: the two early checkpoints report and the late one is silent.
 *
 *   - `cpu_data_init` (step 1 of the ladder, `arm_init+0x40`): reports, `caller=0x80002fc8`.
 *   - `arm_vm_init` (step 18, `arm_init+0x1d4`): reports, `caller=0x80003160`.
 *   - `machine_startup` (step 19, `arm_init+0x340`): silent.
 *
 * The middle one is the informative one, and not for the reason it looks: `--wrap` replaces the
 * function, so a checkpoint at `arm_vm_init` proves the run reaches the call *site* and says nothing
 * about the 0x790 bytes of `arm_vm_init` itself. The interval the silence lives in is therefore
 * `[arm_init+0x1d8, arm_init+0x340)` - `arm_vm_init`'s whole body, plus the eighteen real calls
 * `arm_init` makes after it returns. In 279 both of those ran: 279's own run reported from
 * `klist_init`, which is reached through `machine_startup`, `kernel_bootstrap` and `ipc_init`. The
 * code is instruction-identical between the two images, so whatever kills the 280 run is a
 * *layout*-dependent difference in data, not in code - and the first thing to do is say which half
 * of that interval it is in.
 *
 * Set `STAGE90_ENTRY_CHECKPOINT=<symbol>` and the build adds `--wrap=<symbol>` and compiles this
 * file with the symbol's name. Unset - which is the default, and every stage image - this
 * translation unit is not compiled at all.
 */

#include <stdint.h>

/* entry_stubs.c. Records into `g_kv_buf` and never returns: it calls `entry_epilogue`, which tears
 * the caches and the mmu down and writes the report to the ram console. */
extern void entry_stub_hit(const char *name, uint32_t caller);

#define CP_CAT2(a, b) a##b
#define CP_CAT(a, b)  CP_CAT2(a, b)
#define CP_STR2(x)    #x
#define CP_STR(x)     CP_STR2(x)

/*
 * Four raw registers rather than `void`. A terminal wrapper has no reason to respect the callee's
 * prototype - it never calls the real function and never returns - but it has every reason to *read*
 * what the caller passed, and on AAPCS the first four arguments of any function are in r0-r3. So the
 * wrapper declares four `uint32_t`s whatever the symbol's real signature is, records them as raw
 * registers, and the checkpoint reports a call's arguments as well as its site. It is correct for
 * `klist_init(void)` - the registers hold whatever the caller happened to leave, which is recorded
 * and not interpreted - and it is correct for `patch_low_glo_static_region(uint32_t, uint32_t)`,
 * whose two arguments are the measurement. Arguments passed on the stack (>4) are not recorded; a
 * frame that needs them is a frame a terminal checkpoint cannot describe anyway.
 *
 * The wrapper ignores whatever the caller passed beyond reading it, which is deliberate and safe on
 * AAPCS: the callee reads only what it declares, and this callee is terminal. It is `noreturn` in
 * effect because `entry_stub_hit` is terminal, but it is not declared so - the declaration has to
 * match `entry_stubs.c`'s, and a `noreturn` on one side and not the other is a warning under -Werror.
 */
extern void entry_kv(const char *key, uint32_t value);

/*
 * `__real_<sym>` is `--wrap`'s own escape hatch: the linker rewrites references to `<sym>` into
 * references to `__wrap_<sym>` and defines `__real_<sym>` as the original definition. Declared out
 * here rather than inside the two variants that use it, because an unused `extern` declaration costs
 * nothing and the two variants should not each own a copy of it.
 *
 * The prototype is deliberately generic: four `uint32_t`s in and one out. On AAPCS that matches
 * every function this instrument has been pointed at - arguments beyond the fourth live on the
 * caller's stack and are not disturbed, registers beyond the callee's own declared ones are ignored
 * by it, and a `void` callee's caller does not read `r0`. A function whose real prototype needs
 * stack arguments is not a candidate for either variant.
 */
extern uint32_t CP_CAT(__real_, STAGE90_ENTRY_CHECKPOINT_SYM)(uint32_t, uint32_t, uint32_t, uint32_t);

#ifdef STAGE90_ENTRY_CHECKPOINT_AFTER
/*
 * The *value* variant, and the instrument this walk was missing. The plain checkpoint is terminal at
 * the call site, so it can say that a function was called and never what it did; the `SKIP` variant
 * runs the real function but can only report from a *later* call, which a function with one call
 * site does not have. This one calls the real function, records what it returned, and *then* reports
 * - so it is terminal *after* the call, and a single-site function becomes a value probe.
 *
 *   - A report with `cp_ret=<value>` means the callee returned, and the value is the answer to
 *     whatever the walk was inferring from call sites.
 *   - A silence means the callee did **not** return. That is a positive finding rather than an
 *     absence, because the plain checkpoint on the same symbol has already proved the call site is
 *     reached - the two runs together are what turn "silent" into "the function does not return".
 *
 * `lr` is read by inline assembly as the first statement rather than with
 * `__builtin_return_address(0)`, which the epilogue's own `entry_stub_hit` call would otherwise have
 * to survive: after a call, the value is no longer reliably in `lr` for a frame with no pointer, and
 * `-Werror` has opinions about asking for it anyway. The `asm` is volatile and cannot be moved below
 * the call.
 *
 * It is built only when asked for, and `STAGE90_ENTRY_CHECKPOINT_AFTER` is that ask. It *composes*
 * with `SKIP`, and the composition is the general instrument: the first `SKIP` calls go through
 * untouched and the `SKIP+1`th is the one that is run for real and reported. `AFTER` alone is the
 * `SKIP=0` case. They are not alternatives - they answer different halves of one question ("which
 * call site" and "what did it return") and restricting them to one at a time would have made the
 * probe that matters here, `bcopy` at call 3, unbuildable.
 *
 * ------------------------------------------------------------------ the one thing AFTER cannot do
 *
 * The real call is not a tail call - the wrapper has to get control back to record the return - so
 * the wrapper's own frame sits where the callee reads its *stack* arguments. Measured in the built
 * image rather than reasoned about: `__wrap_mmu_kvtop_wpreflight` opens with
 * `strd r4, [sp, #-32]!`, so at the `bl` the callee reads `[sp + 40]` - its fifth argument - from the
 * wrapper's saved `r4`. A callee whose arguments all fit in `r0-r3` is unaffected; **`bcopy_phys` is
 * not**: its `(addr64_t, addr64_t, vm_size_t)` signature puts `bytes` on the stack, and under
 * `AFTER` it would be read as the caller's garbage. `SKIP`'s pass-through does not have this
 * problem, because GCC compiles `return __real_<sym>(...)` as a sibling call (`add sp, sp, #24` /
 * `b bcopy_phys`) which restores the frame before the callee runs - verified in the built image.
 */
uint32_t CP_CAT(__wrap_, STAGE90_ENTRY_CHECKPOINT_SYM)(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    static volatile uint32_t calls;
    uint32_t lr;
    uint32_t ret;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

#ifdef STAGE90_ENTRY_CHECKPOINT_SKIP
    if (calls < (uint32_t)STAGE90_ENTRY_CHECKPOINT_SKIP) {
        calls = calls + 1u;
        return CP_CAT(__real_, STAGE90_ENTRY_CHECKPOINT_SYM)(a0, a1, a2, a3);
    }
#endif
    calls = calls + 1u;

    ret = CP_CAT(__real_, STAGE90_ENTRY_CHECKPOINT_SYM)(a0, a1, a2, a3);

    entry_kv("cp_calls", calls);
    entry_kv("cp_ret", ret);
    entry_kv("cp_arg0", a0);
    entry_kv("cp_arg1", a1);
    entry_kv("cp_arg2", a2);
    entry_kv("cp_arg3", a3);
    entry_stub_hit(CP_STR(STAGE90_ENTRY_CHECKPOINT_SYM), lr);
    return ret;
}
#elif defined(STAGE90_ENTRY_CHECKPOINT_SKIP)
/*
 * The skipped variant: the first `STAGE90_ENTRY_CHECKPOINT_SKIP` calls go through to the real
 * function and the next one reports. It exists because `--wrap` redirects *every* reference, so a
 * name whose first executed site is already behind the frontier cannot be probed at a later site at
 * all - and in `cpu_machine_idle_init` that is most of the body: `ml_static_vtop` is called eight
 * times and only the sixth, seventh and eighth are in the dark half, `bcopy_phys` three times with
 * the first already measured, `bcopy` three times with an earlier site elsewhere in the boot.
 *
 * Two things make the pass-through safe. The call is a real call to `__real_<sym>`, which `--wrap`
 * defines for exactly this purpose, so the callee's prologue, body and return are untouched; the
 * only cost is one extra frame on the first `n` calls. And the counter is a `static` in this
 * image's `.bss`, which the payload zeroes before `_start` - the same buffer, zeroed by the same
 * loop, that `g_kv_buf` lives in. `volatile`, because it is written in one call and read in the
 * next with no other synchronisation between them.
 *
 * The declaration of `__real_<sym>` - which the `AFTER`/`SKIP` guard above owns, so that the two
 * variants cannot drift apart - is deliberately generic: four `uint32_t`s in and one out. The report
 * says which call it fired on, so a miscount is visible rather than silent.
 */
uint32_t CP_CAT(__wrap_, STAGE90_ENTRY_CHECKPOINT_SYM)(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    static volatile uint32_t calls;

    if (calls < (uint32_t)STAGE90_ENTRY_CHECKPOINT_SKIP) {
        calls = calls + 1u;
        return CP_CAT(__real_, STAGE90_ENTRY_CHECKPOINT_SYM)(a0, a1, a2, a3);
    }
    calls = calls + 1u;

    entry_kv("cp_calls", calls);
    entry_kv("cp_arg0", a0);
    entry_kv("cp_arg1", a1);
    entry_kv("cp_arg2", a2);
    entry_kv("cp_arg3", a3);
    entry_stub_hit(CP_STR(STAGE90_ENTRY_CHECKPOINT_SYM),
                   (uint32_t)(uintptr_t)__builtin_return_address(0));
    return 0;
}
#else
void CP_CAT(__wrap_, STAGE90_ENTRY_CHECKPOINT_SYM)(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    entry_kv("cp_arg0", a0);
    entry_kv("cp_arg1", a1);
    entry_kv("cp_arg2", a2);
    entry_kv("cp_arg3", a3);
    entry_stub_hit(CP_STR(STAGE90_ENTRY_CHECKPOINT_SYM),
                   (uint32_t)(uintptr_t)__builtin_return_address(0));
}
#endif
