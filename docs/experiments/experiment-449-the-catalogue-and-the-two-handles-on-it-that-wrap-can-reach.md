# Experiment 449 — the catalogue, and the two handles on it that `--wrap` can reach

**Status: measured — and the step found something larger than the question it asked. Every count came
in, the catalogue parsed (branch 1 is falsified), the matching thread *was* created, and nothing was
ever matched. The four rows that say "nothing happened" together with the one row that says "the
matching thread existed" leave one explanation, and it is not in IOKit: `thread_block`'s wrapper is
**terminal** — it has never called the real function since experiment 268 — so the run halts at the
first `thread_block`, and the first `thread_block` is exactly the point at which the scheduler would
hand the CPU to the matching thread. 446-448's "the run ends in `ml_get_max_cpus`'s `thread_block`" is
"the instrument's halt is at that block". 450 retires it.**

## Why: 448 left one step, and the step has exactly two live causes

448's numbers are the starting point, and they are enough to fix where the failure is not:

| 448's key | measured | what it decides |
| --- | --- | --- |
| `xnu_entry_dtalloc_count` | `1` | `IOPlatformExpertDevice::initWithArgs` was entered — its only `IODeviceTreeAlloc` call site is inside it |
| `xnu_entry_dtalloc_arg` | `0x806e0000` | the tree the payload handed over is the one 444 moved below `topOfKernelData`, read back from the device for the first time since |
| `xnu_entry_dtalloc_ret` | `0xc0530e70` | the tree at its new home parses |
| `xnu_entry_workloop_ret` | `0xc04fa848` | `initWithArgs` returned **true** — `r5` is `IOWorkLoop::workLoop() != NULL` and `r5` is the return value (`initWithArgs+0x60..+0x7c`) |
| `xnu_entry_workloop_count` | `1` | the first of the image's two `workLoop` sites is the one on this path |

So `StartIOKit` reached `rootNub->attach(0)` and then `rootNub->registerService()`. 447's
`initmax_cpus_count = 0` says the platform expert's `start` never executed its third statement, and the
guard in front of that statement cannot fail: `IOService::start` is `{ return true; }`
(`IOService.cpp:513`), `IOPlatformExpert::configure` returns true on **every** path — source
(`IOPlatformExpert.cpp:187-213`) and disassembly (`0x8015e734`, every branch lands on `mov r0,#1` at
`0x8015e848`) — and `IODTPlatformExpert::configure` is

    if( !super::configure( provider)) return( false);
    processTopLevel( provider );
    return( true );

with `processTopLevel`'s result discarded (`IOPlatformExpert.cpp:1265` — and `0x8016049c` is this
class's `+0x358` slot, which is the slot `IOPlatformExpert::start` calls at `0x8015e724`). **`start` was
never entered.** The frontier is therefore the *match list*, and the pipeline that builds it is:

    registerService (vtable +0x160, called from StartIOKit)
      -> startMatching                       options 0, no provider -> sync == false
        -> _IOServiceJob::startJob(...,kMatchNubJob,...)      (inlined into startMatching)
          -> pingConfig -> kernel_thread_start(_IOConfigThread::main, job, &thread)
            -> [config thread] _IOConfigThread::main
              -> semaphore_wait(gJobsSemaphore), take job
                -> nub->doServiceMatch(options)
                  -> gIOCatalogue->findDrivers(this, &generation)      <- the match list
                    -> IOService::probeCandidates(matches)
                      -> OSMetaClass::allocClassWithName(name)          <- one per personality
                        -> inst->start(this)                            <- where MSM8974PlatformExpert::start would run

The table this list is built from has **two** personalities: this project's `MSM8974PlatformExpert`
(score 1616, `IONameMatch = "qcom,msm8974-xnu-stage90"`) and Apple's `IOPanicPlatform` (score 0, no
`IONameMatch`, provider `IOPlatformExpertDevice`). The fallback therefore matches *any*
`IOPlatformExpertDevice` provider, and both class names are registered before the
`last_kernel_constructor` marker runs (`.init_array` indices 24 and 36, and the marker is 37). So **an
intact catalogue cannot produce an empty match list and no panic at the same time** — it produces a
panic. No panic was measured, which leaves two live causes and no others:

  1. **the catalogue is empty.** `OSUnserialize(gIOKernelConfigTables, &errorString)` returned NULL,
     `assert` is compiled out in this configuration, and `gIOCatalogue->init(NULL)` leaves a catalogue
     that exists, matches nothing, and falls back to nothing. No match, no fallback, no panic — the
     measured state exactly.
  2. **matching never ran.** The config thread was created and never executed its `doServiceMatch` —
     in which case `findDrivers` was never called, the match list was never built, and there was
     nothing to panic about.

