# Experiment 300 — `priority.o`, and the stop leaves `thread_create_internal` entirely

**Step:** link `osfmk/kern/priority.c` (`osfmk_kern_priority.o`) — the object that defines
`sched_set_thread_base_priority`, where the 299 run stopped, and twelve more names this image was
still stubbing.
**Prediction:** that the stop is **not** the next stub in the caller but `processor_up`, the second
call inside `load_context` — i.e. that `thread_create_internal` completes, returns a real `thread_t`
to `kernel_bootstrap`, and the walk reaches the first context switch.
**Result:** `stub_hit=processor_up` at `xnu_entry_stub_caller=0x8000e73c` (`load_context + 0x20`),
exactly as read off the image before the device was touched.

## Thirteen resolved, and nothing asked for

`osfmk/kern/priority.c` (manifest:575) was already built as `osfmk_kern_priority.o`: 0xB30 of
`.text`, 0x100 of `.rodata` (`sched_decay_shifts`), 0x16 of `.rodata.str1.1`, 0x14 of `.bss`.

**13 resolved** — twelve functions (`sched_set_thread_base_priority`, `sched_thread_mode_demote`,
`sched_compute_timeshare_priority`, `thread_recompute_sched_pri`, `thread_quantum_expire`,
`lightweight_update_priority`, `update_priority`, `can_update_priority`,
`sched_thread_mode_undemote`, `sched_set_thread_mode`, `sched_run_incr`, `sched_run_decr`) and one
**storage** name (`sched_run_buckets`, `B 0x14`). **0 added**: all 40 of the object's references are
symbols this image already carries. That is the 250/270 shape inverted — an object that defines
thirteen names the link wants and obliges it to nothing new.

## The prediction is a walk through five functions, not a lookup

299's stop is at `thread_create_internal + 0x3a4` and every stub near it belongs to
`thread_create_internal` itself — but this object answers the last of them, so the step's prediction
had to be where the walk goes *after* its own object. That was read off the image call by call
before the run:

| what runs | why it does not stop |
|---|---|
| `sched_set_thread_base_priority` (this object) | body: `tst state,#TH_RUN` takes the else arm; `mrc` current-thread compare fails, so neither `mach_approximate_time` nor `machine_switch_perfcontrol_state_update` is called; `sched_mode` is **TH_MODE_FIXED** (2, parent is `kernel_task`), so the `blx r1` on `sched_multiq_dispatch + 0x24` is **not taken** — `sched_compute_timeshare_priority`, this object's own and a stub until now, is resolved without being called |
| `set_sched_pri` | real (sched_prim.o, 0x800a24c8). `sched_pri` is 0 in the fresh zone and the new priority is 95, so no early return; `thread_run_queue_remove` is real and returns FALSE with **no calls** inside it (`runq` is `PROCESSOR_NULL`); then `tst state,#TH_RUN` is clear and it returns |
| `thread_create_internal`'s remaining tail | `thread_policy_create`, `queue_enter`, `hw_atomic_add`, the thread id, `kdbg_trace_data`/`kdbg_trace_string`, `task_is_exec_copy`, `kernel_debug` — all real; the epilogue returns `KERN_SUCCESS` with the thread written through the caller's out-pointer |
| `kernel_thread_create` | returns to `kernel_bootstrap + 0x32c` |
| `kernel_bootstrap`'s five statements | `thread->state = TH_RUN`, `mach_absolute_time`, `thread_deallocate`, one log line — all real; then `b load_context`, a **tail call** |
| `load_context` (real, startup.o) | `current_processor`, `machine_set_current_thread`, then **`processor_up`** — unguarded, `bl 800fd3fc`, and a stub |

**Predicted: `stub_hit=processor_up`, `xnu_entry_stub_caller=0x8000e73c`.** `0x8000e73c` is
`load_context + 0x20`, the return address of the `bl` at 0x8000e738; `load_context` is in
`startup.o`, which links long before this step's object, so the address holds.

This is a much larger claim than 299's, so the falsifier was written down with it: any `stub_hit`
other than `processor_up`, from `set_sched_pri` inward, would mean a real function on that path
reaches a stub that reading it did not show. The `b load_context` being a tail call is why the
caller key is `0x8000e73c` and not a site inside `kernel_bootstrap` — 252's and 264's shape arriving
from the other side.

