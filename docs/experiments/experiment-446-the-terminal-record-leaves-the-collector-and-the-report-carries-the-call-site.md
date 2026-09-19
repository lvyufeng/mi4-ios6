# Experiment 446 — the terminal record leaves the collector, and the report carries the call site

**Status: measured. The terminal record left the collector and the report carries the call site — three
of the four mechanical rows came in exact, and the branch missed by one degree: the wait is
`ml_get_max_cpus`, and the thing that would have woken it is this project's own experiment-405 probe in
the platform expert. The miss is recorded as a miss, and it decides more than a site.**

## Why: 445 named the stop and could not name its site

445's report is `real XNU entry: t268: thread_block was called` — the tracer's terminal wrapper running
the epilogue in place of the block, which is what it was built for. And the two records that would have
said *where from* are the two the report does not have:
`t268_thread_block_caller`/`_continuation` were written with `entry_kv` as the run's **last** act, into
a collector that had already refused **16269** records (`xnu_entry_kv_dropped=0x3f8d` against
`xnu_entry_kv_written=0x1fea`). A drop counter makes a refusal visible — that was 269's repair and it
worked — and nothing made the one record that decides the reading survive a collector that keeps the
oldest. The instrument's answer is the newest thing a run produces.

## The change, and it is the pattern the file already has

`entry_stubs.c` already keeps records a full buffer cannot be trusted with: the data-abort handler's
`g_abort_entries` and `g_first_abort_*` live in `.bss` and are written by the epilogue **outside** the
dump (`entry_stubs.c:1067-1080`), for the stated reason that "the teardown's set/way sweep covers the
whole D-cache" and because the report has to survive the fault it is reporting on. The same three lines
now carry the block:

- `entry_stubs.c`, under `#ifdef STAGE90_ENTRY_TRACE`: five `.bss` slots — `g_block_caller`,
  `g_block_continuation`, `g_block_kv_len`, `g_vmwait_caller`, `g_vmwait_count` — plus the two entry
  points the tracer calls: `entry_epilogue_block(why, caller, continuation)`, which stores the two
  numbers and runs the same epilogue every other report runs, and `entry_note_vmwait(caller)`, which
  keeps the **last** `vm_page_wait` caller and a count.
- `entry_stubs.c`'s report block prints the five keys beside the abort keys — **outside** the dump.
- `entry_trace.c`: `__wrap_thread_block` passes its `lr` and its `continuation` argument to
  `entry_epilogue_block` instead of recording them; `__wrap_vm_page_wait` calls `entry_note_vmwait`
  and still records into the buffer, because it is not terminal and its record is useful when there is
  room.
- `build_entry.sh` passes `-DSTAGE90_ENTRY_TRACE=1` to **`entry_stubs.c`'s** compile as well as using it
  for the link, so the slots and the keys exist only in a traced image. That is deliberate: the
  *presence* of `xnu_entry_block_caller` is then a statement about the image, and a value of `0` means
  the wrapper never ran — rather than a zero that could mean either.

## The prediction, written before the run

The build is done and no device has been touched. `.text` **5001344 → 5001536** (+192), entry bin still
**5208596** bytes, `.bss` unmoved at `0x804f7a40 .. 0x80548a18`, layout unchanged (`args +5545984,
topOfKernelData +7340032, tree +7208960, window 0x01000000`), XNU's two writes still
`0x80548a08`/`0x80548a0c`. The new symbols: `entry_epilogue_block` `0x80002e7c`, `entry_note_vmwait`
`0x80002ea0`, the five slots `0x804f8ad4 .. 0x804f8ae4` (inside `.bss`, inside the window),
`__wrap_thread_block` `0x80453b7c` (108 call sites), `__wrap_vm_page_wait` `0x80453b38` (19), and the
wrapper's `why` literal at `0x804c2ff0`.

