# Experiment 293 — `osfmk_kern_bsd_kern.o`, a function that returns a constant, and the 16 KB boundary finally moving

**Step:** link the object that defines `get_task_uniqueid` — `osfmk/kern/bsd_kern.c`,
`osfmk_kern_bsd_kern.o` — the name the 292 run stopped at. It is also the object that defines
`get_task_crash_label`, one of the three names 292 obliged, so the step is again worth two names for
the price of one.
**Prediction:** that `get_task_uniqueid`'s new body returns `UINT64_MAX` without calling anything
(`task->bsd_info` is NULL), that the rest of the coalition adoption and the tails of
`task_create_internal` and `task_init` are clean, and the stop is `stack_init` at
`thread_init + 0xd8`, caller `0x800092ac` — **and** that this is the step where the 16 KB boundary that
has held `.data` at 0x8010c000 finally moves.
**Result:** both. **`stub_hit=stack_init` at `xnu_entry_stub_caller=0x800092ac`**, and `.data` went to
**0x80110000** with the image at **1214488**.

## The object, measured

```
4488 bytes of .text + 16 of .rodata.str1.1, 61 definitions, 34 references.

resolved  23   get_task_uniqueid, get_task_crash_label, get_bsdtask_info/set_bsdtask_info,
               get_bsdthread_info, get_threadtask, get_task_map, get_task_page_table,
               get_task_internal(+_compressed), get_task_phys_footprint(+_recent_max),
               get_task_purgeable_nonvolatile(+_compressed), get_task_purgeable_size,
               get_task_resident_max, get_task_iokit_mapped,
               get_task_alternate_accounting(+_compressed), get_task_cpu_time,
               get_task_dispatchqueue_serialno_offset, current_thread_aborted,
               task_act_iterate_wth_args
added      5   bank_billed_balance_safe, bank_serviced_balance_safe, bsd_threadcdir,
               get_dispatchqueue_serialno_offset_from_proc, proc_pidversion
under      7   act_set_astbsd, bsd_getthreadname, mt_core_supported, mt_fixed_task_counts,
               proc_uniqueid, psignal, thread_update_qos_cpu_time
real      29
```

Thirty-eight more definitions are referenced by nothing — `fill_task_rusage` and its six siblings,
`get_task_frozen`, `get_task_pmap`, `swap_task_map` and the rest of the BSD task accessor surface.
They arrive as dead code, as in 292, and none of the 38 collides with a symbol the image already has.

## The centre of the prediction is a function whose body is four instructions

```
    ldr  r0, [r0, #568]      ; task->bsd_info
    cmp  r0, #0
    beq  1f
    b    proc_uniqueid       ; tail call, only when bsd_info is set
1:  mvn  r0, #0
    mvn  r1, #0              ; UINT64_MAX
    bx   lr
```

`task_create_internal` sets `new_task->bsd_info = NULL` (task.c:1045; the image has
`str r5, [r4, #152]` among the block of zero stores at 0x800bfe6c) and no BSD code has run this early.
So **the new body takes the `beq` and returns the constant without calling anything** — it does not
reach `proc_uniqueid`, which is one of the seven `under` names and is still a stub. All three call
sites in `coalitions_adopt_task` (`+0x1b4`, `+0x220`, `+0x254`) take it, and all three are followed by
`and r0, r0, r1 / cmn r0, #1 / beq`, which the constant satisfies.

So this step **overshoots the symbol it links**, as 290 did. The second half of the prediction is what
makes it interesting: `get_task_uniqueid` was the *last* stub on the whole path back out through
`task_init`. Every remaining guard was read rather than assumed:

