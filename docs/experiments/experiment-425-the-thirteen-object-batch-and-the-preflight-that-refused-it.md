# Experiment 425 — the thirteen-object batch, and the loader preflight that refused it: the image had outgrown the candidate table's alias window

**Step:** thirteen objects linked in one link — the first *batch* step in this run of steps, and the
first step in this project whose prediction is written **after** the build rather than before it.

    bsd_kern_proc_uuid_policy.o   retires proc_uuid_policy_init            1 resolved / 0 added
    bsd_kern_mcache.o             retires mcache_init (+ assfail)          2 resolved / 1 added
    bsd_kern_uipc_mbuf.o          retires mbinit and the mbuf core         8 resolved / 36 added
    bsd_kern_kpi_mbuf.o           retires mbuf_get_mlen and the KPI face   1 resolved / 51 added
    bsd_net_net_str_id.o          retires net_str_id_init                  3 resolved / 1 added
    bsd_kern_subr_eventhandler.o  retires eventhandler_init               1 resolved / 0 added
    bsd_kern_kern_aio.o           retires aio_init                        2 resolved / 2 added
    bsd_kern_sys_pipe.o           retires pipeinit                        5 resolved / 11 added
    bsd_kern_posix_shm.o          retires pshm_lock_init, pshm_cache_init 4 resolved / 10 added
    bsd_kern_posix_sem.o          retires psem_lock_init, psem_cache_init 3 resolved / 9 added
    bsd_kern_pthread_shims.o      retires pthread_init and nine more      11 resolved / 0 added
    bsd_kern_sys_generic.o        retires select_waitq_init               7 resolved / 9 added
    bsd_vfs_vfs_syscalls.o        retires nspace_handler_init             14 resolved / 85 added

(the comment block in `build_entry.sh` says "twelve objects" in its heading, from the first draft; the
batch is thirteen, and `bsd_vfs_vfs_syscalls.o` is the one added after that draft — see below.)

**Effect:** **817 -> 906** stub names, **687 -> 777** function, **130 -> 129** storage. The net is
+89 names: this batch resolves thirteen init bodies and creates the whole `mbuf`/`m_tag`/`mcl_` KPI
face and the VFS syscall surface behind them.

**Prediction, written after the link and before the device run:** `stub_hit=nwk_wq_init`, caller key
`0x8003B1FC` — the return address of the `bl <nwk_wq_init>` at `0x8003B1F8` (`bsd_init + 0x808`),
which `tools/xnu_entry_callwalk.py --root bsd_init` answered against the linked image.
**Measured: it did not get that far.** The run was refused *before* the jump.

    MI4IOS6_STAGE90_XNU loader_xnu_arm_vm_init_full_pmap_failure_mask=0x80000040
    MI4IOS6_STAGE90_XNU loader_xnu_entry_stub_failure_mask=0x00008910
    MI4IOS6_STAGE90_XNU kernel_entry bad: Mach-O/XNU loader preflight
    MI4IOS6_STAGE90_XNU kernel_entry returned failure
    MI4IOS6_STAGE90_XNU platform_reboot entered
    MI4IOS6_STAGE90_XNU stage90 hw_watchdog: forcing immediate bite (independent of PS_HOLD)

The device returned to Android on its own. (That run's capture has since been overwritten — the
next two runs wrote the same `LOGFILE` — so the lines above are quoted from reading it at the time
rather than from a file that still exists; every number in the diagnosis below is re-derived from
the image and the sources, which are both still here.)

**And the same step's fix was then measured, on the same link:**

    MI4IOS6_STAGE90_XNU kernel_entry ok
    MI4IOS6_STAGE90_XNU real XNU entry stub_hit=dqinit
     xnu_entry_stub_caller_v=0x801b3d20        # vfsinit is at 0x801B3880, so this is +0x4A0
     xnu_entry_abort_entries=0x00000000
     xnu_entry_checks=0x00000005   xnu_entry_failures=0x00000000

with `loader_xnu_arm_vm_init_full_pmap_failure_mask=0x00000000`,
`loader_xnu_arm_vm_init_full_pmap_high_alias_verified=0x00000001`, 301803 bytes / 3978 lines, ending
`No errors detected`. **The prediction was wrong, for a second and independent reason** — `dqinit`,
not `nwk_wq_init` — and that is the tool's defect rather than this step's, so it is written up at the
end of this log as rule 426.

## The diagnosis: the candidate table's image-alias window was 3 MB and the image had just passed it

