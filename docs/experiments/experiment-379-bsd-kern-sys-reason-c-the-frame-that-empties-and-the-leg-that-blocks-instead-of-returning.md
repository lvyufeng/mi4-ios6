# Experiment 379 — `bsd/kern/sys_reason.c`: the frame 378 stopped in, and the leg that blocks instead of returning

**Step:** link one object, `bsd/kern/sys_reason.c` (`bsd_kern_sys_reason.o`) — the pool's only definer
of 378's stop `os_reason_free` and of the five `os_reason_*` names beside it. It is inserted into the
entry link between `osfmk_kern_work_interval.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`.
Nothing else changes.

**Prediction:** *6 resolved (6 function, 0 storage) / 0 added — 748 → **742** undefined, 650 → **644**
function, 98 → **98** storage*; the object's `.text` **0x801802A0** (0x378), its `.rodata.str1.1`
**0x801A6B49** (0x40), its `.bss` **0x801FCF2C** (0x10) and its `__DATA,__data` **0x801C6350** (0x30);
platform expert `.text` **0x80180618** (0x150); `realstubs.o` `.text` **0x801807C4** (0x3C60); the
`.rodata` run's start **0x801846D0**; `realstubs.o` `.rodata.str1.4` **0x801A7130 (0x3622)**;
`__TEXT, initcode` **0x801AA760** (0x64C) behind a pad of **0x8**; text size **1749280 (0x1AB120)**;
`.data` **0x801AC000 (0x1A380)**; `.sysctl_set` **0x801C6380 (0x158)**; `.init_array` **0x801C64D8
(0x90)**; `.bss` **0x801C6580 (0x39058)**; `__bss_end` **0x801FF5D8**, args **0x80201000**,
headroom **2099752**, image **1860968 (0x1C6568)**; and the stop at
**`proc_encode_exit_exception_code`** at key **`0x80009628`**.

**Result:** all three counts exact (742 / 644 / 98), `xnu_arm_entry_undef.txt` reading 742 lines and
`xnu_arm_entry_stubnames.txt` 644 `func` and 98 `data` records; **every `.text`-side row exact**,
including both of the pads this step's arithmetic turns on; `.data`, `.sysctl_set`, `.init_array`, the
image, the args page and `topOfKernelData` exact. One prediction miss, in `.bss`: its **start** is
exactly as written (0x801C6580) but the **object's own 0x10 lands at 0x801FCF6C**, not the 0x801FCF2C
written, because the block moved the bucket's start by 0x40 and kept its 378 addresses for the entries
*inside* the bucket — so the tail is +0x40 too, `realstubs.o`'s slot moves to 0x801FCFC0 and
`__bss_end` is **0x801FF618** with headroom **2099688**. And **the stop is in a different thread**:
the run reports `stub_hit=ipc_kmsg_dequeue` at key **`0x800dad30`** = **`ipc_mqueue_destroy_locked +
0x54`** — an IPC port teardown, reached after `thread_terminate_self` took the leg its own falsifier
(e) named and **blocked** in `thread_block`, so the frames above this one are the scheduler's and not
a return path at all.

## The object

```
== bsd_kern_sys_reason.o
   17 definitions, 17 references
   resolved (6: 6 function, 0 storage)
      os_reason_alloc_buffer                                     object T, stand-in was func T
      os_reason_alloc_buffer_noblock                             object T, stand-in was func T
      os_reason_create                                           object T, stand-in was func T
      os_reason_free                                             object T, stand-in was func T
      os_reason_init                                             object T, stand-in was func T
      os_reason_ref                                              object T, stand-in was func T
   added (0: 0 function, 0 storage)
   of the 17 references, 17 are already satisfied
```

Six names retired, none created, and all 17 references already real — the fourth object in a row whose
whole closure is satisfied. All six retired records are *function* records, so no data record retires,
which is what keeps `realstubs.o`'s `.bss` — and every address below it — where it is. Four sections,
one of them the first `.bss` input in four steps:

