# Experiment 448 — the platform expert's `start` is never entered, and the chain that can explain it is `StartIOKit`'s first six statements

**Status: measured — and the prediction's rank-1 branch is the one that fell. `IOPlatformExpertDevice`
came up (`IODeviceTreeAlloc` parsed the tree at its new home, `IOWorkLoop::workLoop` returned non-NULL,
so `initWithArgs` returned true and `registerService` ran), and `MSM8974PlatformExpert::start` was still
never entered: the frontier is the match list, and 449 measures the catalogue that builds it. The
measurement is at the end of this doc, with the two defects this step's own prediction table carries
(one of them a reading of the four guard slots that 449's design had to correct) recorded in place
rather than smoothed over.**

## Why: 447's zero is a deduction, not a statistic

447 measured `xnu_entry_initmax_cpus_count = 0` — the only thing that sets `max_cpus_initialized` never
ran — and the reading can be tightened into a statement about **control flow** rather than about counts,
because every link of `MSM8974PlatformExpert::start`'s guard is unconditional in the source:

    bool MSM8974PlatformExpert::start(IOService *provider)          // this project's file
    {
        if (!super::start(provider)) return false;                   // super = IODTPlatformExpert
        IOService::publishResource("IORTC");
        ml_init_max_cpus(1);
        return true;
    }

| link | source | what it is |
| --- | --- | --- |
| `IOService::start` | `IOService.cpp:513` | `{ return (true); }` — cannot fail |
| `IOPlatformExpert::start` | `IOPlatformExpert.cpp:116,184` | guard is `IOService::start`, and it ends `return configure(provider);` |
| `IOPlatformExpert::configure` | `IOPlatformExpert.cpp:187,213` | ends `return (true);` — the only `return false` is inside the loop body of `getAnyObject` |
| `IODTPlatformExpert::configure` | `IOPlatformExpert.cpp:1269` | returns false only if the above does |

So `super::start` **cannot** return false, and if `MSM8974PlatformExpert::start` is entered at all it
reaches `ml_init_max_cpus(1)`. Together with 447's zero that is a deduction: **the platform expert's
`start` was never called.** The failure is strictly before it, inside `StartIOKit`'s first six
statements (`iokit/Kernel/IOStartIOKit.cpp:120-170`):

    IOLibInit(); OSlibkernInit(); devsw_init();
    gIOProgressBackbufferKey = OSSymbol::withCStringNoCopy(...);
    gIORemoveOnReadProperties = OSSet::withObjects(...);
    interruptAccountingInit();
    rootNub = new IOPlatformExpertDevice;
    if (rootNub && rootNub->initWithArgs(p1,p2,p3,p4)) { rootNub->attach(0); ... rootNub->registerService(); }

One of those three is a virtual call — `registerService` is `virtual` — and **`--wrap` cannot reach a
vtable slot**: it renames an *undefined reference*, while a vtable entry against a symbol defined in the
same link resolves directly. The step's own prediction said `new IOPlatformExpertDevice` was a virtual
`allocClassWithName` too, and the disassembly says otherwise: `StartIOKit+0x104..+0x10c` is
`mov r0,#0x60` / `bl OSObject::operator new` / `bl IOPlatformExpertDevice::IOPlatformExpertDevice()`,
i.e. a **direct** allocation and constructor (the class is known at compile time), and the only virtual
call in the chain is `[vtable+0x340]` = `initWithArgs`. That matters beyond bookkeeping: the class
cannot fail to exist — the constructor is linked in — so `rootNub == NULL` means `kalloc` failed and
nothing else, and `allocClassWithName`'s two call sites in this image (`IOService::probeCandidates`,
`IOService::newUserClient`) are both downstream of a match. It also corrects 447's first bullet, which
took the same claim from the source's `new` and not from its code.

