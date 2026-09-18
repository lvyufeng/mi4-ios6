# Experiment 302 — the commpage, and the walk arrives at the context switch itself

**Step:** link `osfmk/arm/commpage/commpage.c` (`osfmk_arm_commpage_commpage.o`) — the object that
defines `commpage_update_active_cpus`, where the 301 run stopped.
**Prediction:** `stub_hit=machine_load_context`, `xnu_entry_stub_caller=0x8000e804`.
**Result:** `stub_hit=machine_load_context` at `xnu_entry_stub_caller=0x8000e804`. Exact.

## Ten resolved, nothing asked for, and the stop is decided by the caller rather than by the object

`osfmk/arm/commpage/commpage.c` (manifest:433) was already built: `.text` 0x5B0, `.bss` 0xC,
`.rodata.str1.1` 0x13.

**10 resolved** — ten functions, **no storage at all**: `commpage_set_memory_pressure`,
`commpage_set_timestamp`, `commpage_update_active_cpus`,
`commpage_update_atm_diagnostic_config`, `commpage_update_boottime`, `commpage_update_kdebug_state`,
`commpage_update_mach_approximate_time`, `commpage_update_mach_continuous_time`,
`commpage_update_multiuser_config`, `commpage_update_timebase`.

**0 added**: all 18 of the object's references (21 minus three versuffix duplicates) are symbols this
image already carries, and **none of them is a stub**.

Five definitions are not stubs and have no reference yet: `commpage_populate`,
`commpage_set_spin_count`, and the three storage names `commPagePtr`, `_cpu_capabilities`,
`sharedpage_rw_addr`. **`commpage_populate` is the one worth naming**: it is the function that
*installs* the shared page, and its callers are in `arm_vm_init`/`pmap`, which this walk has not
touched. So this step links the commpage's *updaters* while nothing has created the commpage — and
that is safe in exactly the way the stopping function itself shows:

```
800e6f0c:  movw r0, #0x60c8 / movt r0, #0x8016   @ &commPagePtr - the object's own .bss at 0x801660c8
800e6f14:  ldr  r0, [r0]
800e6f18:  cmp  r0, #0
800e6f1c:  bxeq lr                                @ ***the commpage does not exist, so: return***
800e6f20:  movw/movt r0, 0x801660cc                 @ &sharedpage_rw_addr - the next 4-byte slot
800e6f30:  ldr  r0, [r0]                             @ the commpage's RW base
800e6f34:  ldr  r1, [0x80136f80]                      @ processor_avail_count
800e6f38:  strb r1, [r0, #52]                         @ the publish, at _COMM_PAGE_ACTIVE_CPUS+RW_OFFSET
```

So the function the 301 run stopped at **ran and published nothing**, because `commPagePtr` is a
zero-initialised 4-byte `.bss` slot in this very object and no one has called `commpage_populate`.
The value it would have published is real, though: `processor_avail_count` is defined by
`osfmk_kern_processor.o` (a linked object, `B 0x4` at 0x80136f80 per the map) and `processor_up`'s
`hw_atomic_add` had just made it 1. The step's five-statement body, the guard, and the fact that the
guard's answer is *zero* are all in one object.

## The prediction had to be closed against two reachable stubs the path does not take

This is the first step in the whole walk where the next stop is not in the linked object at all, so
the prediction is a reading of `load_context` and of the two stub-shaped things along it.

| at | what runs | |
|---|---|---|
| 0x800e6668 | `lck_spin_unlock(&pset->pset_lock)` → `b hw_lock_unlock` | real |
| 0x800e666c | `ml_cpu_up()` — two `hw_atomic_add` | real |
| 0x800e6670 | `splx(s)`, a tail `b ml_set_interrupts_enabled` | **falsifier 1, closed** |
| 0x8000e73c | `if (thread->kernel_stack) … else stack_alloc_try + panic("load_context")` | **falsifier 2, closed** |
| 0x8000e764 | `thread->state & TH_IDLE ? skip : sched_run_incr(thread)` | real (priority.o, 300) |
| 0x8000e788 | `processor->active_thread = thread` | |
| 0x8000e78c | `thread_get_perfcontrol_class(thread)` | real, 0x7C bytes, no stub branch |
| 0x8000e7a4 | `processor_state_update_explicit(...)` | real: `str`, `add`, `ldr`, `stm`, `bx lr` |
| 0x8000e7ac | `starting_pri`, `deadline = UINT64_MAX`, `last_processor` | |
| 0x8000e7c0 | `mach_absolute_time()` → `b ml_get_timebase` | real |
| 0x8000e7d8, 0x8000e7f0 | two `timer_start`s | real: four stores and `bx lr` each |
| **0x8000e7fc** | **`mov lr, pc` / `b machine_load_context`** | ***the stop*** |