```
.text                           0x378  2**2  the six functions
.rodata.str1.1                  0x040  2**0  three strings
.bss                            0x010  2**2  os_reason_alloc_buffer_internal.site.3
__DATA, __data                  0x030  2**3  group 2 of the .data output section; no `.data` at all
```

## The stop the block predicted, and the guard that was already there

378's predicted stop was `proc_encode_exit_exception_code` — `thread_terminate_self + 0x1C8`, key
`0x80009628` — and it failed because the frontier had descended one frame into `uthread_cleanup`. This
step retires the stub that stopped there, so the walk resumes in `uthread_cleanup` after `+0xD0` and
returns to `thread_terminate_self + 0x120`. The chain the block wrote, read out of the source this
time (`osfmk/kern/thread.c`, `thread_terminate_self`), is:

```
    task = thread->task;
    uthread_cleanup(task, thread->uthread, task->bsd_info);      <- 378's frame, +0x11C
    if (task->bsd_info && !task_is_exec_copy(task)) { kdbg_trace_data(...); }
    threadcnt = hw_atomic_sub(&task->active_thread_count, 1);    <- +0x168
    if (threadcnt == 0 && task->bsd_info != NULL && !task_is_exec_copy(task)) {
            kdbg_trace_string(...);
            subcode = proc_encode_exit_exception_code(task->bsd_info);   <- +0x1C8, the prediction
            proc_exit(task->bsd_info);
            ...
    }
    if (threadcnt == 0) { ... thread_wakeup(&task->active_thread_count); ... }
    uthread_cred_free(thread->uthread);                          <- +0x228
    ... wait-timer cancel / delay loop ...
    stack_free_reserved(thread); thread_mark_wait_locked(thread, ...);
    thread_block(THREAD_CONTINUE_NULL);                          <- +0x2F8
```

**And the block named this leg before the run, as falsifier (e)**: `+0x168 bl hw_atomic_sub`, `cmp
r0,#0`, `bne 0x80009684` — "that branch (the thread still has references) leads to a
`timer_call_cancel`/`delay` loop whose next stub this block cannot name. A stop past 0x80009684 is a
state, not a layout miss."

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x800dad30       xnu_entry_abort_entries=0x00000000
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
 xnu_entry_image_bytes=0x001c6568         xnu_entry_bss_bytes=0x00039098
 xnu_entry_bss_start=0x801c6580           xnu_entry_bss_end=0x801ff618
 xnu_entry_args_pa=0x80201000             xnu_entry_top_of_kernel_data=0x80400000
 xnu_entry_why=0x80184c0c                 xnu_entry_why_byte=0x00000061
 xnu_entry_checksum=0x907fe6b5
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ipc_kmsg_dequeue
 xnu_entry_stub_caller=0x800dad30
 xnu_entry_stub_caller_a=0x800dad30
 xnu_entry_stub_caller_e=0x800dad30