## What `--wrap` can and cannot reach, measured host-side before this step builds

448 named one half of the rule — a vtable slot against a symbol defined in the same link resolves
directly, so a virtual call cannot be wrapped — and this step needs the other half, because the whole
matching pipeline lives in one object file. `--wrap` renames an **undefined** reference; a reference
that resolves inside its own object is not renamed. Measured with this project's own linker:

    $ cat t2.c                       # real_a() and caller_b() in ONE translation unit
    __attribute__((noinline)) int real_a(int x) { volatile int y = x*3; return y + 1; }
    __attribute__((noinline)) int caller_b(int x) { volatile int z = x + 7; return real_a(z) * 2; }
    $ arm-none-eabi-gcc -c -O1 -o t2.o t2.c && arm-none-eabi-ld --wrap=real_a -o t2.elf t2.o
    0000801c <caller_b>:
        8030:	bl	8000 <real_a>          <- 8000 is real_a, NOT 8044 __wrap_real_a

Every entry point of the pipeline above is defined **and called** inside `IOService.cpp` —
`doServiceMatch`, `probeCandidates`, `findDrivers`, `_IOConfigThread::main`, `pingConfig` — and that
file's own references are exactly the ones this rule leaves alone. Four of those five are additionally
**virtual**: their addresses appear in the image's 24 `IOService`-derived vtables (`_ZTV5IOCPU+0x264`
and so on), so even a cross-object call to them would go through a slot. None of the five is reachable
by this instrument, and the step does not pretend otherwise.

What *is* reachable is every handle that crosses an object boundary, and the five that matter are:

| handle | defined in | called from | sites in this image |
| --- | --- | --- | --- |
| `iokit_post_constructor_init` | `IOStartIOKit.cpp` | the project's own `entry_last_kernel_constructor.o` | 1 — a `b` from `last_kernel_constructor+0x0` |
| `IOCatalogue::initialize` | `IOCatalogue.cpp` | `iokit_post_constructor_init` | 1 — `+0x18` |
| `OSUnserialize(const char *, OSString **)` | `libkern/c++/OSSerialize.cpp` | `IOCatalogue.cpp`, `IODeviceTreeSupport.cpp` | 3 |
| `OSMetaClass::allocClassWithName(OSSymbol *)` | `libkern/c++/OSMetaClass.cpp` | `IOService.cpp` | 2 |
| `IOService::publishResource(const char *, OSObject *)` | `IOService.cpp` | `IOPlatformExpert.cpp`, `IOKitBSDInit.cpp`, this project | 9 |
| `kernel_thread_start` | `bsd/kern/kern_thread.c` | `IOService.cpp`, `ioworkloop.cpp`, 22 more | 24 |

`OSUnserialize` is the one that decides branch 1, because its three sites are three different
questions and *the shape of the record* separates them:

| site | caller | what its record means |
| --- | --- | --- |
| `0x80138c44` | `IOCatalogue::initialize+0x18` | the catalogue's own parse — NULL here is branch 1 |
| `0x80162e60` | `IODTMatchNubWithKeys` | a device-tree name was compared against a serialized key list |
| `0x80162fc0` | `IODTFindMatchingEntries` | a tree entry was matched by keys |

Neither of the last two can run before something has a candidate to probe, so **a second or third
`OSUnserialize` record is itself evidence that matching reached a probe** — a handle this step gets for
free by keeping three records instead of one.

## The change

Five wrappers, plus one extension, all non-terminal — each records and then calls through:

| wrapper | what it records |
| --- | --- |
| `iokit_post_constructor_init` | caller, count |
| `IOCatalogue::initialize` | caller, count |
| `OSUnserialize` | **three** (caller, return) pairs, and a count |
| `OSMetaClass::allocClassWithName` | the first `OSSymbol *` name, and a count |
| `IOService::publishResource` | **four** (caller, key) pairs, and a count |
| `kernel_thread_start` (448's, extended) | `(continuation, caller)` for the first four calls |

Twenty `.bss` slots (`0x804f8b48 .. 0x804f8be4`, inside `.bss`, inside the window) and 22 keys printed
**outside** the dump beside 448's. Two design notes, both about not turning a reading into an
assumption:

  * **`unser` and `pub2` keep a short ring, not "the first call".** 448's wrappers kept the first
    caller, which is right when the function has one call site on the path and is an *ordering
    assumption* when it has three (or nine). The ring does not have to make the assumption: whichever
    call is the catalogue's is named by its own caller key.
  * **`kernel_thread_start` records the continuation.** 448 measured `count = 2` and `bad = 0` and
    could not say *which* two threads those were. On this boot's path there are exactly two candidates
    — `IOWorkLoop::init`'s thread (`+0x110`, `[vtable+0x3c]` = `IOWorkLoop::threadMain`) and
    `_IOServiceJob::pingConfig`'s (`+0x120`, the entry materialised as an immediate in the inlined
    `configThread`) — and the entry pointer turns the count into an identity. That is the direct
    reading of "was the async matching thread ever created", which is half of branch 2.

No `why` column in this step's table says "non-zero means failure": the only wrapper here whose
non-zero return is a failure is `kernel_thread_start`'s (`kern_return_t`, and 448 already reads it as
such). `OSUnserialize`'s non-zero return is a **parsed array**, `allocClassWithName`'s is an
**instance**, `publishResource`'s is `void`, and 448's four guard wrappers — `IORecursiveLockAlloc`,
`IOSimpleLockAlloc`, `IOCommandGate::commandGate`, `kernel_thread_start` — returned kernel-heap
pointers (`0xc052f7e0`, `0xc04eeae0`, `0xc04fa820`) for the first three, i.e. successes. For those three
it is the **count** that says they ran and the `_bad` slot that says nothing at all; 448's write-up is
corrected on this point in its own doc.

## The prediction, written before the run

Build: `STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1 ./build_entry.sh` then
`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh`. `.text` **5003264 → 5004992** (+0x6C0),
entry bin **5208596** unchanged, `.bss` `0x804f7a40 .. 0x80548ad8`, layout unchanged
(`args +5545984, topOfKernelData +7340032, tree +7208960, window 0x01000000`). Payload
`out/stage90/stage90-qcdt.img` **8228864** bytes, sha256
`c2cf13bef58575a51d92a0b0ba155d8caa7a7f0f76de60d5af3d5e70ddc30d43`. The routing is mechanical and was
checked in the image before the device was touched: `iokit_post_constructor_init` **1** (a `b` from
`last_kernel_constructor`), `IOCatalogue::initialize` **1**, `OSUnserialize` **3**,
`allocClassWithName` **2**, `publishResource` **9**, `kernel_thread_start` **24**.

Addresses the report will be read against (this image): `last_kernel_constructor` `0x80267500`,
`iokit_post_constructor_init` `0x8011bf2c`, `IOCatalogue::initialize` `0x80138c2c`,
`MSM8974PlatformExpert::start` `0x80267464` (its `publishResource` at `+0x20`, its
`ml_init_max_cpus` at `+0x28`), the `"IORTC"` literal `0x8046d690`, `_IOConfigThread::main`
`0x80132960`, `IOWorkLoop::threadMain` `0x801759f8`, `IOService::probeCandidates` `0x801313a4`.

| key | predicted | why |
| --- | --- | --- |
| `xnu_entry_postctor_count` | **1** | the `.init_array` walk reaches the marker (448 proved the constructors ran by their effects) |
| `xnu_entry_postctor_caller` | **`last_kernel_constructor+0x4`** = `0x80267504` | the only reference is the `b` at `0x80267500`; `lr` is the next instruction |
| `xnu_entry_catinit_count` / `_caller` | **1** / `0x8011bf44` = `iokit_post_constructor_init+0x18` | one call site, inside the function above |
| `xnu_entry_unser_count` | **1** (rank 1) | only the catalogue's call runs, because nothing is probed |
| `xnu_entry_unser0_caller` | **`0x80138c48`** = `IOCatalogue::initialize+0x1c` | `lr` after the `bl` at `0x80138c44` |
| `xnu_entry_unser0_ret` | **`0`** (rank 1) | branch 1: `gIOKernelConfigTables` did not parse — the only cause that explains an empty match list *and* no panic without a thread-scheduling claim |
| `xnu_entry_alloc_count` | **`0`** | nothing was instantiated by name, i.e. `probeCandidates` never ran |
| `xnu_entry_pub2_count` | **`0`** | no `publishResource` call at all, which is what "`start` was never entered" means independently of 447's count |
| `xnu_entry_kthread_count` / `_bad` | **2** / `0` | as 448; both creations succeeded |
| `xnu_entry_kthread_cont0` / `_site0` | **`0x801759f8`** / **`0x80175324`** | `IOWorkLoop::init+0x114`, the work loop's own thread — earlier in time than the config thread because `initWithArgs` precedes `registerService` |
| `xnu_entry_kthread_cont1` / `_site1` | **`0x80132960`** / **`0x8012d124`** | `_IOServiceJob::pingConfig+0x124`, i.e. the async matching thread was **created** |