| key | predicted | why |
| --- | --- | --- |
| `xnu_entry_block_caller` | **present and non-zero** | the wrapper runs, and it now passes `lr` as an argument; `caller - 4` is the `bl __wrap_thread_block` that reached it |
| `xnu_entry_block_continuation` | **0**, most likely | `thread_block`'s argument is `THREAD_CONTINUE_NULL` for the `assert_wait`-style waits that make up most of the 108 sites; a non-zero value is itself a reading — a function the thread meant to resume into |
| `xnu_entry_block_kv_len` | **`0x1fea`** (8170) | the buffer is saturated long before the block, so the position the refused records would have had *is* the buffer's fill, the number 445 reported as `kv_written`; a **smaller** value would be the interesting result, because it would mean the block happened before the buffer filled and 445's window is deeper than it looks |
| `xnu_entry_vmwait_count` | **`0x00000000`** | `vm_page_wait` was never called while the buffer was live, every `kernel_memory_allocate` in the window returned 0, and free pages read 1962; **a non-zero count is the falsifier for the branch below**, because `zalloc_internal`'s shortage path calls `vm_page_wait` *before* it reaches `thread_block`, so a memory-pressure block cannot arrive with a zero count |

**The branch, ranked, and the alternatives are named rather than hidden.** The caller should resolve to
a **wait whose wake-up needs something this machine does not have** — a deadline (`IOService`,
`lck_*_sleep_deadline`, `thread_call_*_wait`), an entropy source (`read_random`), or a console
(`serial_keyboard_poll`) — and **not** to the allocator (`vm_page_wait`, `zalloc_internal`,
`zone_replenish_thread`), which the fourth row above falsifies independently. The strongest single
candidate is the IOKit state/arbitration family — `IOService::waitForState`, `IOService::attach`,
`IOService::lockForArbitration`, `IOService::startMatching` — because IOKit bring-up is on this boot's
path by 420-430's own findings (the platform expert publishes IORTC, the boot thread reaches `ubc_init`,
`IOServicePublishResource` is named) and a state wait with a *deadline* cannot expire without a timebase.
`read_random` is the second: it blocks until the PRNG has entropy, and on a machine where **no interrupt
is ever delivered** nothing ever adds any.

The falsifiable core is the four rows and the arithmetic behind them; the branch is a rank, and a miss
here is a reading, recorded as one.

## The measurement: the block is `ml_get_max_cpus`, and the step it condemns is the platform expert's

`.text` **5001344 → 5001536** (+0xc0 = 192), entry bin **5208596** bytes unchanged, `.bss` unmoved at
`0x804f7a40 .. 0x80548a18`, layout unchanged. Payload `out/stage90/stage90-qcdt.img` **8228864** bytes,
sha256 `9f3b23848a3212b2dbdb31e0e9e35535d80150546da854dc4f77e87694c06ddd`. The gate passed and
`LOGFILE=/tmp/cancro-446-last_kmsg.txt stages/stage90/run_and_capture.sh --allow-xnu-entry` wrote
**310014 bytes / 4259 lines**; the device returned on its own; no `exception:`, no `panic:`, no
`stub_hit=` anywhere in it. The report, at log lines 3932–3973, with the new keys printed **outside**
the dump the way the abort slots already are:

```
MI4IOS6_STAGE90_XNU real XNU entry: t268: thread_block was called
 xnu_entry_kv_written=0x00001fea
 xnu_entry_kv_dropped=0x00003f8b
 xnu_entry_block_caller=0x80008224
 xnu_entry_block_continuation=0x00000000
 xnu_entry_block_kv_len=0x00001fea
 xnu_entry_vmwait_caller=0x00000000
 xnu_entry_vmwait_count=0x00000000
```

| predicted row | predicted | measured | |
| --- | --- | --- | --- |
| `xnu_entry_block_caller` | present and non-zero | `0x80008224` | ✓ |
| `xnu_entry_block_continuation` | `0` | `0x00000000` | ✓ |
| `xnu_entry_block_kv_len` | `0x1fea` | `0x00001fea` | ✓ |
| `xnu_entry_vmwait_count` | `0` | `0x00000000` | ✓ |
| **the branch** | the IOKit state/arbitration family, or `read_random` | **`ml_get_max_cpus`** | ✗ |

