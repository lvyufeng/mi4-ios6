# Experiment 378 — `osfmk/kern/work_interval.c`: the stub 377 stopped at, and a stop one frame *down* rather than one call on

**Step:** link one object, `osfmk/kern/work_interval.c` (`osfmk_kern_work_interval.o`) — the pool's
only definer of 377's stop `work_interval_thread_terminate` and of `work_interval_port_notify` beside
it. It is inserted into the entry link between `iokit_Tests_Tests.o` and
`stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else changes.

**Prediction:** *2 resolved (2 function, 0 storage) / 0 added — 750 → **748** undefined, 652 → **650**
function, 98 → **98** storage*; the object's `.text` **0x8017FCF4** (0x5AC) and its `.rodata.str1.1`
**0x801A66BB** (0x1A2); platform expert `.text` **0x801802A0**; `realstubs.o` `.text` **0x8018044C**
(0x3CF0 = 650 × 0x18); the `.rodata` run's start **0x801843E8**; `realstubs.o` `.rodata.str1.4`
**0x801A6E04 (0x369E)**; text size **1748576** (0x1AAE60); `.data` **0x801AC000 (0x1A350)**;
`.sysctl_set` **0x801C6350 (0x158)**; `.init_array` **0x801C64A8 (0x90)**; `.bss` **0x801C6540
(0x39098)** with start, size *and* end unmoved; `__bss_end` **0x801FF5D8**, args **0x80201000**,
headroom **2099752**, image **1860920**; and the stop at **`proc_encode_exit_exception_code`** at key
**`0x80009628`**.

**Result:** all three counts exact (748 / 650 / 98, and `xnu_arm_entry_stubnames.txt` reading 650
function and 98 data records), every `.text` row exact, `.data`, `.sysctl_set`, `.init_array`, `.bss`,
`__bss_end`, the args page, `topOfKernelData`, the headroom and the **text size** all exact to the
byte — and **the stop is not the one predicted, but it is in the frame the prediction was standing
in**: the run reports `stub_hit=os_reason_free` at key `0x800d69c4` = **`uthread_cleanup + 0xD0`**.
`thread_terminate_self` called the real `uthread_cleanup` at `+0x11C` — a callee the block's own tool
listing labelled **real**, which is true and is not the question — and `uthread_cleanup`'s body
reaches a stub of its own before the frame ever gets back to `+0x1C8`. One layout miss, of 0x4, and
it is a *phase shift*: the `.text` run's delta is 0x57C ≡ 4 (mod 8), so every 8-aligned input in the
`.rodata` run re-rounds its fill by 4, and the shift rides the run to the tail, where the `initcode`
pad absorbs exactly 0x4 and the text size lands as predicted.

## The object

```
== osfmk_kern_work_interval.o
   16 definitions, 21 references
   resolved (2: 2 function, 0 storage)
      work_interval_port_notify                                 object T, stand-in was func T
      work_interval_thread_terminate                            object T, stand-in was func T
   added (0: 0 function, 0 storage)
   of the 21 references, 21 are already satisfied
```

Two names retired, none created, and all 21 of its references are already real — which is why nothing
inside it can stop this run. The two retired records are function records, so **no data record
retires and `realstubs.o`'s `.bss` does not change at all**, which is why every layout row at or below
`.bss` was predicted unmoved. Seven sections, and one of them is new to this walk's arithmetic:

```
.text                           0x5AC  2**2  the two functions
.rodata.str1.1                  0x1A2  2**0  eight strings
.data                           0x008  2**3  group 1 of the .data output section
__DATA, __data                  0x018         group 2 of the same output section
.comment / .ARM.attributes
```

**The two data sections land in *different groups* of one output section**, which is 377's finding
read forward: `entry.ld` places `*(.data .data.*)` first and `*("__DATA,*__data" "__DATA,*__const")`
after it, so this object's `.data` 0x8 appends inside group 1 (at 0x801C50F0, 8-aligned, so no fill)
while its `__DATA,__data` 0x18 goes into group 2. That is why the bucket grows by two named terms
rather than one — and both were written before the run and both came out exact.

## The stop the block predicted: the same function, three calls further on

`thread_terminate_self` has 193 instructions and 45 direct calls; `tools/first_stub_call.py` on it
reports the stub at `+0xE4` and, asked from `+0xE8`, the next one is
**`proc_encode_exit_exception_code` at `+0x1C8`** (key `0x80009628`), with `lck_mtx_lock`,
`thread_policy_reset`, `lck_mtx_unlock`, `bank_swap_thread_bank_ledger`, `uthread_cleanup` and
`task_is_exec_copy` all listed **real** in between. `osfmk/kern/thread.c:585` is the source:
`subcode = proc_encode_exit_exception_code(task->bsd_info);` inside the same teardown. The prediction
was written as conditional on the frame being resumed at all, and the falsifiers below named the
alternatives the call walk could see.

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x800d69c4       xnu_entry_abort_entries=0x00000000
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
 xnu_entry_image_bytes=0x001c6538         xnu_entry_bss_start=0x801c6540
 xnu_entry_bss_end=0x801ff5d8             xnu_entry_args_pa=0x80201000
 xnu_entry_top_of_kernel_data=0x80400000  xnu_entry_why_byte=0x00000061
 xnu_entry_checksum=0x907fe5e5
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=os_reason_free
No errors detected
```