| where | the stub nearby | why it is not reached |
|---|---|---|
| rest of the adopt | — | `COALITION_NUM_TYPES` is 2, both `has_default = 1`; the second op is `i_coal_jetsam_adopt_task` (0x800bf060) and a depth-3 scan from it finds only hits behind `panic` |
| `coalitions_adopt_task` +0x244, +0x278 | `kernel_debug` → `kernel_debug_internal` → `current_proc` | each is behind `ldr r0, [0x80130f20] / mvn r1, #8 / tst r0, r1`, and 0x80130f20 is **`kdebug_enable`, a `.bss` symbol** this payload zeroes |
| +0x204 | `coalition_remove_task_internal` | the `kr != KERN_SUCCESS` cleanup arm only |
| +0x638 | `panic("created task is not a member of a resource coalition")` | the store `str r7, [r0, #936]` precedes `get_task_uniqueid` — which is exactly why the first stop was where it was |
| +0x724 | `place_task_hold` → `thread_hold`, `get_audit_token_pid` | guarded by `kernel_task != TASK_NULL`; `kernel_task` is written from the out-parameter at +0x73c, at the very end |
| `task_init` tail | — | `vm_map_deallocate` and a **tail call** to `lck_spin_init`, both real |
| `kernel_bootstrap` | — | `kernel_debug_string_early` (76 bytes) has no stub within three calls |
| `thread_init` +0x2c…+0xd0 | `zinit` → `btlog_create` at `zinit + 0x950` | behind two `.bss` guards; and `task_init` itself already called `zinit` at `+0x90` in every run since 289, so the path is settled empirically as well as by reading |

`stack_init`, `thread_policy_init` and `machine_thread_init` are `thread_init`'s tenth, eleventh and
twelfth calls, at +0xd4, +0xd8 and +0xdc — three stubs in a row with nothing between them.

## The 16 KB boundary, and this time the arithmetic worked

`.data` is 16 KB-aligned and was at 0x8010c000. The last thing before it, `__TEXT,initcode`, ended at
**0x8010babc**, leaving **1348 bytes**. The object brings 4504 bytes of `.text` and `.rodata`, retires
23 stub names and obliges 5.

```
                   predicted        measured
undefined          800              800
function           706              706
storage             94               94
.data              0x80110000       0x80110000
__bss_start        0x80127c08       0x80127c08
bss end            0x8015e458       0x8015e458
image              1214488          1214488
text               ~1099764         1099960     (band 1097100-1097400 at 52-65 bytes a name)
```

Every count exact, including the boundary move. The image gained precisely the 16384 bytes of `*fill*`
the map file has shown at that seam since the walk began. Headroom 1727400 → 1711016.

**The per-name cost, which is not a constant.** `(4504 - 3584) / 18` = **51.1 bytes**, where 3584 is the
measured text delta and 18 is 23 retired minus 5 obliged. The four measurements now on record:

```
290   65.3      291   51.8      292   61.8      293   51.1
```

so the ledger's "about 62 bytes" from 292 was a single sample and the real band is 51–65. The prediction
was made with 62 and still came out right, because the margin (2040 bytes) was larger than the spread.

**And the pin tracked the move unprompted** — the second measurement of the 291 fix. The writes are now
0x8015e448 and 0x8015e44c, 0x4000 further on, with
`ResetHandlerData - ExceptionLowVectorsBase = 0x15e444`, and the reserved slot moved with them
(`__bss_start` 0x80123c08 → 0x80127c08). Nothing had to be re-aimed; that is what a single definition of
a derived value buys.

```
entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
XNU writes 0x8015e448 and 0x8015e44c, both inside the reserved slot at 0x8015e448
```

## The run

```
stub_hit=stack_init        xnu_entry_stub_caller=0x800092ac
```

`0x800092ac` is `thread_init + 0xd8`, the return address of the `bl` at 0x800092a8, with `thread_init`
at 0x800091d4.

So one run measured that `get_task_uniqueid` returned its constant at all three call sites without
reaching `proc_uniqueid`, that the coalition loop ran for both types, that `task_create_internal` and
`task_init` both returned, and that `kernel_bootstrap` reached **`thread_init` — the first XNU function
in this walk that is about creating a thread rather than filling in a structure.** The remaining work
of this walk is now about threads, and that is the shape of the road to a running OS.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301111 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 294 — `osfmk/kern/stack.c`, `osfmk/kern/thread_policy.c` and
`osfmk/arm/machine_thread.c` for `stack_init`, `thread_policy_init` and `machine_thread_init`, three
consecutive unconditional calls with nothing between them, so a single step can take all three. After
that, `kernel_bootstrap`'s own tail: `atm_init`, `bank_init`, `ipc_pthread_priority_init` and
`corpses_init`, then `kernel_thread_create` and `load_context` — the point where XNU stops building
structures and starts scheduling.
