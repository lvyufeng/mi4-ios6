# Experiment 292 — `security_mac_mach.o`, and a prediction that leaves two functions entirely

**Step:** link the object that defines `mac_exc_create_label` — `security/mac_mach.c`,
`security_mac_mach.o` — the name the 291 run stopped at. **Not** `security/mac_exc.c`, which is what
291's own next-step note named and which is not a file in this tree. Nothing is lost by the
correction: the two names 291 obliged, `mac_exc_free_action_label` and `mac_exc_inherit_action_label`,
are defined in `mac_mach.c` too, so one object settles all three.
**Prediction:** that the frontier leaves both functions the walk has been inside since 288 — that the
thirteen MACF iterations, `ipc_task_init`'s `TASK_NULL` arm, `task_create_internal`'s else path and
`task_init`'s tail all complete, and the stop is inside the coalition adoption at
`get_task_uniqueid`, caller `0x800bd900`.
**Result:** exactly that. **`stub_hit=get_task_uniqueid` at `xnu_entry_stub_caller=0x800bd900`** — a
stop in `osfmk/kern/bsd_kern.c`, the first frontier this walk has had that is not in osfmk/ipc,
osfmk/kern/generic or security.

## The object, measured

```
3532 bytes of .text, and nothing else - no .data, no .rodata, no .bss, 15 definitions, 19 references.
The one string literal, in mac_exc_action_check_exception_send's printf, is inside the .text input
section, so this is the first object of the walk whose entire contribution is code.

resolved  12   mac_exc_create_label, mac_exc_associate_action_label, mac_exc_free_label,
               mac_exc_free_action_label, mac_exc_inherit_action_label, mac_exc_update_action_label,
               mac_exc_update_task_crash_label, mac_exc_create_label_for_proc,
               mac_exc_create_label_for_current_proc, mac_task_check_expose_task,
               mac_task_check_set_host_special_port, mac_task_check_set_host_exception_ports
               - every one of them already a stub body in this image
added      3   get_task_crash_label (osfmk/kern/bsd_kern.c), proc_self and proc_task
               (bsd/kern/kern_proc.c)
under      7   current_proc, get_bsdtask_info, kauth_cred_get, kauth_cred_proc_ref, kauth_cred_unref,
               proc_find, proc_rele - already stubs, so no new demand
real       9   current_task, mac_error_select, mac_labelzone_alloc, mac_labelzone_free,
               mac_policy_list, mac_policy_list_conditional_busy, mac_policy_list_unbusy,
               task_pid, _consume_printf_args
```

Three of the nineteen references are new demands and not seven, because the seven `under` names are
already stub bodies — the counting rule 288 had to correct, and this is the fourth object it has held
for. The other three definitions — `mac_exc_action_check_exception_send`,
`mac_task_check_set_host_exception_port`, `mac_thread_userret` — are referenced by nothing at all, not
by the image and not by the object's own `.text`; each is confirmed absent from the image's symbol
table, so they cannot collide at link time. They arrive as dead code.

The three `added` names are each one call away from being reachable and each behind a guard the
thirteen iterations do not open: `proc_self`/`proc_task` are in `mac_task_get_proc`, reached only by
the three `mac_task_check_*` functions, and `get_task_crash_label` by
`mac_exc_update_task_crash_label` and `mac_exc_action_check_exception_send`.

## The MACF loops are no-ops, measured in the object's own code

The thirteen unrolled iterations of `for (i = FIRST_EXCEPTION; i < EXC_TYPES_COUNT; i++)` call the two
names this object defines and nothing else — but "nothing else" is a claim about `MAC_PERFORM`, so it
was measured rather than assumed, twice:

* the first loop's bound is loaded **from the list itself**: `ldr r0, [r6, #12]` is
  `mac_policy_list.staticmax`, and the object's offsets match `struct mac_policy_list` exactly
  (numloaded 0, max 4, maxindex 8, **staticmax 12**, chunks 16, freehint 20, **entries 24** — the
  loop's `ldr r1, [r6, #24]` and its `ldr r2, [r1, r5, lsl #2]` confirm 4-byte elements).
  `mac_policy_init` set it to 0 and nothing at boot calls `mac_policy_register`, so
  `cmp r0, #0 / beq` at `mac_exc_create_label + 0x2c` skips the whole loop.
* the second is behind `mac_policy_list_conditional_busy()`, whose first statement is
  `if (mac_policy_list.numloaded <= mac_policy_list.staticmax) return (0);` (mac_base.c:318) —
  `0 <= 0` — so the indirect `blx r2` that would reach `entries[i].mpc` and the
  `mac_policy_list_unbusy()` after it are both unreachable, and the only context-dependent read of
  the whole step is never made.

So the iterations reduce to thirteen `mac_labelzone_alloc(MAC_WAITOK)` → `zalloc(zone_label)` (real,
and the zone exists because `mac_policy_init` called `mac_labelzone_init` at 275) and thirteen stores
through `mac_exc_associate_action_label`. Then the `TASK_NULL` arm: `host_priv_self` (12 bytes, no
call) and `host_get_host_port` → `host_get_special_port` (three real calls), whose
`assert(kr == KERN_SUCCESS)` is compiled out — the image shows no comparison after the call.

## 291's next-step note was wrong, and the image says so twice

291's note predicted "the nine further stub calls `task_create_internal` makes at +0x300 and up".
`task_create_internal` makes **exactly three** stub calls, and all three are inside
`if (parent_task != TASK_NULL)`, the arm that `task_init`'s `TASK_NULL` argument does not take:

| | |
|---|---|
| `vm_shared_region_get` | +0x300 |
| `vm_shared_region_set` | +0x30c |
| `task_affinity_create` | +0x348 |
| everything else | real — `__bzero` ×4, `kalloc_canblock`, `memset`, `coalitions_adopt_init_task`, `ipc_task_enable`, `lck_mtx_lock`, `lck_spin_init`, `place_task_hold` (not reached), the six coalition calls, `vm_map_deallocate` |

Two independent readings, both from the image:

* the branch at +0x2cc (0x800bfe90) tests a word that is set to 1 **only** on the parent path —
  `str r7, [sp, #12]` sits inside `if (parent_task != TASK_NULL)` and the `beq` that skips it is taken
  for a null parent;
* the block it jumps to is unmistakably the else arm of the source: it stores
  `KERNEL_SECURITY_TOKEN`/`KERNEL_AUDIT_TOKEN` out of `.data` and picks `BASEPRI_KERNEL` when
  `kernel_task == TASK_NULL` — the opposite of the parent arm's "inherit the parent's shared region",
  which is where `vm_shared_region_get` lives.

`task_init` then returns through `vm_map_deallocate` and a tail call to `lck_spin_init`, both real.
`kernel_bootstrap` hands over to `thread_init`, whose fourth call is `stack_init` — a stub, at
`thread_init + 0xd4`, followed by two more stubs with nothing between them. **That is where the
frontier would have landed if the coalition block were clean.** It is not:

```
coalitions_adopt_init_task       coalitions_adopt_task(init_coalition, task)
  -> coalitions_adopt_task       the inlined coalition_adopt_task_internal:
       lck_mtx_lock              real
       reaped/terminated check   ldrb r0,[r7,#32] / tst r0,#12 - both clear for the init coalition
       coal_call(adopt_task)     blx through the ops table at 0x80103bfc: type 0's entry is
                                 i_coal_resource_adopt_task (0x800bebd8), real
       counters, task->coalition[type] = coal
  -> get_task_uniqueid           ***STUB***   if (get_task_uniqueid(task) != UINT64_MAX)
```

`get_task_uniqueid` is called on the way *out* of the inlined internal function, after `out_unlock`,
so it is reached whether or not `coal_call` succeeds.

## The falsifiers, named in advance

| call | the stub inside | why it is not reached |
|---|---|---|
| `kalloc_canblock` | `ledger_debit` → `ledger_entry_check_new_balance` → `set_astledger` | `KALLOC_ZINFO_SALLOC` exists only on the large-allocation path (kalloc.c:679, inside the `size >= kalloc_max_prerounded` else) and `sizeof(struct io_stat_info)` takes `get_zone_dlut` |
| `kalloc_canblock` | `vm_tag_alloc` → `vm_tag_bt` → `OSKextGetAllocationSiteForCaller` | `if (site) tag = vm_tag_alloc(site)` is inside the same large-path else (kalloc.c:645) |
| `kmem_alloc_flags` | `trace_backtrace` | `log_leaks` in `.bss` is zero — 291's reading, unchanged |
| `coalitions_adopt_init_task` | `panic` → `panic_trap_to_debugger` → `PEHaltRestart` | only if `coalitions_adopt_task` returns non-zero |
| `i_coal_resource_adopt_task` | `panic` → … | a depth-4 scan from it finds only hits behind `panic`; nothing on its success path |
| `place_task_hold` | `thread_hold`, `get_audit_token_pid` | **not reached** — guarded by `kernel_task != TASK_NULL`, and `kernel_task` is still NULL: it is written from the out-parameter at the very end of the function |
| `thread_init` | `stack_init`, `thread_policy_init`, `machine_thread_init` | **not reached** — after the coalition block |
| `lck_mtx_lock` | … → `ast_taken_kernel` | nothing has posted an AST this early (291's reading); a depth-2 scan finds no stub |

One more, which is not a stub but would end the run just as surely: `task_create_internal`'s
`panic("created task is not a member of a resource coalition")`, one instruction after the coalition
block. It fires if the adopt did not store into `task->coalition[RESOURCE]` — which is why the store
happens *before* `get_task_uniqueid` and not after.

## The build

```
                  predicted        measured
undefined         818              818
function          724              724
storage            94               94
image             1198104          1198104
.data             0x8010c000       0x8010c000
__bss_start       0x80123c08       0x80123c08
text              ~1096500         1096376      (+2976; the soft number, off by 130)
```

Two corrections, both worth keeping:

* the object's 3532 bytes of `.text` net **+2976**, so a retired stub name is worth about **62 bytes**
  — the same figure 290's three retirements implied (65), and not the 46 the 291 arithmetic suggested.
  The 2976 is `realstubs.o`'s `.text` (17592 → 17376) and `.rodata.str1.4` (15388 → 15060) shrinking
  against the new object exactly.
* **`__bss_end` did not move, and the prediction that it would was wrong.** The writes are still
  0x8015a448 and 0x8015a44c, and `.bss` is 0x35c18 both before and after. The modelling error is
  itself a fact about the generator: the function stubs have **no `.bss` slot at all** — their bodies
  and their name strings are the whole of their cost, both in `realstubs.o` — so `.bss` moves only
  when the *storage* set moves, and the storage set is unchanged at 94. The 291 note that attributed
  +64 to "alignment inside the stub object's storage" was right about the mechanism and wrong about
  the trigger. The pin is therefore *more* stable than this step assumed: retiring function names
  cannot move the reserved slot, because it cannot move `.bss`.

The pad did not move either — 0x800023d4, branching over 512 bytes to 0x800025d4, exactly as in 291's
post-fix build. It lives in `xnu_arm_entry_stubs.o`, the fixed entry-side object (`.text` 4616 bytes),
at offset 0x454 in it; the XNU objects proper start at 0x80003188 with `arm_init.o`.

```
entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
XNU writes 0x8015a448 and 0x8015a44c (ResetHandlerData - ExceptionLowVectorsBase = 0x15a444),
both inside the reserved slot at 0x8015a448
```

For the first time the walk is within a kilobyte of the 16 KB boundary that would move `.data`:
`__TEXT,initcode` ends at 0x8010af1c and `.data` is at 0x8010c000 — 4324 bytes of room, of which this
step used 2976. The next few steps fit; somewhere in the next two or three, `.data` moves to
0x80110000 and the image gains 16384 bytes. That is not a failure — it is what the `*fill*` in the map
file means — but it is the next number that will surprise a prediction.

## The run

```
stub_hit=get_task_uniqueid        xnu_entry_stub_caller=0x800bd900
```

`0x800bd900` is `coalitions_adopt_task + 0x0dc`, the return address of the `bl` at 0x800bd8fc, and the
`_a` and `_e` copies of the caller address agree with `_v`.

So one run measured the longest chain this walk has cleared **and the first one that leaves a
function**: thirteen real label allocations and thirteen real associations, `ipc_task_init`'s whole
`TASK_NULL` arm, `task_create_internal`'s else path through `KERNEL_SECURITY_TOKEN`,
`kalloc_canblock`, the rollup else, `task_init`'s own tail, and eight real levels into
`coalitions_adopt_init_task` → `coalitions_adopt_task` → `i_coal_resource_adopt_task`. Both functions
291 predicted the frontier would spend several more steps inside are behind it.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`,
`high_va_data_verified=0x00000001`), log 301118 bytes, no `exception:` line,
`xnu_entry_kv_dropped=0x00000000`, and every abort-record field zero.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 293 — `osfmk/kern/bsd_kern.c` (`osfmk_kern_bsd_kern.o`) for
`get_task_uniqueid`. It is also the object that defines `get_task_crash_label`, one of the two names
this step obliges, and it is 4488 bytes of `.text`, 60 definitions and 34 references. After it, the
coalition adoption completes, `task_create_internal` returns, `task_init` returns, and
`kernel_bootstrap` reaches `thread_init` — where three stubs sit in a row
(`stack_init`, `thread_policy_init`, `machine_thread_init`), followed by `atm_init`, `bank_init`,
`ipc_pthread_priority_init` and `corpses_init` in `kernel_bootstrap`'s own tail.