The two masks decode to one failure each:

    0x80000040 = FAIL_HIGH_ALIAS (0x40) | FAIL_SAFETY_BOUNDARY (0x80000000)
    0x00008910 = FAIL_BAD_RETURN_STATUS (0x10) | FAIL_NO_OUTPUT (0x100)
               | FAIL_SAFETY_BOUNDARY (0x800) | FAIL_ARM_VM_INIT_FULL_PMAP (0x8000)

and `FAIL_SAFETY_BOUNDARY` is derived, not independent: the full-pmap check only sets it when
`failure_mask == 0` at the end, so the run's single real failure is `FAIL_HIGH_ALIAS` — the high-alias
probe. The result recorded `high_alias_verified=0x00000000` with everything around it satisfied
(`live_pmap_verified=1`, `ram_console_verified=1`, `gic_verified=1`, `high_va_data_verified=1`,
`satisfied_mask=0x0001ff7f` against `required_mask=0x0101ffff`, and `candidate_l2_pool_checksum=0`
is a normal value for this image — the 424 run reported the same — not a symptom.)

**The mechanism, and it is the third appearance of one defect class in this project's history.**
`xnu_arm_vm_init_full_pmap.c`'s Phase 5 builds the high alias of the payload's own image —
`STAGE90_HIGH_ALIAS_BASE + off -> PA off` — in a loop that stops early when the next alias would
collide with it. In the *candidate* table that limit was this file's own RAM-console alias at
`0xc0300000`, i.e. **3 MB**, while `mmu.c`'s identity table — the one the payload actually runs
under — stops at the GIC alias, `0xc0400000`, i.e. **4 MB**. One window, two limits, in two files.

The batch then moved the probe the check reads:

    stage90_full_pmap_probe_word   424 image  PA 0x002FC0B4   (limit 0x00300000: 0x3F4C to spare)
                                   425 image  PA 0x0032C0B4   (0x2C0B4 past it)

so Phase 5 stopped after three sections, `*alias_probe` became `0xc032c0b4` — which is not the
image alias's fourth section but the *RAM-console* alias's second kilobyte, resolving to PA
`0xde52c0b4` — and the read came back as ramoops content instead of `0x11223344`. Nothing faulted;
the mapped VA simply belonged to another device. The log's only line naming the real cause is inside
the branch that runs when it is already too late:

    MI4IOS6_STAGE90_XNU pmap_image_alias_image_end=0x00362000
    MI4IOS6_STAGE90_XNU pmap_image_alias_limit=0xc0300000
    MI4IOS6_STAGE90_XNU pmap image alias window is smaller than the image

**The identity table was never at risk**, which is why this is a *gate* failure and not a crash: its
window is 4 MB, `mmu_high_alias_sections=0x00000004` with no "smaller than the image" line, so the
payload's own map was complete and the boot would have run under it. What refused the entry was the
`xnu_arm_vm_init_full_pmap` dry-run — the model of the table a handed-off kernel would run under —
failing its own verification, which propagates into `loader_xnu_arm_vm_init_full_pmap_failure_mask`
and from there into the entry stub's, and `stage90_loader_preflight_run` then refuses the jump by
design.

**And 424 was one step from the same break.** Its probe word sat `0x3F4C` bytes below the 3 MB
limit; the batch spent those bytes. This is not a fact the 424 run could have shown — it passed — but
it is what makes the defect a boundary rather than a mistake: the window had been "big enough" for
eleven steps without anyone having to know how big it was, because its size was a side effect of
where an unrelated alias happened to sit.

## Why the batch, and why the prediction came second

The frontier at the time was `nspace_handler_init`, the fourth of the four stubs 422 created four
bytes apart in `vfsinit`. The remaining distance to `vfs_mountroot` is no longer inside `vfsinit`'s
body: intersecting the destinations of every `bl` in `bsd_init` with the stub set gives 45 names,
each defined by a *different* pool object — about thirty-five more one-object steps at the rate of
415-424. A batch is only sound if its membership is chosen by the call graph, and the first draft of
this one proves it: the twelve objects without `bsd_vfs_vfs_syscalls.o` left the frontier exactly
where it was, because `nspace_handler_init` is called from *inside* `vfsinit` and blocks every one of
the other twelve from being reached at all. **`bsd_init`'s line is a chain of nested calls, not a
flat list.**