```

`os_reason_free` is gone from the stub list, `abort_entries=0` with `abort_first_pc=0` and
`abort_first_dfar=0`, `checks=5`, `failures=0`, no `panic` line, and the log ends in the kernel's own
`No errors detected`.

### The leg the run took: the block that contains the predicted stub was skipped

The stop is **not** `proc_encode_exit_exception_code`, and the reason is in the guard above it rather
than in the object linked. `hw_atomic_sub` was called and returned without stopping (it is real), and
the three-way guard that follows — `threadcnt == 0 && task->bsd_info != NULL &&
!task_is_exec_copy(task)` — did not let the walk into the block that contains the predicted call. So
**the walk took the leg the source labels `bne 0x80009684` and the block labelled falsifier (e)**:
`uthread_cred_free` at `+0x228`, the wait-timer `timer_call_cancel` / `delay` loop, `stack_free_reserved`,
`thread_mark_wait_locked`, and then **`thread_block`**, which is unconditional on this path.

**Which of the three conditions was false is not readable from the log, and the block should not pretend
it is.** `task->active_thread_count` still being non-zero and `task->bsd_info` being `NULL` skip the
*same* block, both are state, and both are consistent with every number in the log. What the log does
settle is the leg: every call between `+0x228` and `+0x2F8` is real, and `thread_block` is reached.

### And the stop is in another thread: the first context switch this walk has stopped behind since 302

`thread_block(continuation)` marks the thread waiting and hands the processor to the next runnable
thread, so from that instruction on **the frames the walk is executing are the scheduler's, and the
code it eventually runs is another thread's**. That is the structural finding of this step and it is
why the frames above the stop cannot be named from the image: the wall is not a missing symbol, it is
that `lr` no longer describes a call.

What *can* be named is one frame wide and it is exact — the `bl` at **0x800dad2c**, whose return
address is the reported key **0x800dad30**:

```
800dacdc <ipc_mqueue_destroy_locked>:            ; osfmk/ipc/ipc_mqueue.c, real since 278
  ...