**The branch, ranked, with the discriminating row named for each.**

  1. **`unser0_ret == 0`, `alloc_count == 0`.** The catalogue is empty. `gIOKernelConfigTables` is one
     of the two symbols this project's own object supplies (`stage90_platform_config_tables.c`); its
     `.data` word and its string were both verified in this image (`0x804e02a0 -> 0x804710e4`, and the
     string parses as the two-entry old-style plist this project wrote), so a NULL here is about the
     *parser* and not about the pointer: the third possibility is that `.init_array` reached the marker
     and the string is still the one 363 wrote, and the answer is in `OSUnserialize`'s own old-style
     lexer. This is the branch the whole step is built around.
  2. **`unser0_ret != 0`, `unser_count == 1`, `alloc_count == 0`.** The catalogue parsed and the match
     list was never built: `findDrivers` never ran, i.e. **the config thread was created and never
     executed `doServiceMatch`** — the fork this step's `kthread_cont1` row is there to confirm
     (`cont1 == 0x80132960` says the thread was created; it says nothing about whether it ran, and if
     it never ran the next frontier is the scheduler and the timer, not IOKit).
  3. **`unser_count >= 2` and `alloc_count > 0`.** Matching ran, a candidate was instantiated, and the
     probe ran — in which case `MSM8974PlatformExpert::start` *must* have been entered, `pub2` must show
     `0x80267488` / `0x8046d690`, and 447's `initmax_cpus_count = 0` becomes a contradiction between
     two instrumented counts that no reading of the source survives. This branch would refute 448's
     deduction rather than confirm it.
  4. **`pub2_count > 0` with `pub2N_caller == 0x80267488` and key `0x8046d690`.** `MSM8974PlatformExpert`
     reached its second statement, so `ml_init_max_cpus(1)` ran in *this* image (`+0x28` follows `+0x20`
     unconditionally) — and the block at `commpage_populate` would then be a different block from the
     one 446 measured, which the same report's `maxcpus_count`/`initmax_cpus_count` rows have to be
     re-read for.

Falsifiers of the instrument itself, named in advance: `postctor_count == 0` while `dtalloc_count == 1`
(the `.init_array` walk did not reach the marker even though later constructors' effects are visible —
the ordering defect `last_kernel_constructor` at index **37 of 70** exists for, and this wrapper is the
first direct reading of whether it matters); a `_caller` of 0 with a non-zero count (`lr` again, so the
`b`/`bl` is at `caller - 4`); `kthread_cont1` not equal to `_IOConfigThread::main` while
`kthread_count == 2` (the second thread is something else, and branch 2's "was it created" is
unanswered); and `pub2_key` values that do not resolve to the strings this image's nine sites pass
(`"IOPlatformUUID"`, `"IONVRAM"`, `"IORTC"`, `"IOBSD"`, `"boot-uuid"`, …), which would mean the pointer
recorded is not the key.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the hardware
watchdog across the jump.

## The measurement: four zeroes and one non-zero, and they leave one explanation

`.text` **5004992**, entry bin **5208596** bytes unchanged, `.bss` `0x804f7a40 .. 0x80548ad8`, layout
unchanged. Payload `out/stage90/stage90-qcdt.img` **8228864** bytes, sha256
`c2cf13bef58575a51d92a0b0ba155d8caa7a7f0f76de60d5af3d5e70ddc30d43`. The gate passed, the run wrote
**311915 bytes**, the device returned on its own, no `panic:`, no `exception:`, no `stub_hit=`, and the
`xnu_entry_abort_*` slots are all zero — so nothing faulted and nothing unwound.