`mbinit` is the batch's one substantial body (`bsd/kern/uipc_mbuf.c`, ~0x700 bytes): three `mbuf_*`
sizing calls, `read_random` ×3, seven `snprintf`s, four `mcache_*` calls and two
`kernel_thread_start`s — which is why `kpi_mbuf.o` and `mcache.o` are in this batch rather than a
later one. Two names on that path are worth recording as *not* stubs: `freelist_populate` is a
private/inlined helper of `uipc_mbuf.o` itself (in no stub set and in no object's definition list),
and `IOMapperIOVMAlloc` is defined by `iokit_Kernel_IOMapper.o`, which has been in `LINK_OBJS` since
the IOKit work.

## Layout

    entry text   2244032 (0x223D00)
    entry image  2358924 (0x0023FE8C)
    bss          0x8023FEC0 .. 0x802813D8 (267544 bytes, zeroed by the payload)
    layout       args 0x80283000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1567784 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, 5378048 bytes — one link, run twice: refused
                  first, then (after the window fix) entered and stopped at dqinit

## Where the frontier is now

**`dqinit`** (`bsd/vfs/vfs_quota.c`, defined by `bsd_vfs_vfs_quota.o`), reached from
`vfsinit + 0x4A0`, key `0x801b3d20`. It is a **one-object step — 1 resolved / 0 added** — and the
first step after the batch, so its prediction is written before its build again. The batch's thirteen
objects are linked and referenced and the corrected run walked through them: `nspace_handler_init`
and the other thirteen init bodies returned, and the stop is the next call on the same line.

## Rule 426 — the walk cannot see a call reached only by a loop exit, and the fix is reporting rather than classifying

The prediction above came from `tools/xnu_entry_callwalk.py --root bsd_init`, and it answered one
level up from where the device stopped. The reason is structural, not a bug in one table: the walk
follows the straight line plus calls reachable by *taking a branch*, and a loop's **exit target** is
the branch it does not model. `dqinit`'s call site is in `vfsinit`'s post-loop block, four bytes
after the exit tests of the `for (i = 0; i < maxvfsslots; i++, vfsp++)` loop that fills the vnode
table:

    0x801b3d18   bl <vnode_authorize_init>
    0x801b3d1c   bl <dqinit>                 # return address reported: 0x801b3d20 = vfsinit + 0x4A0

so the block was classified cold and the walk continued to `vfsinit`'s caller.

**The tempting fix does not work, and this is the part worth keeping.** Adding loop-exit targets to
the reachable set makes the walk answer
`zfree+0x5f8 → btlog_remove_entries_for_element` for `--root bsd_init` — a call that is
*structurally identical* to this one (a `bl` in a post-loop block) and that is provably never
executed, because `zfree`'s zone-check blocks make it unreachable; the device has disproved it
hundreds of times. Classifying both as cold misses `dqinit`. **The discriminating question — "is
this call site a loop exit?" — is not statically decidable**, so the tool cannot be fixed by
classifying better; what it needed was to stop being the only witness.

**The fix that was made.** `report_guarded_stub_sites()` prints, in execution order, the guarded
sites whose callee is *itself a stub*: all such sites for fully-traversed functions, and only those
before the descent call site for functions on the path to the answer. `--root bsd_init` prints
`vfsinit+0x49c -> dqinit   STUB` eleventh of fifteen. The device's own execution history then decides
which of them is the stop — the same division of labour 421 established for the guarded column, one
list over. The change is verifiably additive: with the new call in place, **all 9649 functions answer
exactly as they did before** (`/tmp/callwalk_old.py` diffed against the new tool, 0 differences).

**Still owed and unchanged: the timer.** Nothing in this step's path takes a deadline either — the
thirteen bodies are allocation, lock and queue initialisation — but they are the last of those: the
next layer is `vfs_mountroot`'s machinery, which is where `IOFindBSDRoot` and the mount loop's waits
live.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. **The refusal** is
the recovery design working: `stage90_loader_preflight_run` returned before the jump, `kernel_entry`
returned 0, `platform_reboot` wrote the restart reason and `PS_HOLD=0`, and the hardware watchdog
forced an immediate bite rather than waiting out its 25 s bark. No `exception:`, no `panic`, no
persistent write — the refusal path cannot write to storage — and the device returned to Android on
its own.

**The corrected run** went through the same two scripts and its stop is the normal kind: 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`,
`checks=5`/`failures=0`, no `exception:`, no `panic`, 301803 bytes / 3978 lines ending
`No errors detected`. The only net armed across the jump was the hardware watchdog
(`hw_watchdog_counter_running=0x00000001`, `hw_watchdog_bite_truncated=0x00000000`) and it is what
ended the run; the device returned to Android on its own and was confirmed there (`adb devices`
shows `4a2fe00b`).