**The collector fix is visible in its own counter, which is the arithmetic the 445 doc asked for.**
`xnu_entry_kv_dropped` fell from `0x3f8d` (16269) to `0x3f8b` (**16267**) while `xnu_entry_kv_written`
did not move at all (`0x1fea` in both runs): exactly **two** records left the buffer, and they are the
two the terminal wrapper used to write as the run's last act. The defect 445 named is measured on both
sides — a record written last is the first a keep-the-oldest buffer refuses, and these two no longer
depend on the buffer at all.

### `0x80008224`, read off the image the device ran

It is **`ml_get_max_cpus+0x3c`**, so the `bl` is at `+0x38` and it is the call the linker routed to the
wrapper (`__wrap_thread_block` `0x80453b7c`):

```
800081e8 <ml_get_max_cpus>:
800081ec: mov r0,#0 ... bl ml_set_interrupts_enabled
800081f8: movw r0,#0xbe88 / movt r0,#0x804f   ; r0 = 0x804fbe88 = &max_cpus_initialized
80008200: ldr r1,[r0]
80008204: cmp r1, #1                          ; MAX_CPUS_SET
80008208: beq 80008224 <ml_get_max_cpus+0x3c> ; already set: no wait
8000820c: mov r1, #2                          ; MAX_CPUS_WAIT
80008210: str r1,[r0]
80008214: mov r1, #0
80008218: bl  assert_wait
8000821c: mov r0, #0
80008220: bl  80453b7c <__wrap_thread_block>  ; <- the block; lr = 0x80008224
80008224: mov r0, r4 ... bl ml_set_interrupts_enabled
80008234: ldr r0,[r0,#8]                      ; return machine_info.max_cpus
```

and the source it is a rendering of is `osfmk/arm/machine_routines.c:184-196`:

```c
current_state = ml_set_interrupts_enabled(FALSE);
if (max_cpus_initialized != MAX_CPUS_SET) {
        max_cpus_initialized = MAX_CPUS_WAIT;
        assert_wait((event_t) & max_cpus_initialized, THREAD_UNINT);
        (void) thread_block(THREAD_CONTINUE_NULL);
}
(void) ml_set_interrupts_enabled(current_state);
return (machine_info.max_cpus);
```

Three constants agree between the two readings and none of them was assumed: `cmp #1` is
`MAX_CPUS_SET`, `str #2` is `MAX_CPUS_WAIT`, and the `bne +0x58` branch to `thread_wakeup_prim` at
`ml_init_max_cpus+0x54` is the source's `if (max_cpus_initialized == MAX_CPUS_WAIT) thread_wakeup(...)`.
Both functions address `0x804fbe88`, which `nm` names `b max_cpus_initialized` — **one value with one
definition**, which is the check this project's most repeated defect class would have failed.

### The miss, and it is one degree wide

The prediction's *shape* was right — a wait whose wake-up needs something this machine does not have.
The *member* was wrong, and the miss is worth more than the hits because of what it lands on. The image
has exactly **two** `bl ml_init_max_cpus` sites and **six** `bl ml_get_max_cpus` sites, both sets
resolved against the image:

| writer — `bl ml_init_max_cpus` (`0x8000817c`) | site | file |
| --- | --- | --- |
| `IOCPUInterruptController::initCPUInterruptController(int,int)+0x94` | `0x80124638` | `iokit/Kernel/IOCPU.cpp:765` |
| `MSM8974PlatformExpert::start(IOService*)+0x28` | `0x80266ecc` | **this project's own file** |