**Falsifier 1 — `splx(s)`, and the only stub inside `ml_set_interrupts_enabled`.** The function has
exactly one `bl` into the stub object, `ast_taken_kernel` at 0x80016fd8
(`machine_routines_common.c:529`), behind `if (enable) … if (get_preemption_level() == 0) … while
(thread->machine.CpuDatap->cpu_pending_ast & AST_URGENT)`. `splx` passes back the value the *first*
`ml_set_interrupts_enabled` returned, so whether the AST arm is reachable turns on the interrupt state
`processor_up` was entered in.

**It is disabled, and the boot order is where that is read rather than assumed.** `kernel_bootstrap`
(`osfmk/kern/startup.c:252-411`), the function that calls `load_context`, contains **no
interrupt-enabling statement at all**, while `assert(ml_get_interrupts_enabled() == FALSE)`
(startup.c:548) and `(void) spllo(); /* Allow interruptions */` (startup.c:560) are both inside
`kernel_bootstrap_thread` (startup.c:413-660) — that is, on the far side of the context switch this
step stops at. So `ml_set_interrupts_enabled(0)` returns 0 — the return is `1 & ~(CPSR >> 7)`, and
`CPSR.I` is set — and `splx(0)` takes the `cmp r0,#0 / beq 0x80016fa8` arm: `cpsid if`, `pop`,
return. The `AST_URGENT` loop cannot be entered because `enable` is FALSE, whatever the thread's
pending-AST bit happens to be.

**Falsifier 2 — `stack_alloc_try`.** Its guard is `if (!thread->kernel_stack)`, and the thread came
from `kernel_thread_create`, which calls `stack_alloc(thread)` two statements later with
`assert(thread->kernel_stack != 0)` (`osfmk/kern/thread.c:1644-1645`). That function had already run
to completion in 300, since 300's stop is past it. So the guard is taken at 0x8000e744 and neither
`stack_alloc_try` nor `panic("load_context")` at 0x8000e760 is reached.

## The caller key is set by one instruction, not by a `bl`

```
8000e7fc:  mov lr, pc
8000e800:  b   800fd8d4 <machine_load_context>
```

The call is not a `bl`. `mov lr, pc` followed by a branch is `bl` semantics written out, and on ARM
reading `pc` as a source operand gives instruction + 8 — so 0x8000e7fc sets `lr = 0x8000e804`, and
that is the key the stub reports. `load_context` is in `startup.o`, which links far below this step's
object (0x8000e71c against 0x800e…), so appending an object cannot move it.

## The build

| | predicted | measured |
|---|---|---|
| undefined / function / storage | 745 / 658 / 87 | **745 / 658 / 87** |
| `.data` | 0x80118000 | 0x80118000 |
| `__bss_start` | 0x80130a00 | 0x80130a00 |
| `__bss_end` | 0x80167798 | **0x80167798** (unchanged, third step running) |
| `.bss` size | 224664 | 224664 |
| `.text` | 0x1176C0 → ~0x117A60 | **0x117AA0** |
| image | 1247700 | **1247700** (unchanged) |

Three counts exact, three layout lines exact. **`__bss_end` is unchanged for the third step running**,
and this time the mechanism is visible in the map rather than argued: `commpage.o`'s `.bss` (0xC) is
placed at 0x801660c8, *inside* the existing 0x38 `*fill*` in front of `realstubs.o`'s 64-aligned
arrays (0x801660c8 → 0x80166100), so the section's total is unmoved and `realstubs.o`'s own 0x1684
does not shrink either — no *storage* stub retires in this step, which is the other half of 299's
slot rule.

### The `.text` miss is 0x40, and its cause is the term the band got wrong

Four terms, each **exact in the map**:

| | |
|---|---|
| `commpage.o` `.text` | **+0x5B0** — placed 0x5B0 at 0x800e6ba8, after machine.o's 0x5F0 |
| `commpage.o` `.rodata.str1.1` | **+0x13** — placed 0x13 at 0x80112248, **not relaxed this time** |
| `realstubs.o` `.text` | **−0xF0** — 0x3EA0 → 0x3DB0: ten function stubs retired, 10 × 24 |
| `realstubs.o` `.rodata.str1.4` | **−0x148** — 0x34D4 → 0x338C |
| | **+0x38B**, against a measured section delta of **+0x3E0** |

**`0x148` was measured, not estimated**: the ten names were read one at a time out of the stub
object's `.rodata.str1.4` before the build, and they range from 0x18 (`commpage_set_timestamp`,
`commpage_update_active_cpus`) to 0x28 (`commpage_update_atm_diagnostic_config`,
`…mach_approximate_time`, `…mach_continuous_time`).

So the fifth term — the fill the linker inserts in front of aligned inputs — is **+0x55**, and the
prediction's band gave it 0..0x1F. That is a 0x40 miss and it is the same term 301 measured as +0x12.
It is a sum over *every* aligned input from the insertion point to the end of the section, and each
one follows modular arithmetic the step can compute: this object's `.text` is 0x5B0, which is 0x30
short of a multiple of 64, so every 64-aligned read-only input after it moves from fill *f* to
(*f* + 0x30) mod 64 — **+0x30 where *f* < 0x10, −0x10 otherwise**; every 32-aligned one shifts by
0x10 the same way; and the inputs after `realstubs.o`'s string section have shifted back by 0x378
instead, which is 0x18 mod 32 and 0x38 mod 64, moving *their* fills the other way.

**A `.text` prediction is `sum(measured inputs) + Σ(aligned fills)`, and the second term is neither
small nor one-signed nor bounded by one alignment.** For an insertion of this size the honest band is
0..+0x60, not 0..+0x20. 301's +0x12 and 302's +0x55 are the same term on steps that inserted 0x5F0
and 0x5B0.

## The run

```
stub_hit=machine_load_context                 xnu_entry_stub_caller=0x8000e804
xnu_entry_bss_start=0x80130a00                xnu_entry_bss_end=0x80167798   (both unchanged)
xnu_entry_bss_bytes=0x00036d98                xnu_entry_copied_bytes=0x001309d4
```

What that measures, in the order it happened, none of it asserted before the run:

* `commpage_update_active_cpus` **ran and returned without publishing** — `commPagePtr` is zero, so
  the guard at 0x800e6f1c took the `bxeq lr`. The commpage's updaters are now real code and the
  commpage still does not exist.
* `processor_up`'s remaining two statements ran: the pset spinlock was released and `ml_cpu_up`
  incremented its two counters.
* `splx(s)` **took the disabled arm** — `ast_taken_kernel` was not called, which is falsifier 1
  closed by measurement as well as by reading.
* The `thread->kernel_stack` guard was taken: `stack_alloc_try` and its `panic("load_context")` were
  not reached, falsifier 2 likewise.
* `sched_run_incr`, `thread_get_perfcontrol_class`, `processor_state_update_explicit`,
  `mach_absolute_time` and **both** `timer_start`s completed.
* The stop is `machine_load_context` — and `lr` at that point is 0x8000e804, which is the value the
  `mov lr, pc` at 0x8000e7fc wrote.

**The processor is now fully up, the first thread is bound to it, its timers are armed, and the only
thing left in `load_context` is the instruction that transfers control to it.**

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`),
log 301121 bytes, one `stub_hit=` line and no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10, `ro.product.device` = cancro).

**Next:** experiment 303 — `osfmk/arm/cswitch.s` (manifest:438, built as `out/xnu_asm_obj/cswitch.o`)
for `machine_load_context`.

**This is the step where the walk stops being a walk.** Everything up to here has been a function
returning to a caller that calls the next function, and the frontier has been a name to link.
`machine_load_context` is not a name to link: it switches stacks and `eret`s into
`kernel_bootstrap_thread`, so **it does not return**, the caller-key idiom ends, and what follows is
the first thread actually running — and the first thing that thread does is `idle_thread_create`.