The frame chain is the one the prediction was standing in, one level deeper than it looked:

```
thread_apc_ast                        the only caller of thread_terminate_self
  thread_terminate_self  +0xE4        work_interval_thread_terminate  <- 377's stop, retired
                         +0xEC..+0x108 real, as the block said
                         +0x11C       uthread_cleanup   <- *real*, and the block stopped reading there
    uthread_cleanup       +0xD0       os_reason_free    <- THE STOP, key 0x800d69c4
```

`work_interval_thread_terminate` is gone from the stub list altogether, `abort_entries=0` with
`abort_first_pc=0` and `abort_first_dfar=0`, `checks=5`, `failures=0`, no `panic` line, and the log
ends in the kernel's own `No errors detected`.

### The defect the run found: "not a stub" is not "does not contain a stub"

The block asked `tools/first_stub_call.py` for `thread_terminate_self`'s direct calls from `+0xE8`
and got a list in which `uthread_cleanup` is labelled **real**. That is true, and it is not the
question: `uthread_cleanup`'s own body reaches `os_reason_free` at `+0xD0` before
`thread_terminate_self` ever returns to `+0x1C8`. **The instrument that answers the right question is
the call *walk*, which descends** — run on `uthread_cleanup` it names `os_reason_free` as the first
stub on the straight-line path, with the `throttle_lowpri_io` call at `+0x2C` listed as the first
*guarded* alternative. The run took neither the alternative nor the prediction: `uthread_cleanup`'s
two guards at `+0x24`/`+0x28` (`ldr r0,[r1,#0x140]` / `cmp #0` / `bne`, then `ldr r0,[r5,#0x138]` /
`beq`) both read zero on this thread, so the `throttle_lowpri_io` site was skipped and the walk went
straight to the unconditional call at `+0xD0`.

**The two tools disagree about the `+0x2C` site and the call walk is the one that matched**:
`first_stub_call.py` counts a call as straight-line where the walk sees the guard. So the block's
error was not in either tool but in the question it asked — it wanted "what does the *path* reach",
and it read a per-callee answer. **This is 375's lesson in a new place**: the frontier is not the
caller's next call, it is the first stub the path reaches, and a path descends. The cheap check is to
walk from each frame the previous run named, not to list that frame's direct calls.

### And the layout miss: a phase shift, absorbed by an `ALIGN`

Every `.rodata` row from `osfmk_arm_pmap.o`'s `.rodata` (0x8018DA18) onward is **+0x4** of the
+0x57C predicted, and the term that appears is the *fill* in front of it: **0x2 → 0x6**. The `.text`
run's delta is 0x57C, and 0x57C mod 8 = 4, so every input in the `.rodata` run whose alignment is 8
or 16 re-phases — its fill is `(−cursor) mod align`, and a cursor moved by 4 re-rounds by 4. That one
fill then rides the whole run: `realstubs.o`'s `.rodata.str1.4` sits at **0x801A6E08** where 0x801A6E04
was predicted, `__TEXT,__const` at **0x801AA4A8** where 0x801AA4A4 was. **And there it is absorbed**:
the pad before `__TEXT, initcode` measured **0x4** where 0xC was 377's value and 0x8 was predicted, so
`initcode` still began at **0x801AA4B0**, the three `__TEXT,__os_log` rows, `.ARM.exidx` and the raw
end still landed at **0x801AAE60**, and the text size, image, `.bss`, args page and headroom are all
exactly as written. **What decides whether a shift survives a row is `delta mod alignment`** — 0x57C
is 4 mod 8, so the 8-aligned inputs re-phase by 4 while 4-aligned ones never notice — and 374's and
376's "a shift upstream of an `ALIGN` is invisible" now has a *cause* rather than a coincidence.