800dad24: bl 800a8300 <waitq_wakeup64_all_locked>
800dad28: mov r0, r5
800dad2c: bl 8018125c <ipc_kmsg_dequeue>         ; <- THE STOP, key 0x800dad30
800dad30: cmp r0, #0
800dad34: ...
800dad38: beq 800dad5c
800dad40: bl 80181244 <ipc_kmsg_delayed_destroy> ; the loop body's other stub
800dad50: bl 8018125c <ipc_kmsg_dequeue>
```

which is `ipc_mqueue_destroy_locked`'s `while ((kmsg = ipc_kmsg_dequeue(kmqueue)) != IKM_NULL)` loop —
the port-message-queue teardown, called from one of the two port functions in the table below and **not**
from the thread-teardown frame the block was reasoning about. `ipc_mqueue_destroy_locked` is real
(linked at 277/278, `osfmk_ipc_ipc_mqueue.o`), and the
stub it calls is defined by `osfmk/ipc/ipc_kmsg.c` and by nothing else in the pool.

The tree above this frame is a wall of a kind this walk has not met before, and it is worth writing
down exactly how far it can be walked:

| | in-image `bl` callers | in-image tail (`b`) callers |
|---|---|---|
| `ipc_mqueue_destroy_locked` | `ipc_port_clear_receiver+0x60`, `ipc_port_destroy+0x1C4` | — |
| `ipc_port_destroy` | `ipc_kobject_notify+0x1AC`, `ipc_port_release_receive+0x24` | `ipc_port_dealloc_special+0x2C` |
| `ipc_port_clear_receiver`, `ipc_port_release_receive`, `ipc_kobject_server` | none | none |
| `ipc_port_dealloc_special` | `ipc_thread_terminate`, `ipc_thread_reset`, `ipc_task_terminate`, `ipc_task_reset`, `iokit_destroy_object_port`, `work_interval_port_notify`, `semaphore_dereference`, `iv_dealloc`, `convert_voucher_to_port` | — |

Every one of those is a *teardown* path — a thread's or a task's IPC objects being released — and the
chain breaks at the two port functions because nothing in the image calls them except by a tail call or
from outside it. That is not a defect in the image: it is what a context switch looks like from a
return address. **A stop reached through `thread_block` names the frame it is in and nothing above it**,
and the honest statement of the frontier is one frame wide.

### The layout: one `.bss` row read against a stale neighbour

Every `.text`-side row came out exactly as written — the object's own `.text` at 0x801802A0 (0x378),
the expert at 0x80180618, rtabi at 0x80180770, `realstubs.o` at 0x801807C4 (0x3C60 = 644 × 0x18), the
`.rodata` run's start at 0x801846D0, and every string row a flat **+0x2E8** of 378's — including the
object's own `.rodata.str1.1` at **0x801A6B49 (0x40)**, the first mergeable row in five steps that
measured exactly the object's own size *and* landed at the predicted address, because none of its three
strings occurs anywhere in the image the device ran.

**And both pads moved, in the same direction as the shift, exactly as the block's accounting said**:
the pad before `__TEXT, initcode` measured **0x8** where 378's was 0x4, and the closing `ALIGN(32)` paid
**0x10** because the raw end 0x801AB110 is not 32-aligned. Text size **1749280 (0x1AB120)**, `.data`
**0x801AC000 (0x1A380)**, `.sysctl_set` **0x801C6380 (0x158)**, `.init_array` **0x801C64D8 (0x90)**, the
image **1860968 (0x1C6568)** — all exact. The two pads are the terms no sum of section *contents* can
represent, and this is the second step running where they carry the difference.

**The `.bss` block is where the prediction was wrong, and the error is the shape of the arithmetic
rather than the arithmetic.** The block wrote that the bucket's start moves 0x40 (0x801C6540 →
0x801C6580, from `.data` growing 0x30) and that its *end* does not, on the grounds that
`realstubs.o`'s 64-aligned slot would absorb the object's new 0x10 in its pad. The start is right, and
the pad rule is right — `*fill* 0x3C` became `*fill* 0x2C`, giving up exactly 0x10 — but the same 0x40
that moves the bucket's start moves **every address inside the bucket**, because `.bss` inputs are
placed sequentially from that start. The block applied the 0x40 to the bucket label and kept 378's
addresses for the entries underneath it: it has `iokit_Kernel_IOPowerConnection.o` at 0x801FCF14 (its
378 address) where the map says **0x801FCF54**. From there the whole tail is +0x40 as well, and the
rule the block wrote holds exactly once the base is right — the object's 0x10 begins where
IOPowerConnection.o's 0x18 ends, at **0x801FCF6C**; `MSM8974PlatformExpert.o`'s 0x18 follows at
0x801FCF7C; `xnu_arm_entry_macho.o`'s 0x0 at 0x801FCF94; `align64` of that is **0x801FCFC0**, so
`realstubs.o`'s slot moves and its pad gives up the object's 0x10; `realstubs.o` ends 0x801FF604, and
`+0x4` (align 8) with `+0x10` put `__bss_end` at **0x801FF618**.

**So the bucket's content span is unchanged at 0x39084** — start +0x40, content +0x10, pad −0x10 — and
the size the map prints, which includes the trailing align-8 pad and the 0x10 past it, comes out
**0x39098**: the very number 378 printed, for the opposite reason. 378's start and end both stood
still; this step's *both* moved by 0x40. Everything derived above `.bss` follows: the image is
`.init_array` end − base and does not include `.bss`, so it is exact at 1860968; `args` is
`align_up(0x1FF618, 0x1000) + 0x1000` = **0x80201000**, still the page 0x1FEDD8's bucket gave;
`topOfKernelData` 0x80400000, the tree 0x80600000 and the window 0x80800000 unmoved; and only
`headroom` changes, by exactly the 0x40 the bucket's end moved — 2099752 → **2099688**.

**And this is 378's phase rule read from its other side.** That step's delta was 4 mod 8 and re-phased
every 8-aligned input; this step's `.bss` delta is **0x40, which is 0 mod 64**, and the one input that
could have absorbed it does not re-phase anything: the pad takes the object's 0x10 and nothing else moves
in kind. A delta that is a multiple of the alignment survives a round of it invisibly, which is why this
miss touched `.bss`'s size and every address below it and not a single `.text` row.

**The one other defect is a transcription in the prediction itself**: the text size was written as
`1753376` beside the hex `0x1AB120`, and 0x1AB120 is 1749280. The hex is what the arithmetic produces
and what the map prints, so this is the hex-to-decimal class again (370's and 377's), and it was
corrected in place in the block rather than recorded as a miss.

## The layout

| | 378 measured | 379 predicted | 379 measured |
|---|---|---|---|
| counts | 748 / 650 / 98 | **742 / 644 / 98** | **742 / 644 / 98** |
| object `.text` | — | 0x801802A0 (0x378) | **0x801802A0 (0x378)** |
| platform expert `.text` | 0x801802A0 (0x150) | 0x80180618 (0x150) | **0x80180618 (0x150)** |
| rtabi `.text.eabi` | 0x801803F8 (0x54) | 0x80180770 (0x54) | **0x80180770 (0x54)** |
| `realstubs.o` `.text` | 0x8018044C (0x3CF0 = 650 × 0x18) | 0x801807C4 (0x3C60 = 644 × 0x18) | **0x801807C4 (0x3C60)** |
| `.rodata` run start | 0x801843E8 | 0x801846D0 | **0x801846D0** |
| `kern_malloc` `.rodata.str1.1` | 0x801A61E6 (0x4CF) | +0x2E8 | **0x801A64CE (0x4CF)** |
| `Tests` `.rodata.str1.1` | 0x801A66B5 (0x0A) | 0x801A699D (0x0A) | **0x801A699D (0x0A)** |
| work_interval `.rodata.str1.1` | 0x801A66BF (0x1A2) | 0x801A69A7 (0x1A2) | **0x801A69A7 (0x1A2)** |
| object `.rodata.str1.1` | — | 0x801A6B49 (0x40) | **0x801A6B49 (0x40)** |
| `realstubs.o` `.rodata.str1.4` | 0x801A6E08 (0x369E) | 0x801A7130 (0x3622) | **0x801A7130 (0x3622)** |
| `__TEXT,__const` / pad / `initcode` | 0x801AA4A8 / 0x4 / 0x801AA4B0 | 0x801AA754 / 0x8 / 0x801AA760 | **0x801AA754 / 0x8 / 0x801AA760** |
| `.ARM.exidx` / fill / raw end | 0x801AAE58 / 0x8 / 0x801AAE60 | 0x801AB108 / 0x10 / 0x801AB110 | **0x801AB108 / 0x10 / 0x801AB110** |
| text size | 1748576 (0x1AAE60) | 1749280 (0x1AB120) | **1749280 (0x1AB120)** |
| `.data` | 0x801AC000 (0x1A350) | 0x801AC000 (0x1A380) | **0x801AC000 (0x1A380)** |
| object `__DATA,__data` | — | group 2, 0x30 | **0x801C6350 (0x30)** |
| `.sysctl_set` | 0x801C6350 (0x158) | 0x801C6380 (0x158) | **0x801C6380 (0x158)** |
| `.init_array` / its end | 0x801C64A8 (0x90) / 0x801C6538 | 0x801C64D8 (0x90) / 0x801C6568 | **both exact** |
| `.bss` start | 0x801C6540 | 0x801C6580 | **0x801C6580** |
| its neighbour `IOPowerConnection.o` | 0x801FCF14 (0x18) | 0x801FCF14, unmoved | **0x801FCF54 (0x18)** |
| object `.bss` | — | 0x801FCF2C (0x10) | **0x801FCF6C (0x10)** |
| `realstubs.o` `.bss` | 0x801FCF80 (0x2644) | 0x801FCF80, unmoved | **0x801FCFC0 (0x2644)** |
| its pad | `*fill* 0x3C` | 0x2C | **`*fill* 0x2C`** |
| `.bss` size | 0x39098 | 0x39058 | **0x39098** |
| `__bss_end` | 0x801FF5D8 | 0x801FF5D8 | **0x801FF618** |
| image | 1860920 (0x1C6538) | 1860968 (0x1C6568) | **1860968 (0x1C6568)** |
| headroom | 2099752 | 2099752 | **2099688** |
| args / topOfKernelData / tree | 0x80201000 / 0x80400000 / 0x80600000 | all unmoved | **all unmoved** |

**And the falsifiers, one by one:** (a) `proc_encode_exit_exception_code` at `0x80009628` — **not hit**,
because the guard above it was not satisfied (the section above). (b) no stop at `os_reason_free` — yes,
the name is gone from the stub list. (c) the layout — everything exact except the `.bss` tail, as
measured above. (d) `vfork_exit_internal` at `0x800D6A04` — **not hit**: `uthread_cleanup`'s `UT_VFORK`
guard did not fall through (and it is a stub, so a hit would have been unmistakeable). (e) the refcount
leg past `0x80009684` — **this is the one that fired**, and it fired in the form the block predicted: a
stop past `0x80009684`, in a frame the block's tooling could not name. (f) the `lck_mtx_lock` leg —
not reached, and unreachable to a named stop, as written.

## The next object, named before its run

`ipc_kmsg_dequeue` and `ipc_kmsg_delayed_destroy` — the two stubs in the frame the run is standing in —
are both defined by **`osfmk_ipc_ipc_kmsg.o`** (`osfmk/ipc/ipc_kmsg.c`) and by nothing else in the pool.
Measured against this image: 54 definitions, 73 references, of which 58 are already satisfied —
**13 resolved (12 function, 1 storage) / 15 added (15 function, 0 storage)**, i.e. **742 → 744
undefined, 644 → 647 function, 98 → 97 storage**. The one retired *storage* record is `ipc_kmsg_zone`
(`pool B 4`) — the first storage record to retire in five steps, and the first change to
`realstubs.o`'s `.bss` since 374, whose slot table is a sum of per-record `align64(size)` terms and
therefore gives up exactly `align64(4)` = 0x40 — and the 15 added names are `ipc_entries_hold`, `ipc_entry_claim`, `ipc_entry_dealloc`,
`ipc_entry_grow_table`, `ipc_notify_port_deleted`, `ipc_object_copyin_from_kernel`,
`ipc_object_copyin_type`, `ipc_object_copyout_dest`, `ipc_object_destroy`, `ipc_object_destroy_dest`,
`ipc_right_copyin`, `ipc_right_copyin_check`, `ipc_right_copyin_two`, `ipc_right_copyout`,
`ipc_right_reverse` — the entry-rights face of IPC, all of them already compiled in the pool.

**The prediction this step can make is short, and it is not the frame's own loop — and that is the one
thing in this section that was written wrong.** *Corrected in place before 380's build, when the
contradiction was noticed; the prediction as first written is quoted at the end of this section.* The
section's own first sentence says `ipc_kmsg_dequeue` **and** `ipc_kmsg_delayed_destroy` are both defined
by `osfmk_ipc_ipc_kmsg.o`; the prediction then said that with `ipc_kmsg_dequeue` real the next stop the
walk can reach is `ipc_kmsg_delayed_destroy` at key `0x800dad44`. That is the other symbol of the same
object, retired by the same link. **A prediction that names a stop its own step retires is empty, and
the state it hung on — "only if the queue is non-empty" — does not rescue it: the frame cannot stop in
either state.**

Disassembled, `ipc_mqueue_destroy_locked` is `0xa0` bytes (`0x800dacdc`..`0x800dad7c`) with exactly
three stub calls, all three inside the loop and all three in the object 380 links —
`ipc_kmsg_dequeue` at keys `0x800dad30` and `0x800dad54`, `ipc_kmsg_delayed_destroy` at `0x800dad44` —
and everything after the loop is real: `strh r7, [r4, #44]`, `waitq_invalidate_locked` (`0x800a8d14`,
a four-instruction leaf that ends `bx lr`), `waitq_clear_prepost_locked` (`0x800a9274`), `pop
{r4, r5, r6, r7, fp, pc}`. Both queue states run that tail, and the frame returns.

