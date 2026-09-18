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
 * Set `STAGE90_ENTRY_CHECKPOINT=klist_init` and the build adds `--wrap=klist_init` and defines the
 * symbol name here. Unset - which is the default, and every stage image - this translation unit is
 * not compiled at all.
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
 * The wrapper takes no arguments and ignores whatever the caller passed in r0-r3, which is
 * deliberate and safe on AAPCS: the callee reads only what it declares. It is `noreturn` in effect
 * because `entry_stub_hit` is terminal, but it is not declared so - the declaration has to match
 * `entry_stubs.c`'s, and a `noreturn` on one side and not the other is a warning under -Werror.
 */
void CP_CAT(__wrap_, STAGE90_ENTRY_CHECKPOINT_SYM)(void)
{
    entry_stub_hit(CP_STR(STAGE90_ENTRY_CHECKPOINT_SYM),
                   (uint32_t)(uintptr_t)__builtin_return_address(0));
}