**And the band written for the mergeable row resolved to its lower end.** The object's eight strings
were checked against the image the device ran and **none of them occurs in it**, so there was no
duplicate to collapse into and nothing for an earlier chunk to share: the row measured **0x1A2**,
exactly the object's own size, as argued rather than assumed. That is the first time since 301 that a
mergeable row has been *predicted* rather than banded, and it was predicted by counting duplicates.

## The layout

| | 377 measured | 378 predicted | 378 measured |
|---|---|---|---|
| counts | 750 / 652 / 98 | **748 / 650 / 98** | **748 / 650 / 98** |
| object `.text` | — | 0x8017FCF4 (0x5AC) | **0x8017FCF4 (0x5AC)** |
| platform expert `.text` | 0x8017FCF4 (0x150) | 0x801802A0 | **0x801802A0 (0x150)** |
| last kernel ctor / rtabi | 0x8017FE48 / 0x8017FE4C (0x54) | 0x801803F4 / 0x801803F8 | **both exact** |
| `realstubs.o` `.text` | 0x8017FEA0 (0x3D20 = 652 × 0x18) | 0x8018044C (0x3CF0 = 650 × 0x18) | **0x8018044C (0x3CF0)** |
| `.rodata` run start | 0x80183E6C | 0x801843E8 | **0x801843E8** |
| IOPowerConnection `.rodata` / `.str1.1` | 0x801A58C8 (0x38C) / 0x801A5C54 (0x12) | +0x57C | **0x801A5E48 / 0x801A61D4** (+0x580) |
| `kern_malloc` `.rodata.str1.1` | 0x801A5C66 (0x4CF) | 0x801A61E2 (0x4CF) | **0x801A61E6 (0x4CF)** |
| `Tests` `.rodata.str1.1` | 0x801A6135 (0x0A) | 0x801A66B1 (0x0A) | **0x801A66B5 (0x0A)** |
| object `.rodata.str1.1` | — | 0x801A66BB (0x1A2) | **0x801A66BF (0x1A2)** |
| platform expert `.rodata` / `.str1.1` | 0x801A6140 (0x440) / 0x801A6580 (0x16) | 0x801A6860 / 0x801A6CA0 | **0x801A6864 / 0x801A6CA4** |
| `.rodata.macho` | 0x801A6598 (0x14C) | 0x801A6CB8 (0x14C) | **0x801A6CBC (0x14C)** |
| `realstubs.o` `.rodata.str1.4` | 0x801A66E4 (0x36DA) | 0x801A6E04 (0x369E) | **0x801A6E08 (0x369E)** |
| `__TEXT,__const` / pad / `initcode` | 0x801A9DC0 / 0xC / 0x801A9DD0 (0x64C) | 0x801AA4A4 / 0x8 / 0x801AA4B0 | **0x801AA4A8 / 0x4 / 0x801AA4B0** |
| `.text` raw end = end | 0x801AA780 | 0x801AAE60 | **0x801AAE60** |
| text size | 1746816 | 1748576 | **1748576** |
| `.data` | 0x801AC000 (0x1A330) | 0x801AC000 (0x1A350) | **0x801AC000 (0x1A350)** |
| object `.data` / its `__DATA,__data` | — | 0x801C50F0 (0x8) / group 2 | **0x801C50F0 (0x8)** |
| `.sysctl_set` | 0x801C6330 (0x158) | 0x801C6350 (0x158) | **0x801C6350 (0x158)** |
| `.init_array` / its end | 0x801C6488 (0x90) / 0x801C6518 | 0x801C64A8 (0x90) / 0x801C6538 | **both exact** |
| `.bss` | 0x801C6540 (0x39098) | 0x801C6540 (0x39098) unmoved | **0x801C6540 (0x39098)** |
| `realstubs.o` `.bss` | 0x801FCF80 (0x2644) | unmoved | **0x801FCF80 (0x2644)** |
| `__bss_end` | 0x801FF5D8 | unmoved | **0x801FF5D8** |
| image | 1860888 (0x1C6518) | 1860920 (0x1C6538) | **1860920** |
| headroom | 2099752 | unmoved | **2099752** |
| args / topOfKernelData / tree / window | 0x80201000 / 0x80400000 / +6291456 / 8388608 | all unmoved | **all unmoved** |