## The build

| | predicted | measured |
|---|---|---|
| undefined / function / storage | 760 / 672 / 88 | 760 / 672 / 88 |
| `.data` | 0x80118000 | 0x80118000 |
| `__bss_start` | 0x80130a00 | 0x80130a00 |
| `__bss_end` | 0x80167798 | **0x80167798** |
| `.bss` size | 224664 | 224664 |
| `.text` | 0x116780 → ~0x117294 | **0x117120** |
| image | 1247700 | **1247700** (unchanged) |

Three of the four layout lines landed exactly. **`__bss_end` is the interesting one: 299 got it wrong
by deriving it from sizes and this step got it right by deriving it from slots.** The retired
`sched_run_buckets` stand-in (`B 0x14`, 64-byte aligned) frees 0x40 from `realstubs.o`'s `.bss`
(0x1704 → 0x16c4, measured), and the object's own 0x14 plus the fill in front of those 64-aligned
arrays costs 0x38 — so the section shrinks by exactly 0x40.

### The `.text` miss, and it is 298's lesson in a second shape

`.text` was predicted 0x174 high. The estimate used `arm-none-eabi-size`'s **text column** for the
object — 3142 — and then added the object's `.rodata` (256) and `.rodata.str1.1` (22) on top. But
298 established that `size`'s text column already **is** the read-only sections plus `.text`, so
those two were counted twice. Corrected: the object's sections are `.text` 0xB30, `.rodata` 0x100,
`.rodata.str1.1` 0x16, and the realstubs deltas are −0x120 (twelve retired stubs × 24 bytes) and
−0x128 (their name strings), summing to 0x9FE against a measured 0x9A0 — the 0x5E between them is
section alignment across five inputs.

**`size`'s text column is a total, not a component.** That column has now been the source of an
error twice in three experiments ([[mi4-measurement-defects]], 298) — first as a number mistaken for
`.text`, now as a number mistaken for a summand.

## The run

```
stub_hit=processor_up                      xnu_entry_stub_caller=0x8000e73c
xnu_entry_bss_start=0x80130a00             (unchanged)
xnu_entry_bss_bytes=0x00036d98             (0x36dd8 -> 224664, as predicted)
xnu_entry_copied_bytes=0x001309d4          (unchanged)
```

Thirty-one bytes of call chain away from where the last run stopped. What that measures, in the order
it happened, none of it asserted before the run:

* `sched_set_thread_base_priority` **completed**, and `sched_compute_timeshare_priority` was resolved
  and *not* called, because `sched_mode` is TH_MODE_FIXED — the 288/294 family: a name the step
  retires without executing.
* `set_sched_pri` **completed**.
* **`thread_create_internal` ran to its `return KERN_SUCCESS`**, having queued the thread on the
  task, given it a thread id, taken its buckets and its ledger — with the `thread_t` written through
  the caller's out-pointer. **The first `kernel_thread_create` in this kernel returned a real
  thread.**
* `kernel_bootstrap`'s five remaining statements ran (`thread->state = TH_RUN`,
  `mach_absolute_time`, `thread_deallocate`, one log line) and **tail-called `load_context`**.
* The stop is `load_context`'s second call, one instruction past `machine_set_current_thread`.

**So this is the step where thread creation works**, and where the walk leaves the idiom it has
followed for a hundred steps: everything before this was a function returning to a caller that calls
the next function. `load_context` does not return — `machine_load_context` switches stacks and `eret`s
into the new thread — so from here the frontier is a context switch and the walk's question changes
from "which object" to "what does the first thread run".

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301113 bytes, one `stub_hit=` line and no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 301 — `osfmk/kern/machine.c` (manifest:569) for `processor_up`, the next call in
`load_context`; `machine_set_current_thread` and `current_processor` are already real and
`osfmk_kern_machine.o` is already built. Behind it `load_context` continues into `stack_alloc_try`
(real, guarded), `sched_run_incr` (**300 resolves it**), the `processor_state_update_explicit` block
with `thread_get_perfcontrol_class`, and `machine_load_context` (`osfmk/arm/cswitch.s`, a stub) —
which is the actual context switch.