```
 xnu_entry_postctor_caller=0x8011e590      <- OSRuntimeInitializeCPP+0x184, NOT the predicted 0x80267504
 xnu_entry_postctor_count=0x00000001
 xnu_entry_catinit_caller=0x8011bf44       <- iokit_post_constructor_init+0x18
 xnu_entry_catinit_count=0x00000001
 xnu_entry_unser_count=0x00000001
 xnu_entry_unser0_caller=0x80138c48        <- IOCatalogue::initialize+0x1c
 xnu_entry_unser0_ret=0xc054d9e0           <- NON-ZERO: the plist parsed
 xnu_entry_unser1_caller=0x00000000
 xnu_entry_unser2_caller=0x00000000
 xnu_entry_alloc_name=0x00000000
 xnu_entry_alloc_count=0x00000000
 xnu_entry_pub2_count=0x00000000
 xnu_entry_pub20..23_caller=0x00000000 / _key=0x00000000
 xnu_entry_kthread_count=0x00000002
 xnu_entry_kthread_bad=0x00000000
 xnu_entry_kthread_cont0=0x801759f8  _site0=0x80175324   <- IOWorkLoop::threadMain / IOWorkLoop::init+0x110
 xnu_entry_kthread_cont1=0x80132960  _site1=0x8012d124   <- _IOConfigThread::main / pingConfig+0x124
 xnu_entry_maxcpus_caller=0x800e7ad8  _count=1           <- commpage_populate+0x70
 xnu_entry_initmax_cpus_caller=0x00000000 _count=0x00000000
 xnu_entry_block_caller=0x800087e4  _continuation=0       <- ml_get_max_cpus+0x3c (ml_get_max_cpus 0x800087a8)
```

| key | predicted | measured | |
| --- | --- | --- | --- |
| `postctor_count` | 1 | `1` | ✓ |
| `postctor_caller` | `last_kernel_constructor+0x4` | `0x8011e590` = `OSRuntimeInitializeCPP+0x184` | ✗, and the miss is the finding |
| `catinit_count` / `_caller` | 1 / `iokit_post_constructor_init+0x18` | `1` / `0x8011bf44` | ✓ |
| `unser_count` | 1 | `1` | ✓ |
| `unser0_caller` | `IOCatalogue::initialize+0x1c` | `0x80138c48` | ✓ |
| `unser0_ret` | `0` (rank 1) | **`0xc054d9e0`** | ✗ — **branch 1 is falsified** |
| `alloc_count` | 0 | `0` | ✓ |
| `pub2_count` | 0 | `0` | ✓ |
| `kthread_count` / `_bad` | 2 / 0 | `2` / `0` | ✓ |
| `kthread_cont0` / `_site0` | `IOWorkLoop::threadMain` / `IOWorkLoop::init+0x114` | `0x801759f8` / `0x80175324` | ✓ (`+0x110` is the `bl`, `lr` is `+0x114`… the recorded value is the branch's own address) |
| `kthread_cont1` / `_site1` | `_IOConfigThread::main` / `pingConfig+0x124` | `0x80132960` / `0x8012d124` | ✓ |

**The one prediction that missed is worth more than the ones that hit.** `postctor_caller` is
`OSRuntimeInitializeCPP+0x184`, and `0x8011e58c` is `blx r0` — the **indirect call the `.init_array`
walk makes**. The prediction assumed `last_kernel_constructor`'s `b` would leave its own address in `lr`
the way a `bl` does. It does not: a `b` writes nothing, so the value the wrapper reads is the `lr` the
*indirect* call set, one frame up. The prediction was wrong about the mechanism and the measurement is
strictly better than it: it says the walk reached the marker's entry (`postctor_count = 1`), that the
entry is the one-instruction tail branch, and that the walker is `OSRuntimeInitializeCPP` — three
facts from one number. The defect is recorded rather than smoothed: **a `caller` key on a function
reached by a tail branch names the branch's caller, not the branch.**

### What the numbers decide

**The catalogue is not empty.** `IOCatalogue::initialize`'s own `OSUnserialize` returned `0xc054d9e0` —
a parsed object on the kernel heap — and `assert(array && …)` therefore had nothing to complain about
(and would have been compiled out anyway). Branch 1 is dead, and with it the leading explanation of the
last four experiments. The project's two-entry table parses.

**Matching never ran, and the instrument says so twice.** `unser_count = 1` means neither
`IODTMatchNubWithKeys` nor `IODTFindMatchingEntries` was called, so no candidate was ever probed;
`alloc_count = 0` means `OSMetaClass::allocClassWithName` was never called, so `probeCandidates` never
instantiated anything. Those two together are the statement *the match list was never built* — not that
it was built and came back empty, because an empty list would still have to explain the silent absence
of the `IOPanicPlatform` fallback, and this table's fallback matches any `IOPlatformExpertDevice`
provider, which the root nub is.

**And the thread that would build it exists.** `kthread_cont1 = 0x80132960` is `_IOConfigThread::main`
— the async service-matching thread, created by `_IOServiceJob::pingConfig` (whose inlined
`configThread` is the site `0x8012d124` names) — and `kthread_bad = 0` says its `kernel_thread_start`
succeeded. `kthread_cont0` is the work loop's own thread, created earlier in the same `StartIOKit` pass.
So the boot created the thread whose entire job is to do the match, and then never ran it.

**That leaves the block, and the block is not a hang.** `ml_get_max_cpus` is a *wait* for
`max_cpus_initialized`, and the only writer that can run on this boot is this project's own
experiment-405 probe in `MSM8974PlatformExpert::start` — which needs the match — which needs the
matching thread — which is dispatched by **the block itself**: `thread_block` is where the scheduler
runs the next thread, and the first `thread_block` on this boot is
`ml_get_max_cpus+0x3c`'s (`xnu_entry_block_caller = 0x800087e4`, and the 447 wrapper's
`maxcpus_count = 1` says that call is the one that blocked). The wait and its cure are the same event.

