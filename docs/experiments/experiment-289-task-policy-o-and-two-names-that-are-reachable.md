# Experiment 289 — `osfmk_kern_task_policy.o`, and two new names that both turn out to be reachable

**Step:** link the object that defines `task_watch_init` — `osfmk/kern/task_policy.c`,
`osfmk_kern_task_policy.o` — the name the 288 run stopped at, once the pad's contract was fixed.
**Prediction:** that the frontier moves **fourteen calls** past the stop, to `machine_task_init` —
which required clearing the two names this step itself brings in, a `PE_get_default` whose branch is
decided by the device tree, and `task_create_internal`'s own prelude.
**Result:** exactly that. **`stub_hit=machine_task_init` at `xnu_entry_stub_caller=0x800bfdc4`** =
`task_create_internal + 0x200`, all three build counts exact, `.data` crossing the block it was
predicted to cross.

## The step is worth two calls, not one

288 retired `task_init`'s first seven stubs and stopped on the eighth, `task_watch_init`. The object
that defines it is `osfmk/kern/task_policy.c` — and reading it before the build gave the pleasant
surprise that it defines **both** of the two new names in `task_init`'s body that 288 introduced:
`task_watch_init` *and* `proc_init_cpumon_params`. So this step is worth two calls rather than one,
and the prediction is the longest chain of the walk.

```
14636 bytes of text, 16 of data, 240 of rodata, 479 of rodata.str1.1, 28 of bss, 72 of
__DATA,__data, 16 of rodata.cst16, 71 definitions, 82 references.

resolved   28   (26 functions, plus the two storage stand-ins 288 had just added -
                 default_task_effective_policy and default_task_requested_policy, both R)
added       8   (seven functions and one storage, thread_qos_policy_params R)
                mig_strncpy            proc_apply_resource_actions
                proc_apply_task_networkbg   proc_pidpathinfo_internal
                proc_restore_resource_actions   thread_policy_update_complete_unlocked
                thread_policy_update_tasklocked   thread_qos_policy_params
```

## The chain, read off the bodies

`task_init`'s calls in address order after the 288 stop:

```
 8  task_watch_init          ***this object*** - its whole body is one `lck_mtx_init`, real
 9  PE_parse_boot_argn       real, and its own extent has no call at all
10  PE_get_default           real - and the one branch in the chain a *string* decides
11-13 _consume_printf_args x3   real
14  proc_init_cpumon_params  ***this object*** - five calls, PE_parse_boot_argn and
                              PE_get_default only, both real
15-21 PE_parse_boot_argn x7  real
22  task_create_internal     real (task.o, linked in 288) - and its first stub call is next
```

`PE_get_default` is the third kind of stop-decider this walk has met — a string this build supplies
or omits — and it is the one link in the chain worth stating in full, because it is the one that
could have gone somewhere else. Its body looks `/defaults` up first (`DTLookupEntry(NULL, ...)`) and
reaches `IODTGetDefault` — **a stub** — only when that lookup *fails*. The payload's synthetic tree
has a `defaults` child (`stage90_main.c:735`, `apple_dt.c:230`) and `kSuccess` is 1
(`device_tree.h:125`), so the lookup succeeds, the code takes the `DTGetProperty` path — real — and
`IODTGetDefault` is never called.

The falsifier was named before the build and is cheap: a report naming `IODTGetDefault` would mean
the lookup failed and the tree is not what `apple_dt.c` promises — a measurement about the device
tree rather than about this step. It did not happen; the report names `machine_task_init`.

**And `task_create_internal` is where the frontier actually is**, so its stub calls were listed in
address order before the build, so that whichever one the run named could be checked against the
list rather than merely observed:

```
+0x1fc  machine_task_init                     <- the first, return address +0x200
+0x220  ipc_task_init
+0x300  vm_shared_region_get     +0x30c  vm_shared_region_set
+0x348  task_affinity_create     +0x360  task_is_marked_importance_donor
+0x3f4  task_is_marked_importance_receiver
+0x428  task_is_marked_importance_denap_receiver
+0x480  task_policy_create       +0x6b0  ipc_task_enable
```

## The build

```
                      predicted        measured
undefined             867              867
function stubs        774              774
storage               93               93
text                  1082340          1082328
image                 1198056          1198056
__bss_start           0x80123c08       0x80123c08
```

All three counts and `__bss_start` exact — the third step in a row where the 16 KB arithmetic landed
on the byte. Text came out 12 bytes under the arithmetic (286's equivalent error was 36), and the
image is exact because the read-only region's slack absorbed the difference.

`.data` moved `0x80108000 → 0x8010c000`, the second step in a row to cross a block, and the image
grew by 0x4058 — the 0x4000 the base moved, plus the 0x58 that `__DATA, __const` (0x144) and
`__DATA, __data` (0xa98) grew by. Those two sections sit *above* `.data` and are what the `.bin`
actually ends at: the image is `__bss_start` (the end of `.data`) plus 0x144 + alignment + 0xa98,
which is a second reason the image size is not the thing to predict text growth with.

bss end 0x80156388 → 0x8015a3c8, headroom 1743992 → **1727544**.

**The pad's derivation, now tracking a moving value.** This is the first step that shows why
`verify_pad` had to stop comparing against 281's literals:

```
entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
XNU writes 0x800024c4 and 0x800024c8 (ResetHandlerData - ExceptionLowVectorsBase = 0x24c0),
and both land inside what it skips
```

0x24c0 is 0x18 above 288's 0x24a8 and 0xbc above 281's 0x2404. Both writes are still inside the pad;
under the old 128-byte pad and the old literal check, this step would have been silent too.

## The run

```
stub_hit=machine_task_init        xnu_entry_stub_caller=0x800bfdc4
```

`0x800bfdc4` is `task_create_internal + 0x200`. `task_create_internal` links at 0x800bfbc4, and the
image's own instruction stream puts `bl machine_task_init` at 0x800bfdc0 — so the reported caller is
the return address of that call, to the byte.

So one run measured the longest chain of the walk — fourteen calls that had to return before the
stop:

- the two names **this object** brought in: `task_watch_init` (one `lck_mtx_init`) and
  `proc_init_cpumon_params` (five `PE_parse_boot_argn` / `PE_get_default` calls);
- `PE_get_default`, taking its `DTGetProperty` path — the DT branch taken as predicted, and
  `IODTGetDefault` never reached;
- the three `_consume_printf_args` and seven further `PE_parse_boot_argn`;
- `task_create_internal`'s own prelude — `zalloc`, `ledger_instantiate`, `sched_group_create`,
  `task_init` and the rest — none of which contains a stub.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301118 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 290 — `osfmk/arm/machine_task.c` (`osfmk_arm_machine_task.o`) for
`machine_task_init`. A first reading, before the build: **324 bytes of `.text` for five functions**,
five definitions, six references, and `machine_task_init` at object offset 0x140 — the last four
bytes of that text, because **its body is empty** (`machine_task.c:171-175`: three `__unused`
parameters and no statement). So the prediction is that the step overshoots again: the frontier moves
to the next stub call in `task_create_internal`, which this image shows is `bl ipc_task_init` at
0x800bfde4, return address **0x800bfde8 = task_create_internal + 0x224**. The seven instructions
between the two calls are `mov`/`str`/`add` and one `vmov.i32`, with no `bl` among them.
