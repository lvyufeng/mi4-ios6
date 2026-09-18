# Experiment 296 — the first step inside thread creation, and a task flag that decides it

**Step:** link `bsd_kern_kern_fork.o` and `osfmk_arm_status.o` for `uthread_alloc` and
`machine_thread_state_initialize` — the stop 295 made, and the stop behind it.
**Prediction:** that the success path of `thread_create_internal` runs clean past `set_astledger`
(guarded by a task flag that is zero at boot) and stops at **`kpc_thread_create`**,
`thread_create_internal + 0x348`, caller `0x8000b034`.
**Result:** exactly that — and the build's `__bss_start` line uncovered a **latent defect in the
instrument** that every run this project has ever made has been carrying.

## The objects, measured

```
7924 bytes of .text (4968 + 2956), 127 of .rodata, 40 of .bss

resolved  11   cloneproc, machine_thread_set_state, machine_thread_state_initialize,
               proc_list_lock, proc_list_unlock, proc_lock, proc_unlock, uthread_alloc,
               uthread_cleanup, uthread_cred_free, uthread_zone_free
added     32   the BSD fork plumbing kern_fork.o drags in
```

The uthread four really are defined in `bsd/kern/kern_fork.c` in this tree, which is why
`bsd_kern_kern_fork.o` is the object for them and not an `osfmk/kern/uthread.o` that does not exist.
`machine_thread_state_initialize` is reached through `machine_thread_create` (real since 294), which
calls it unconditionally at the end of its body, at `machine_thread_create + 0x074`.

**Thirty-two new stubs for four retired names is the largest single-step increase in the stub count
this walk has made**, and it is the shape of what is ahead: the frontier is now in BSD process
creation, where an object is a subsystem rather than a file of accessors.

## The deciding instruction is a task flag read one byte wide

`thread_create_internal`'s success path, in address order:

| offset | call | status |
|---|---|---|
| +0x268 | `machine_thread_inherit_taskwide` | real |
| +0x280 | `task_reference_internal` | inlined (ldrex/strex) |
| +0x2ac | `set_astledger` | **stub**, and guarded |
| +0x2b4 | `ledger_instantiate` | real |
| +0x2c8 | `ledger_entry_setactive` | real |
| +0x2f0 | `ledger_reference` | real |
| +0x32c | `timer_call_setup` ×2 | real |
| +0x334 | `blx r1` → `sched_multiq_initial_thread_sched_mode` | real (the `SCHED` macro) |
| +0x344 | `kpc_thread_create` | **stub**, unguarded |
| +0x3a0 | `sched_set_thread_base_priority` | **stub**, unguarded |
| +0x3dc | `sched_thread_mode_demote` | **stub**, guarded |

`set_astledger` is guarded by

```
ldr r0, [r4, #688]     ; thread->task
ldrb r0, [r0, #642]    ; task->rusage_cpu_flags, one byte wide
tst r0, #2
beq  skip
```

which is `new_thread->task->rusage_cpu_flags & TASK_RUSECPU_FLAGS_PERTHR_LIMIT` (thread.c:1251, flag
0x02). `task_create_internal` writes a halfword zero to that field (`strh r5, [r4, r7]` with r7 = 642 at
0x800bfe18) and the only writers afterwards are the resource-accounting syscalls. `sched_thread_mode_demote`
is behind `cmp r1, #4 / bgt` on `parent_task->max_priority`, which for a task built with `TASK_NULL` as
parent is `MAXPRI_KERNEL` (95).

## The build, and the defect it found

```
                   predicted        measured
undefined          772              772
function           685              682
storage             87               90
.data              0x80118000       0x80118000
__bss_start        0x80130934       0x8012fc78
image              1247540          1247536
text               ~1139995         1139704
```

The function/storage split is off by three because `nprocs`, `pidhash` and `pidhashtbl` are `B 0x4`
storage, not functions — the trap 294 recorded.

The `__bss_start` line is off by 0xCB4, and chasing that difference found something that matters.

### `__bss_start` is not the start of `.bss`

The script symbol is placed between the `.data` output section and the `.bss` output section, but
`entry.ld` only captures `*(.data .data.*)` there. The objects' `__DATA, __const` and
`__DATA, __data` sections — note the **space** in the name, `"__DATA, __data"`, which is why
`*(.data .data.*)` does not match them — are **orphan** sections the linker places on its own: after
`.data`, and therefore **after `__bss_start` and before the real `.bss`**.

```
__bss_start          0x8012fc78   = the first byte of `__DATA, __const`
__DATA, __const      0x8012fc78   0x144
__DATA, __data       0x8012fdc0   0xb70
.bss (real)          0x80130940   0x036dd8
__bss_end            0x80167718
```

The payload does `memset(BSS_START, 0, BSS_END - BSS_START)` **after** copying the image in
(`stages/stage90/xnu_entry_jump.c`, step 2 of `stage90_xnu_entry_run`). So every run this project has
made has zeroed **3252 bytes of initialized data**: `const_boot_args`, `BootArgs`, and every
`vm_allocation_site` that `VM_ALLOC_SITE_STATIC` places there.

129 of those 3252 bytes are non-zero in the file, and they are exactly the `refcount = 2` the macro
sets:

```c
#define VM_ALLOC_SITE_STATIC(iflags, itag)                                 \
	static vm_allocation_site_t site                                      \
	__attribute__((section("__DATA, __data")))                             \
	 = { .refcount = 2, .tag = (itag), .flags = (iflags) };
```

`osfmk/mach/vm_types.h:176`; `struct vm_allocation_site` has `refcount` first, which is the
`02 00 00 00` the file carries, repeating every 24 bytes down `__DATA, __data`.

**So the walk has been running with every static allocation site's reference count zeroed.** It has not
bitten yet — `kalloc_canblock` reads `site->flags`, which is zero either way, and `vm_tag_alloc` only
needs `tag` — but `refcount` is what `vm_allocation_site` refcounting is *for*, and a boot that got as
far as releasing a site would find a count that says it is already free.

This is the same family as the pad defect 288 and 291 fixed: **a value with two definitions, where one
is the link's and the other is the instrument's assumption about the link.** Here the second definition
is "`__bss_start` is where the zeros begin". It is fixed in 297 as its own step, because it changes the
linker script and so deserves its own prediction.

## The run

```
stub_hit=kpc_thread_create        xnu_entry_stub_caller=0x8000b034
```

`0x8000b034` is `thread_create_internal + 0x348`, the return address of the `bl` at 0x8000b030 — and
`thread_create_internal` is at 0x8000acec, exactly as predicted before the build, because
`osfmk_kern_thread.o` links before both of this step's objects.

So one run measured that `uthread_alloc`, `machine_thread_state_initialize`, the whole ledger
instantiation, `SCHED(initial_thread_sched_mode)`, both `timer_call_setup`s and the
fail-thread-creation guard all completed — and that **`set_astledger` was skipped**, which was the
step's first falsifier and is now a measured fact about `rusage_cpu_flags` at boot rather than an
argument.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301118 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 297 — the `__bss_start` fix, on its own, because a change to `entry.ld` moves
addresses. Then 298 — `osfmk/kern/kpc_thread.c` for `kpc_thread_create`, and
`sched_set_thread_base_priority`/`sched_thread_mode_demote` (`osfmk/kern/priority.c`), which closes
`thread_create_internal`; after that `kernel_thread_create` returns a real thread to
`kernel_bootstrap`, which calls `thread_deallocate` and branches to `load_context` — the first time this
walk crosses into a context switch rather than a function call.