Which makes the next sentence the actual result of this experiment:

### The stop is the instrument's

`__wrap_thread_block` is **terminal**, and has been since experiment 268
(`entry_trace.c:429`):

    void __wrap_thread_block(void *continuation)
    {
        entry_epilogue_block("t268: thread_block was called",
                             (uint32_t)(uintptr_t)__builtin_return_address(0),
                             (uint32_t)(uintptr_t)continuation);
    }

It records two numbers and runs the epilogue — which writes the report, sets `RESTART_REASON`, drops
`PSHOLD` and spins in `wfe`. **It never calls `__real_thread_block`.** So the boot is not stopped by
`ml_get_max_cpus`; it is stopped *at* `ml_get_max_cpus`, one instruction before the scheduler would have
handed the CPU to `_IOConfigThread::main`. The 446/447/448 advance of the frontier "some thirty frames
behind `bsd_init`" is the instrument's own limit, and the earlier reading — 439's `stub_hit=bpf_init` at
`bsd_init+0x870`, 440's device-tree panic at `bsd_init+0x880` — is the same boot, built **without the
trace layer**. `STAGE90_ENTRY_TRACE` is what moved the frontier backwards, and it did it by refusing one
call.

Two consequences, both recorded here rather than left for the next doc to find:

  * **446's, 447's and 448's "the run ends in that `thread_block`" is "the report is dumped there".**
    The reports are honest — they are snapshots taken at the block, and everything they say about the
    instant of the block is measured — but the word "ends" was doing work nothing had measured. The
    state at the block is: the writer has not run *yet*.
  * **444's "the run is silent" has the same cause.** A run with the trace layer off has no terminal
    wrapper to dump anything, so a run that reaches a stub (`439`) or a panic (`440`) reports through
    *those* paths and a run that reaches neither reports nothing at all.

## Experiment 450, from this measurement

Retire the terminal halt: make `thread_block` non-terminal — a ring of the first eight
`(caller, continuation)` pairs in `.bss` **and** the same eight written live with `entry_write_kv`, so
the progress survives a run that never reports — and let the boot proceed. The prediction is the whole
of the chain this step measured the absence of: the block dispatches the matching thread, the match
instantiates `MSM8974PlatformExpert`, its `start` publishes `"IORTC"` and calls `ml_init_max_cpus(1)`,
the flag is set, the boot thread resumes, and the run's report comes from wherever the *next* stop is —
`bsd_init`'s first stub (436 predicted `kmstartup`, 439 measured `bpf_init`) or a later block. `249`'s
and `447`'s non-terminal wrappers are unchanged and go on counting, so the report at that next stop
carries `initmax_cpus_count >= 1` and `alloc_count >= 1` as the confirmation that the wait was cured by
the boot itself.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the hardware
watchdog across the jump. A run that no longer halts itself is still bounded by that watchdog, which is
the only net the payload leaves armed across the jump by design.
