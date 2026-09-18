# Experiment 288 — `osfmk_kern_task.o`, and the pad's contract broken by a constant

**Step:** link the object that defines `init_task_ledgers` — `osfmk/kern/task.c`,
`osfmk_kern_task.o` — the name the 287 run stopped at. The largest step since 280: 23504 bytes of
text, 146 definitions, 224 references, 60 resolved and 59 added.
**Prediction:** that the frontier moves **two functions and eight calls** past the stop, to
`task_watch_init` — `init_task_ledgers` and `coalition_create_internal` both complete, then
`task_init` (retired by this very object) runs its five lock/zinit calls and stops on the sixth.
**Result:** `stub_hit=task_watch_init` at `task_init+0xb0`, exactly as predicted — **but only after
five silent runs, because this step broke the instrument.** The pad's contract had been checked
against two literals measured in 281 for six experiments; the value they stand for grows with the
walk, and by this step XNU's two writes were landing 0x58 bytes past the end of the pad, back inside
`entry_epilogue`. 282's defect, reached a different way, and the third time in this walk that a
silence was the instrument's.

## The step itself

`osfmk_kern_task.o`: 23504 bytes of text, 496 of bss, 104 of data, 1094 of `rodata.str1.1`, 144 of
`__DATA,__data`, 264 of `__TEXT,__os_log`, 146 definitions, 224 references.

```
resolved   60   (54 functions and 6 storage stand-ins - dead_task_statistics, kernel_task,
                 max_task_footprint_mb, task_ledgers, task_ledger_template, task_max - every one
                 of them a *generated* stand-in, checked against the stub list rather than the
                 image's symbol table)
added      59   (56 functions and 3 storage: default_task_effective_policy,
                 default_task_requested_policy, exc_via_corpse_forking)
```

That `resolved` set is the largest of the walk and it includes `task_init` and `task_max` — the two
names the pending list had carried for several steps — so this step retires a whole block of adjacent
stubs rather than one.

The prediction was read off the bodies, the way 285–287 were. `init_task_ledgers` calls 24
`ledger_entry_add`, 11 `ledger_track_credit_only`, 4 `ledger_set_callback`, `ledger_track_maximum`,
`ledger_template_create`, `ledger_template_complete` and `panic` — all real, and each of those was
scanned in turn (13, 2, 4, 2, 4, 1 calls respectively, no stub among them). `coalition_create_internal`
makes 9 calls, all real. So `coalitions_init` completes. Then `task_init`, now real because this
object defines it, calls in address order: `lck_grp_attr_setdefault`, `lck_grp_init`,
`lck_attr_setdefault`, `lck_mtx_init` ×2, `zinit`, `zone_change` — all real — and then
**`task_watch_init`**, which nothing in this image defines.

Measured build deltas, all three exact: 888 → **887** undefined, 791 → **793** function stubs, 97 →
**94** storage. Text 1042736 → 1067704, image 1148528 → **1181584**, `__bss_start` 0x80117b68 →
0x8011fbf8, headroom 1794040 → 1743992. This is the second step in a row to cross a 16 KB boundary;
`.data` moved `0x80100000 → 0x80108000`.

## The silence, and what it was

The run came back at **exactly 294042 bytes** — the silent-log size — with no `stub_hit` and no
`exception:` line, and the instrument's own tests followed: a checkpoint at `task_init` was silent,
and a checkpoint at `machine_init` — which 286 and 287 both reported from *past* — was silent too.
That last one is what identified the fault as the instrument's rather than the boot's.

**It is 282 again, reached a different way.** `cpu.c:570-580` writes

```c
bcopy_phys(vtop(&CpuDataEntries_paddr),
           gPhysBase + ((unsigned int)&(ResetHandlerData.cpu_data_entries)
                        - (unsigned int)&ExceptionLowVectorsBase), 4);
```

and the same with `boot_args`. 281 measured that difference as 0x2404 and `build_entry.sh`'s
`verify_pad` had compared against the literals 0x2404/0x2408 ever since. **The difference is not a
constant.** Read out of this image's own instruction stream at `cpu_machine_idle_init+0x1a8`:

```
80004294: movw r2, #0xb494 ; movt r2, #0x800e   ->  r2 = ResetHandlerData       = 0x800eb494
800042a4: sub  r5, r2, r5                        r5 = ExceptionLowVectorsBase  = 0x800e8fec
                                                 -> r5 = the difference        = 0x24a8
800042b4: add  r2, r1, #8    (r1 = gPhysBase)   -> cpu.c's boot_args       target = 0x800024b0
800042f0: add  r2, r1, #4                       -> cpu.c's cpu_data_entries target = 0x800024ac
```

The gap is 152 generated stub bodies at 0x18 bytes apart, sitting between `ExceptionLowVectorsBase`
(0x800e8fec) and `ResetHandlerData` (0x800eb494) in the linked `.text`. **Every object this walk adds
grows it.** At 281 it was small enough that the writes fell inside a 128-byte pad; by 288 they were at
0x800024AC/0x800024B0 — 0x58 bytes past the pad's end, inside `entry_epilogue`'s own code — and
because `verify_pad` was checking the wrong number, **every build said the layout was fine.**

So the silence was the report path again, and the boot may have run any distance before it.

## The fix: one value, one definition

Both halves are about the value having a single definition, which is a defect class this project
already has a memory about:

- **The pad is 512 bytes** — `b 1f` over 127 NOPs rather than over 31. Its cost is image bytes and
  nothing else, because it is skipped.
- **`verify_pad` now derives the two addresses from the linked image** — `ResetHandlerData` and
  `ExceptionLowVectorsBase` at the struct's own field offsets, 4 and 8 — and compares the pad's
  skipped range against those, never against a literal. It also now fails the build if
  `ResetHandlerData` is not above `ExceptionLowVectorsBase`, and the minimum pad size check went from
  128 to 256 bytes.

Every build now prints the derivation:

```
entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
XNU writes 0x800024ac and 0x800024b0 (ResetHandlerData - ExceptionLowVectorsBase = 0x24a8),
and both land inside what it skips
```

Worth stating plainly, because it is the lesson: **the check that exists to catch exactly this
failure was itself the failure.** 282 built `verify_pad` to guarantee the report path could not be
corrupted, and then the guarantee was made against a number that drifts.

## The run, with the instrument working

```
stub_hit=task_watch_init        xnu_entry_stub_caller=0x800bf900
```

`0x800bf900` is `task_init + 0xb0`; `task_init` links at 0x800bf850 in the rebuilt image, and the
0x180 that 0x800bf900 sits past the pre-fix 0x800bf780 is exactly the 384 bytes the wider pad added
above it. So the step's real frontier is `task_watch_init`, and one run measured that
`init_task_ledgers` completes with all forty-odd of its ledger calls, that `coalition_create_internal`
completes, and that `task_init`'s first five calls — including `zinit` again, with 287's measurement
covering its one stub — all return.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301116 bytes, no `exception:` line.

**Safety, on every run including the five silent ones:** non-persistent `fastboot boot` only, nothing
flashed, `persistent_write_attempted=0x00000000` x25 and `failure_mask=0x00000000` x87 on each,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own every time — the
silent runs through the payload's deadman, exactly as designed.

**Next:** experiment 289 — `osfmk/kern/task_watch.c` (`osfmk_kern_task_watch.o`) for
`task_watch_init`. Smaller than this step by an order of magnitude, and the prediction will again
come from the body: `task_watch_init` may complete, in which case the frontier moves to `proc_init_cpumon_params` — the other new name in `task_init`'s body — or past `task_init` entirely, to
`thread_init` and then `atm_init`.