**The `.text` end comes out of both routes and they agree.** Position, term by term from the rows
above: `realstubs.o`'s `.rodata.str1.4` at 0x801A6E08 with 0x369E ends at 0x801AA4A6, `fill 0x2`
(`__TEXT,__const` demands align4), `__TEXT,__const` 0x4 ending 0x801AA4AC, `fill 0x4` to the
2\*\*4-aligned `initcode` at 0x801AA4B0, `initcode` 0x64C ending 0x801AA AFC, the three
`__TEXT,__os_log` rows (+0x251 +0x3 +0x108), `.ARM.exidx` +0x8, raw end **0x801AAE60** — already
32-aligned, so `ALIGN(0x20)` costs 0x0. Delta: 377's 0x801AA780 + 0x57C (the `.text` run) + 0x166
(the `.rodata` run's content: +0x1A2 − 0x3C for the two retired name slots) + 0x7 of fills created
and re-phased + 0x2 − 0x8 (the `initcode` pad going 0xC → 0x4) = 0x801AAE60. **The pad is the term no
sum of section *contents* can represent** (375's rule), and this step is the first where it moved in
the direction *against* the shift that caused it, which is why the miss stayed invisible.

**And the falsifiers, one by one:** (a) `proc_encode_exit_exception_code` at `0x80009628` — **not
hit**, and the reason is the defect above, not a wrong step. (b) no `work_interval_thread_terminate` —
yes, gone from the stub list. (c) the counts — 748 / 650 / 98, exact to the record. (d) the layout —
exact except the phase shift, which touched no printed number. (e) the stop is earlier than (a) —
**this is the one that fired**, and it fired as the block's own falsifier (e) said it would: the run
stopped inside a frame the model could not place from the listing it had.

## The next object, named before its run

`os_reason_free`'s pool definer is **`bsd_kern_sys_reason.o`** (`bsd/kern/sys_reason.c`) and nothing
else. Measured against this image: 17 definitions, 17 references, **6 resolved (6 function, 0 storage)
/ 0 added** — `os_reason_alloc_buffer`, `os_reason_alloc_buffer_noblock`, `os_reason_create`,
`os_reason_free`, `os_reason_init`, `os_reason_ref` — so 379 takes the counts to **742 undefined, 644
function, 98 storage**. Sections: `.text` 0x378, `.bss` 0x10, `.rodata.str1.1` 0x40 and
`__DATA,__data` 0x30 — **a `.bss` input for the first time in four steps**, so the pad rule is back in
play for that bucket, and another group-2 `__DATA,__data` for the two-group `.data` arithmetic 377
found.

**And the object this block predicted for is still unvisited.** `proc_encode_exit_exception_code` is
defined by `bsd_kern_kern_exit.o` and by nothing else, and it is the first incoming step this walk has
faced that *grows* the undefined set: 128 references of which 90 are satisfied, **1 resolved / 33 added
(32 function, 1 storage)** — 748 → 783 / 650 → 684 / 98 → 99, plus a 0x40 stand-in slot for
`proc_shutdown_exitcount`. It is still on this path: `thread_terminate_self`'s `+0x1C8` is downstream
of `uthread_cleanup`'s return. The third name in sight is `throttle_lowpri_io`, the *guarded* call at
`uthread_cleanup + 0x2C`, which belongs to `bsd_miscfs_specfs_spec_vnops.o` (4 resolved, **91 added**).
Three candidates, all measured, none taken.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img` (4880384 bytes, sha256
`d8da8daaba396dff83fb45c8c2308bb0602ed2889fc1292108d82b8ef64432fa`); nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading
of either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `checksum=0x907fe5e5`,
`abort_entries=0` with `abort_first_pc=0` and `abort_first_dfar=0`, no `exception:` line. Log 301626
bytes, 3975 lines, last line `No errors detected`. The device came back to Android on its own
(`MI 4LTE`, release 10).

The step's own code is two functions of work-interval bookkeeping over storage the kernel had already
allocated, and all 21 of its references are already satisfied, so nothing new can fault at load. What
it *unlocks* is the teardown's own continuation: `thread_terminate_self` now runs past its
`work_interval_thread_terminate` call for real, and the frame that the walk then entered
(`uthread_cleanup`) is where the frontier moved. The recovery nets (`sleepGate` returning under the
hardware watchdog, and the software dead-man firing on a silent boot) are what would have caught a
stop that is not a stub; they were not needed.