**What the walk does next is therefore decided by which of the two callers it returns into — and that
is the state the log cannot read**, because the frame was reached through a context switch, so no
caller sits on the stack to name. The two are nameable anyway, and so is their continuation:

* **`ipc_port_clear_receiver + 0x60`** (`bl` at `0x800d7c78`): `imq_unlock`, then `return reap_messages`.
  `ipc_port_clear_receiver`'s only stub call is the `ipc_pset_remove_from_all` at `+0x24`
  (`0x800d7c3c`), which **precedes** the call the run is in and is guarded by `ip_in_pset != 0`, so
  there is no stop left anywhere in it; and its own caller is not in the image (the literal-pointer
  search below). **No nameable stop.**
* **`ipc_port_destroy + 0x1C4`** (`bl` at `0x800d81e4`, on the arm taken when `pdrequest == IP_NULL` —
  which this run must have taken, since the `ipc_notify_port_destroyed` stub at `0x800d8134` precedes
  the call and was not hit): the walk continues at `0x800d81e8`, `waitq_unlock`, the `io_bits & 0x8000`
  test, `lck_spin_unlock`, and rejoins the common tail at `0x800d828c`. The stub calls still reachable
  from there, in address order, are `ipc_kmsg_free` at key **`0x800d828C`** and `ipc_kmsg_reap_delayed`
  at key **`0x800d82CC`** — *both retired by this same link* — and then the three this image really
  still lacks: `ipc_notify_send_once` at key **`0x800d82C0`** (guarded by `port->ip_nsrequest !=
  IP_NULL`), `ipc_notify_dead_name` at key **`0x800d8330`** (inside the `ip_dead_names` scan), and
  `io_free` at key **`0x800d8380`** (guarded by the port's last reference, `io_references == 1`).

So 380's prediction is **(a) `ipc_notify_send_once` at key `0x800d82C0`**, **(b) `ipc_notify_dead_name`
at key `0x800d8330`**, **(c) `io_free` at key `0x800d8380`**, or **(d) no nameable stop at all** — and
in no case a stop in the frame the run is standing in. Falsifier: a stop inside
`ipc_mqueue_destroy_locked`'s own frame, or at either of the two retired keys. **(d) is not a miss** —
it is the answer to the question the previous two steps could not ask, and it names the caller.

*The prediction as first written, kept because the defect is the point:* "with `ipc_kmsg_dequeue` real,
the next stub the walk can reach is `ipc_kmsg_delayed_destroy` at key `0x800dad44` — the `bl` at `+0x64`,
the loop body — and it is reached only if the queue the port is tearing down is non-empty; if it is
empty the `while` exits on the first call … and the frame returns into one of the two port callers
above, where the next stub is not nameable from the image." The falsifier it named, "no stop in this
frame at all", is the right answer; what is wrong is (i) the first branch, which the link it describes
retires, and (ii) the claim that the caller's continuation is unnameable, which the disassembly above
names three deep.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img` (4880384 bytes, sha256
`0b0b8f9d3f8b4d85855d373eb35d9c9c07d790ff09b36d38af8b2d0f2fc6bdc6`); nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading
of either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `checksum=0x907fe6b5`,
`abort_entries=0` with `abort_first_pc=0` and `abort_first_dfar=0`, no `exception:` line. Log 301628
bytes, 3975 lines, last line `No errors detected`. The device came back to Android on its own.

The step's own code is the reason pool's five entry points over storage the kernel had already
allocated, and all 17 of its references are already satisfied, so nothing new could fault at load. What
it *unlocked* is the rest of `uthread_cleanup`'s exit path and, past it, a frame of
`thread_terminate_self` the walk has never executed — and that frame's first new decision is the
`active_thread_count` subtraction, which is where this run left the bootstrap thread's straight line.
From here the walk's stops are as likely to name a frame in another thread as one in this one, and the
recovery nets (`sleepGate` returning under the hardware watchdog, and the software dead-man firing on a
silent boot) are what catch a stop that is not a stub; they were not needed.