| caller — `bl ml_get_max_cpus` (`0x800081e8`) | site | file |
| --- | --- | --- |
| `vm_page_init_local_q` | `0x80019f38` | `osfmk/vm/vm_resident.c:614` |
| `waitq_alloc_prepost_reservation` | `0x800aaccc` | `osfmk/kern/waitq.c:741` |
| `commpage_populate` | `0x800e7514` | `osfmk/arm/commpage/commpage.c:222`, via `commpage_cpus` |
| `mcache_init` | `0x801d104c` | `bsd/kern/mcache.c:191` |
| `mbinit` | `0x801d4304` | `bsd/kern/uipc_mbuf.c:1585` |
| `sysctl_mib_init` | `0x8022c5cc` | `bsd/kern/kern_mib.c:602` |

**The prediction named the right subsystem and the wrong function inside it.** The first-ranked family
was "the IOKit state/arbitration family — `IOService::waitForState`, `attach`, `lockForArbitration`,
`startMatching` … because IOKit bring-up is on this boot's path by 420-430's own findings". The measured
wait is one frame *above* that family's own writer: `ml_get_max_cpus` waits for a fact that only
`IOCPUInterruptController::initCPUInterruptController` — or this project's platform expert — ever sets,
and the branch that would have been right is "the wait whose **waker** is an IOKit object this image
never instantiates", not "a wait inside IOKit". `read_random`, the second-ranked candidate, is not on
the path at all. The fourth row's falsifier for the allocator family (a zero `vm_page_wait` count) was
never needed, because the caller was never in that family.

### The number condemns a step, not a site

Whichever of the six callers it was, the block happened because `max_cpus_initialized != MAX_CPUS_SET`,
and on this machine the only writer of that flag that can run is `MSM8974PlatformExpert::start+0x28` —
**experiment 405's own probe, in this project's own file** (`stages/stage90/xnu_platform/
MSM8974PlatformExpert.cpp`, whose third statement is `ml_init_max_cpus(1)`, after
`if (!super::start(provider)) return false;` and after `IOService::publishResource("IORTC")`). 405's
measurement is that this call cured *this exact block* and moved the frontier 23 frames to
`bsd_init+0x8`; 439's and 440's runs are further past it still. So this run says something 446 was not
built to ask: **the platform expert's `start` did not reach its third statement in this image.** Either
it is no longer reached — the nub path in `StartIOKit`, which `PE_init_iokit` calls at `0x80005298`,
before `vm_shared_region_init` (`osfmk/kern/startup.c:545` and `:576`) — or `IOPlatformExpert::start`
returned false and the `super::start` guard ended it early. The `IORTC` publish is in the same function,
so a second reading follows from the same number: if `start` is not running, `IOKitInitializeTime`'s
`waitForService("IORTC", 30 s)` — 419's thirty-second wait, which 420 armed this same `start` to clear —
is unarmed as well.

## What this run cannot say, and the correction 445 owes

The run names the frame and the condition. **It does not name the site**, and 445's sentence "the boot
passed that point [`bpf_init`] and reached a block after at least 16269 further allocation records" does
not survive reading: `bpf_init`'s absence from this report is not evidence about where the boot got to,
because 439/440 made it real — the manifest now carries `bsd/net/bpf.c`, and the stop that named it was
`bsd_init+0x86c`'s `blx r1` into the `pseudo_inits` array, not a stub of its own. What the absence
proves is that entry 4 of that array is no longer a stub, and nothing about distance. The record count
is the other direction: 16267 refused records is 16547 writes, and that is a great deal of allocation
for a boot that stopped before `bsd_init`'s first statement. **The two readings cannot both be leaned
on, and this run does not choose between them** — a terminal wrapper reports the block, not the
distance to it.

Naming the site is one non-terminal wrapper away and needs no new mechanism: `ml_get_max_cpus` *returns*
once its flag is set, so a wrapper can record its caller into a `.bss` slot the epilogue already prints
outside the buffer and then call through — the same shape 446 used for `vm_page_wait`, pointed at
`ml_get_max_cpus` itself. Wrapping `ml_init_max_cpus` beside it turns that same run into both readings
above: *did the probe run at all*, and *from which of its two sites*. That run is 447.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the
hardware watchdog across the jump, because the payload's GIC and vector state are gone the moment
`_start` switches tables.