So this step reads the chain through the **direct** calls it contains, each of which is decisive on its
own:

  * **`IODeviceTreeAlloc` has exactly one call site in the whole image** — `initWithArgs+0x14`
    (`0x8016083c` in 447's image) — so its *count* says whether `initWithArgs` was entered at all, its
    *argument* is the tree pointer the boot actually handed over, and its *return* is whether that tree
    parsed. `initWithArgs` takes the tree branch only for a non-NULL `dtTop`; a NULL return there does
    not fail the function (`super::init()` with no tree still returns true).
  * **`IOPlatformExpertDevice::initWithArgs` returns false by exactly one path, and the disassembly
    names it**: `0x80160890` is `bl IOWorkLoop::workLoop`, then `cmp r0,#0` / `movne r5,#1` / `str r0,[r4,#84]`,
    and `r5` is the return value. So **`IOWorkLoop::workLoop`'s return value *is* the answer** to "did
    the platform expert's nub come up".
  * **`IOWorkLoop::init`** (`ioworkloop.cpp:117`) returns false at six guards, each a direct call whose
    result it tests, and four of them are wrapped here. (`OSObject::init` and `IOMalloc` are the other
    two and are far too common to wrap without drowning the report.)

## The change

Six new non-terminal wrappers, twenty `.bss` slots (`0x804f8afc .. 0x804f8b44`, inside `.bss`, inside
the window) and twenty keys printed **outside** the dump beside 447's:

| wrapper | what it records |
| --- | --- |
| `IODeviceTreeAlloc` | caller, argument, return, count |
| `IOWorkLoop::workLoop` | caller, return, count |
| `IORecursiveLockAlloc` | **first non-zero return** and the site that got it, and a count |
| `IOSimpleLockAlloc` | same |
| `IOCommandGate::commandGate(owner, action)` | same |
| `kernel_thread_start` | same |

The four guards share one rule through one helper (`entry_note_guard`), so "the first failure wins" is
written once: a later success must not erase an earlier failure, and zero is a legitimate success for
all four — so a slot of 0 **with a count of 0** ("never called") is a different reading from a slot of 0
with a non-zero count ("called, never failed").

**Correction, recorded in place after the measurement and carried into 449's design.** That rule is
right for exactly one of the four. `IORecursiveLockAlloc`, `IOSimpleLockAlloc` and
`IOCommandGate::commandGate` return a **pointer**, so their non-zero return is the allocated lock, not a
failure — the measurement below reads `0xc052f7e0`, `0xc04eeae0` and `0xc04fa820`, three kernel-heap
pointers, i.e. three successes. Only `kernel_thread_start`'s non-zero return is a failure
(`kern_return_t`). For the other three, the `_bad` slot says nothing at all and the **count** is the
reading that says the guard ran; `entry_note_guard`'s "first non-zero wins" is harmless there because it
records a success, which is what the next section has to interpret it as. 449's own table therefore has
no `why` column that claims zero-means-success or non-zero-means-failure for a pointer-returning call.

## The prediction, written before the run

Build: `STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1 ./build_entry.sh` then
`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh`. `.text` **5001888 → 5003264** (+0x560),
entry bin **5208596** unchanged, `.bss` `0x804f7a40 .. 0x80548a98`, layout unchanged
(`args +5545984, topOfKernelData +7340032, tree +7208960, window 0x01000000`). The routing is
mechanical and already checked in the image: `IODeviceTreeAlloc` **1**, `IOWorkLoop::workLoop` **2**,
`IORecursiveLockAlloc` **12**, `IOSimpleLockAlloc` **4**, `IOCommandGate::commandGate` **2**,
`kernel_thread_start` **24** call sites.

| key | predicted | why |
| --- | --- | --- |
| `xnu_entry_dtalloc_count` | **1** | one call site in the image; 1 means `initWithArgs` ran, 0 means `new IOPlatformExpertDevice` returned NULL — which no panic would report |
| `xnu_entry_dtalloc_arg` | **`0x806e0000`** | the tree's home since 444 (`ENTRY_BASE + ENTRY_DT_OFFSET = 0x80000000 + 7208960`); **the first time since 444 that the device is asked for it**, since 442 read back the *old* home |
| `xnu_entry_dtalloc_ret` | **non-zero** | 439 walked the old tree clean and the clobber that panicked 440-443 was 444's diagnosis; a NULL here would mean the tree at its new home does not parse |
| `xnu_entry_workloop_ret` | **0** (rank 1) | `initWithArgs` returns false only through this call, and the block at `commpage_populate` with no `start` requires `registerService` to have been skipped |
| `xnu_entry_workloop_count` | **1** | only `initWithArgs` is on this boot's path; a 2 would be the second call site |
| exactly one of `_rlock_bad` / `_slock_bad` / `_cgate_bad` / `_kthread_bad` | **non-zero, with its caller inside `IOWorkLoop::init`** | the four guards; their sites are fixed *offsets* from `IOWorkLoop::init`'s own symbol — `+0x50` (`IORecursiveLockAlloc`), `+0x6c` (`IOSimpleLockAlloc`), `+0xc0` (`IOCommandGate::commandGate`), `+0x110` (`kernel_thread_start`) — which is the form that survives this relink |

**The branch, ranked, and the alternatives are named rather than hidden.**

  1. **`workloop_ret == 0` and one guard refused.** Then the platform expert's nub never came up, the
     boot skipped `registerService`, nothing matched, no driver started, and `ml_get_max_cpus` waited
     for a flag whose writer is unreachable — the whole measured chain, one cause wide. The guard that
     refused names the next step, and the ranking among the four is: `kthread_bad` first (the work
     loop's own thread is the newest machinery on this boot), then `cgate_bad`, then `slock_bad`, then
     `rlock_bad`.
  2. **`dtalloc_count == 0` or `dtalloc_arg != 0x806e0000`.** Then the frontier is not the work loop at
     all but the nub's creation or the tree pointer the payload handed over — and `dtalloc_arg` would be
     the first reading of that pointer since 444 moved it, with 444's own "one value, two definitions"
     defect class as the named suspect.
  3. **`dtalloc_ret == 0`.** The tree does not parse at its new home, which would put 444's fix in
     question rather than the boot's path.
  4. **`workloop_ret != 0`.** `initWithArgs` returned true, the nub exists, and `registerService` ran —
     so the match failed *and* Apple's `IOPanicPlatform` fallback did not run either, which no reading of
     the config table survives without the catalogue being empty. That would make the constructor order
     the suspect: `last_kernel_constructor` — whose only job is to run *last* and call
     `iokit_post_constructor_init()`, which is where `IOCatalogue::initialize()` parses
     `gIOKernelConfigTables` — sits at **index 37 of 70** in this image, with 33 C++ constructors after
     it, so the personality table is parsed before most of the classes it names are registered. This
     branch is the one the build's own ledger would have to be re-read for, and it is ranked last only
     because `IOCatalogue::initialize` needs no class registration to parse a plist.

Falsifiers of the instrument itself, named in advance: the four guard counts all zero *and*
`workloop_ret == 0` (the guards are not on the failing path — `OSObject::init` or `IOMalloc` is);
`dtalloc_count == 0` *and* `workloop_count != 0` (contradictory, since `workLoop` is called from
`initWithArgs` only after `IODeviceTreeAlloc`); and a `_caller` of 0 with a non-zero count, which would
mean `__builtin_return_address(0)` was not the caller — it is `lr`, so the `bl` is at `caller - 4`.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the
hardware watchdog across the jump.

## The measurement: rank 4 came in, its stated mechanism did not

`.text` **5003264**, entry bin **5208596** bytes unchanged, `.bss` `0x804f7a40 .. 0x80548a18`, layout
unchanged. Payload **8228864** bytes. The gate passed, the run wrote **310870 bytes**, the device
returned on its own, no `panic:`, no `exception:`, and no `stub_hit=`.

```
 xnu_entry_block_caller=0x80008514        <- ml_get_max_cpus+0x3c
 xnu_entry_maxcpus_caller=0x800e77f8  _count=1     <- commpage_populate+0x70
 xnu_entry_initmax_cpus_caller=0x00000000 _count=0
 xnu_entry_dtalloc_caller=0x80160aa0  _arg=0x806e0000  _ret=0xc0530e70  _count=1   <- initWithArgs+0x14
 xnu_entry_workloop_caller=0x80160af4 _ret=0xc04fa848  _count=1
 xnu_entry_rlock_bad=0xc052f7e0  _caller=0x80125e18  _count=0x00000024
 xnu_entry_slock_bad=0xc04eeae0  _caller=0x80174fa0  _count=0x00000001
 xnu_entry_cgate_bad=0xc04fa820  _caller=0x80174ff4  _count=0x00000001
 xnu_entry_kthread_bad=0x00000000 _caller=0x00000000 _count=0x00000002
```

| key | predicted | measured | |
| --- | --- | --- | --- |
| `dtalloc_count` | 1 | `1` | ✓ |
| `dtalloc_arg` | `0x806e0000` | `0x806e0000` | ✓ |
| `dtalloc_ret` | non-zero | `0xc0530e70` | ✓ |
| `workloop_count` | 1 | `1` | ✓ |
| `workloop_ret` | **0** (rank 1) | **`0xc04fa848`** | ✗ |
| exactly one `_bad` non-zero, caller inside `IOWorkLoop::init` | one | three non-zero slots, and they are pointers; `kthread_bad = 0` with `count = 2` | ✗, as the correction above records |

**Rank 1 is falsified and rank 4 is the measured branch** — `initWithArgs` returned true, so
`StartIOKit` called `attach(0)` and `registerService()`, and the platform expert's `start` was still
never entered (447's `initmax_cpus_count = 0`). The two readings at the far end of the prediction's
list are worth naming for what they *do* establish:

  * **`dtalloc_arg = 0x806e0000` and a non-NULL return.** The device was asked for the tree pointer for
    the first time since 444 moved it, and it handed back exactly the address the payload laid out
    (`ENTRY_BASE + ENTRY_DT_OFFSET`), and that tree parsed. 444's move is good on hardware, and the
    suspicion the prediction attached to it (`dtalloc_arg != 0x806e0000`, 444's own "one value, two
    definitions" class) is dead.
  * **`kthread_count = 2`, `bad = 0`.** Two kernel threads were created and both creations succeeded.
    449 then made the count an identity — the two are `IOWorkLoop::threadMain` (the work loop's, created
    inside `initWithArgs`) and `_IOConfigThread::main` (the async service-matching thread, created by
    `_IOServiceJob::pingConfig` inside `registerService`) — so the thread whose whole job is the match
    existed at this point in the boot.

**Rank 4's stated mechanism is what this step gets wrong.** The prediction said `workloop_ret != 0`
would mean "the match failed *and* the fallback did not run, which no reading of the config table
survives without the catalogue being empty", and named the constructor order — `last_kernel_constructor`
at `.init_array` index **37 of 70** — as the suspect. 449 measured the catalogue directly:
`OSUnserialize(gIOKernelConfigTables, &errorString)` returned `0xc054d9e0` from
`IOCatalogue::initialize+0x1c`, i.e. **the plist parsed**, and `assert` had nothing to report. The parse
needs no class registration (`OSDictionary`, `OSArray`, `OSString`, `OSSymbol`, `OSNumber`, `OSBoolean`
and `OSData` are all constructed at `.init_array` indices 0-18, before the marker), so the ordering
defect is latent and not this failure. The actual explanation is 449's: **the match never had a chance
to run, because the instrument halts at the first `thread_block` — which is the very point at which the
scheduler would dispatch the matching thread.** 448's frontier statement ("the run ends in
`ml_get_max_cpus`'s `thread_block`") is therefore about the report's trigger, not about the boot.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the hardware
watchdog across the jump.
