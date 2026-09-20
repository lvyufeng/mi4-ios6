# Experiment 454 — the two thirty-second waits, and the clock that is not the problem

**Status: built, gated, run on hardware.** 453 named the primitive the boot thread was stuck in and
proved it could not have come from `_sleep`, which left one family: IOKit's two `...SleepDeadline`
entries. 454 wraps both (`TRACE_LDFLAGS` 25 -> 27) and records, per call, the site, the lock, the
event, `interType`, the `deadline` **and the counter the deadline will be compared against**. The
frontier turns out to be a **30-second `waitForMatchingService` deadline**, at the point in the boot
where the source says the root device is chosen, and the run's own numbers name which of the three
30-second waits in the image it is: `IOFindBSDRoot`'s stall for the **IOBSD resource**
(`iokit/bsddev/IOKitBSDInit.cpp:379`), reached from the inlined `setconf` inside `bsd_init`'s mount
loop (`bsd_init.c:940`). The first of the two waits is `IOKitInitializeTime`'s 30-second wait for
**IORTC** (`iokit/Kernel/IOStartIOKit.cpp:70-76`), and it *returned* — so wakes work in this boot;
what is missing is the timer. Run: 303430 bytes, **272 live records**, 33 blocks with 8 returns, 6
sleeps, **2 IOKit deadline waits**, 25 x `persistent_write_attempted=0x00000000`, 87 x
`failure_mask=0x00000000`, no `exception:`/`panic:`/`stub_hit=`, device back on its own. `.text`
5011584 -> **5013120**, entry bin 5208596, `.bss` `0x804f7a40 .. 0x80548c58`, payload `eaac0aa8...`
8228864 bytes.

## The mechanism: both remaining callers of the primitive, and both ends of the comparison

453 ended with arithmetic on the image: `lck_mtx_sleep_deadline` has exactly three callers there -
`_sleep+0x21c` (`kern_synch.c:197`), `IOLockSleepDeadline+0x20`, `IORecursiveLockSleepDeadline+0x38` -
and the boot thread reached it through none of the seven wrapped sleep entries, so it came from IOKit.
Both IOKit entries are global (`T`), both are

    int (IOLock *, void *event, AbsoluteTime deadline, UInt32 interType)

and the disassembly fixes the ABI before the wrapper is written: each stores the `{r2, r3}` pair - the
`AbsoluteTime` - to the stack for the call below and loads `interType` from `[sp, #16]` (or `[sp, #24]`,
one more saved register), so a `uint64_t` third parameter is ABI-identical to what the real function
expects. `__OSAbsoluteTime` is the identity here. Nothing changes an argument or a return value.

What the wrapper adds is the **other end of the wait**. `SLEEPNOTIFYTO` passes a deadline to
`assert_wait_deadline`, which arms the *thread's own* timer (`osfmk/kern/locks.c:892`) - and a thread
timer is delivered by a timer interrupt. So the pair `(deadline, now)` is what separates "waiting for a
timer that cannot exist" from "waiting for a publisher that never came", and it is the one argument
that says whether the wait can ever end. `now` is read in the same wrapper with one instruction,
`mrrc p15, 0, lo, hi, c14` - the counter `ml_get_timebase` reads (`0x8000f374`, whose
`mach_absolute_time` is a four-byte tail branch to it) - for the same reason 453 read the thread with
one instruction: no memory, nothing the boot has or has not initialised, and no dependence on the
thing being measured.

## The measurement, in the source's own words

    ent=1  site=_ZN9IOService22waitForMatchingServiceEP12OSDictionaryy+0xe0
           thr=init_thread+0x0  lock=0xc0471810  event=0xc1fcbe34  inter=0
           dl=0x0000000026b6429a  now=0x046132a6  dl-now=575999988  (30.000 s)
    ent=1  site=_ZN9IOService22waitForMatchingServiceEP12OSDictionaryy+0xe0
           thr=init_thread+0x0  lock=0xc0471810  event=0xc1fcbe04  inter=0
           dl=0x0000000026b9684d  now=0x04645859  dl-now=575999988  (30.000 s)

Four things are read out of this, and none of them needs a frame pointer:

1. **The site is the single `SLEEPNOTIFYTO` in `IOService::waitForMatchingService`.** The image has
   `bl __wrap_IORecursiveLockSleepDeadline` at `0x80135a50`, so the wrapper's `lr` is `0x80135a54` -
   and the run reports `0x80135a54`, i.e. `+0xe0`. Both records are `ent = 1`
   (`IORecursiveLockSleepDeadline`), which is the recursive-lock entry, and `waitForMatchingService` is
   the only one of the five call sites 453 listed that goes through it. The disassembly around it is
   the source's own text: `and r0, r6, r5 / cmn r0, #1 / beq` is `UINT64_MAX != timeout`,
   `nanoseconds_to_absolutetime`, then `clock_absolutetime_interval_to_deadline`, then the `{r0, r1}`
   pair of `gNotificationLock` (`0x8052dc6c`), `&result`, the deadline pair and `inter = 0`
   (`mov r1, #0` = `THREAD_UNINT`). The measured `inter = 0` is that instruction.

2. **The timeout is 30 seconds, and the deadline is `mach_absolute_time() + timeout`, not a caller's
   absolute time.** `dl - now` = 575999988 ticks in *both* calls although the two `now` values differ
   by 206259. A constant difference between two calls means the deadline is being *computed* from the
   same clock the wrapper reads - which is what the source says: `waitForMatchingService` takes a
   `uint64_t timeout` in nanoseconds and does `nanoseconds_to_absolutetime` +
   `clock_absolutetime_interval_to_deadline`, i.e. `mach_absolute_time() + timeout` - so `timeout` is
   30 s and the shortfall of 12 ticks (0.6 us at the rate derived below) is the instruction distance
   between XNU's read of the clock and the wrapper's.

3. **The wrapper's clock is XNU's clock, to the tick.** Between the two calls the counter advances
   206259 ticks (`0x046132a6 -> 0x04645859`) and the deadline the second caller computed advances by
   exactly the same 206259 (`0x26b6429a -> 0x26b9684d`). Two independent readings of the same clock,
   taken in two different functions, agreeing to the tick - which also measures their sum:
   `dl - now = 575999988` ticks is 30 s, so **the counter runs at 19.2 MHz, read off the device**
   rather than assumed from the SoC's crystal. And it fixes the timebase offset at 0: `ml_init_timebase`
   has never run in this image, so `mach_absolute_time` here *is* the bare counter, and `mrrc` is a
   valid substitute for it - a fact 455 and the timer work can rely on.

4. **The counter is moving, monotonically, all through the run.** Every block carries its `now`:
   `0x045ebe56` (first, t = 3.8186 s) to `0x04647682` (last, t = 3.8382 s), and the two waits sit
   inside that span. So this is not a frozen clock and not a dead CPU. What is missing is the
   *interrupt*, and the last record is 575992267 ticks - **29.9996 s** - short of the deadline that
   was still pending: the deadline did not pass and go undelivered, it never arrived. The hardware
   watchdog (`hw_watchdog_timeout_s=0x19`, 25 s, armed in the payload before the jump to XNU) returns
   the device at ~28.8 s, still ~1.2 s inside the second wait's deadline.

## Which of the 30-second waits it is: one measured integer, and a closed list from the image

The image's `bl` list for `waitForMatchingService` is six sites, and a whole-image scan for the 30 s
immediate `0x00000006FC23AC00` finds **five** places that materialize it: **two deadline waits** -
`IOFindBSDRoot+0x68` (`0x80204c14`, `movw r2,#0xac00 / mov r3,#6 / movt r2,#0xfc23`, i.e.
30000000000 ns, the timeout) and `IOSecureBSDRoot+0x40` (`0x8020586c`, the same four instructions) -
plus `memorystatus_init`'s 30 s scan interval (`0x804c6328`) and two `sfb_*` comparisons
(`0x802ab338`, `0x802ad3fc`), none of which is a wait. So the image offers exactly two candidate
30 s *waits* - and `IOSecureBSDRoot` cannot be one of the two records even before the stack arithmetic,
because it is called *after* `vfs_mountroot` succeeds (`bsd_init.c:966`) and the boot never got past
the mount loop.

The trace names the answer with one integer, and the integer is a stack address. Both records carry
the `event` - which is `&result`, the local in `waitForMatchingService`'s frame - and the two events
differ by **0x30**. `&result` is `sp + 20` in a frame of `push {r4-r8, lr}` + `sub sp, #24`, so it is
`sp_at_call - 28`, and `sp_at_call` is `bsd_init`'s `sp` minus the frames of everything between. The
two call sites are both in `bsd_init`'s own straight line - `bl IOKitInitializeTime` at `0x8003cf10`
and `bl IOFindBSDRoot` at `0x8003d050`, with no `sp` adjustment between them and none on the loop's
back edge (`b 0x8003d040` from `0x8003d0a8`) - so the difference is exactly `Σ2 - Σ1`, the sums of the
callee frames:

| the 30 s wait | chain | Σ | `&result` |
| --- | --- | --- | --- |
| `IOKitInitializeTime` (`IOStartIOKit.cpp:73`) | `IOKitInitializeTime`(16) -> `waitForService`(16) | **32** | `sp - 60` |
| `IOFindBSDRoot:379` | `IOFindBSDRoot`(80) | **80** | `sp - 108` |
| `IOSecureBSDRoot:675` | `IOSecureBSDRoot`(40) | 40 | `sp - 68` |

Only the pair `{32, 80}` differs by 48, and it is also the only pair in the source's order
(`IOKitInitializeTime` at `bsd_init+0x7c0` runs before the mount loop at `bsd_init+0x8f0`; the
`IOSecureBSDRoot` site is behind a successful `vfs_mountroot`). So **the first wait is
`IOKitInitializeTime`'s and the second, the one that never returned, is `IOFindBSDRoot`'s** - and the
first one *returned*, ~100 us later, with 30 s left on its deadline:

| # | wait | entered | block | outcome |
| --- | --- | --- | --- | --- |
| 1 | `IOKitInitializeTime`, 30 s, for the **IORTC** resource | t = 3.8270 s | 6 (`lck_mtx_sleep_deadline+0x8c`) | returned |
| 2 | `IOFindBSDRoot`, 30 s, for the **IOBSD** resource | t = 3.8378 s | 19 (same site) | never |

Wait 1 is the run's positive control for the thing this experiment is really about: **a deadline wait
in this boot *can* return - when a `thread_wakeup` arrives.** Its wrapper is entered at `now`
0x046132a6, its block is recorded 280 ticks later (0x046133be) and the next block on the same thread
is at 0x04613bcd - so the sleep lasted 2033 ticks, **106 us**, with 30 s left on its deadline, and
nothing about time ended it. The only code in the image that wakes that exact event
is `IOService::syncNotificationHandler`'s `WAKEUPNOTIFY(ref)` on the `&result` the wait is armed on
(`IOService.cpp:4660-4673`), so a matching notification was delivered. The trace carries the publish
that makes the match, **seven records earlier**: `MSM8974PlatformExpert::start` is a five-instruction
tail after its `super::start` - `movw/movt r0, #0x8046f668` (the pooled `"IORTC"` string),
`mov r1, #0`, `bl __wrap_IOService::publishResource(const char *, OSObject *)`,
`mov r0, #1`, `bl __wrap_ml_init_max_cpus` (`0x80268388-0x802683b4`) - and both of those calls are in
the run's records (`xnu_live_pub2_key = 0x8046f668`, `xnu_live_initmax_cpus_caller = 0x802683b0`).
So notifications, `gNotificationLock`, `thread_wakeup` and the wakeup path all work in this image.
What does not exist is a path from a *deadline* to a running thread.

## What the two waits are, in the boot's own sequence

Both waits are inside `bsd_init`, and the trace's tail follows that function's straight line, in
order. `MSM8974PlatformExpert::start` publishes `"IORTC"` (`0x80268398`) and calls `ml_init_max_cpus(1)`
(`0x802683b0`, the caller 452 recorded) early in the run; then `IOKitInitializeTime` waits for it
(`bsd_init+0x7c0`) and is woken; then the inlined `bsd_autoconf` tail runs `IOKitBSDInit()`
(`bsd_init+0x880`) -> `IOService::publishResource("IOBSD")` -> the `xnu_live_pub2_key = "IOBSD"`
record, `loopattach`/`dlil_init`/`net_init_run` start the network threads (`nwk_wq`, `pf_purge`,
`flowadv`, `dlil_main_input`, `ifnet_detacher` - the last five `kernel_thread_start`s before wait 2),
`inittodr`, and then the mount loop: `setconf()` (inlined) -> `bl IOFindBSDRoot` at `0x8003d050` ->
`serviceMatching(gIOResourcesKey)` + `setObject(IOResourceMatched, "IOBSD")` ->
`waitForMatchingService(matching, 30ULL * kSecondScale)` -> **sleep 2, which never returns**.

And a negative result inside that: the IOBSD resource was published *before* the wait, `bsd_init`'s
straight line orders `IOKitBSDInit()` (`0x8003cfd0`) ahead of the mount loop (`0x8003d040`), and the
trace's records are in that order too. `waitForMatchingService` calls `copyExistingServices` first and
only sleeps when it finds nothing - so **the entry into sleep 2 is itself the measurement that the
published resource does not satisfy `IOFindBSDRoot`'s dictionary**. The two spellings are in the
source and are not the same property: `IOKitBSDInit()` sets `gIOResources.IOBSD` (`publishResource`
with a NULL value substitutes `gIOServiceKey`), while the dictionary is
`serviceMatching("IOResources")` with `IOResourceMatched = "IOBSD"`, whose matcher
(`IOService.cpp:5107`) looks for an *array* `IOResourceMatched` on the candidate and asks whether
`"IOBSD"` is in it. Which of the two halves is the reason is 455's question, not this step's - but the
*direction* is settled: the registry is where this frontier lives, not the sleep primitive.

## What the next step measures

The step above is a strategy result, not just a name. `IOFindBSDRoot`'s two waits are the only way to
the root device, and neither of them can end by time in this image:

  * the 30 s deadline is delivered by `assert_wait_deadline`'s **thread timer**, which needs a timer
    interrupt, and this boot has no timer driver - so the wait can only end by `thread_wakeup`, i.e.
    by a service that matches. Wait 1 proves the wakeup path works; wait 2 proves nothing publishes a
    match;
  * and even with a timer, the deadline is 30 s while the watchdog returns the device at 25 s - so
    **the timer work item and this frontier are the same work item**, and the interrupt must land
    inside the run's window for the boot to advance. The rate the deadline is counted in is now
    measured (19.2 MHz, above), which is exactly the number the `tbd_ops_t` over the GPT at
    `0xf9020000` (IRQ 19) has to be configured with.

So 455 has two cheap, independent probes to choose between, and both are wrappable (`T`) or already
wrapped: `IOService::serviceMatching` / `copyExistingServices` / `registerService` on the registry
side (which dictionary, which candidate, which property), or `IOCatalogueMatchingDriversPresent` /
`OSMetaClass::allocClassWithName`, which is how a kext would appear. The instrument does not need a new
mechanism for either - 454's `(site, lock, event, inter, deadline, now)` record is enough to say "this
is still the same wait".

## Corrections this step owes 453

  * 453's commit message ended with **"and the two 30 s sites plus the absence of any `IOSleep` (which
    would show as a wrapped `msleep0`) identify the caller family as `IOFindBSDRoot`"**. The absence
    proves nothing: `IOSleep` is `delay_for_interval(milliseconds, kMillisecondScale)`
    (`iokit/Kernel/IOLib.cpp:1118-1120`), a busy-wait that never enters the sleep family, so an
    `IOSleep` in that path would have been **invisible** to all seven wrappers. (Recorded as defect
    164.)
  * The same message's chain was `setconf` <- `vfs_mountroot` <- `bsd_init:946`. `setconf` is called
    from `bsd_init` itself (`bsd_init.c:940`) and is inlined into it; `vfs_mountroot` is a *sibling*
    call one statement later (`:946`), and `vfs_mountroot` never calls `setconf` (`vfs_subr.c:1040-1075`).
  * The 30 s site list was itself incomplete for a while: the image scan for the immediate
    `0x6FC23AC00` cannot see `IOKitInitializeTime`'s 30 s, because it is `t.tv_sec = 30` and the
    nanoseconds are computed by `waitForService`'s `umlal` against `0x3b9aca00` at run time. The
    closed list had to be built from the source *and* checked against the image. (Recorded as defect
    163.) `ROOTDEVICETIMEOUT`, for reference, is **60** in this configuration (not 120: `DEBUG` is
    off) - and that is the timeout of the `waitForService` loop `IOFindBSDRoot` would have reached
    *after* the wait that never returned.

Safety, unchanged and re-measured on every run: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed; the device returned to
Android on its own; 25 x `persistent_write_attempted=0x00000000`, 87 x `failure_mask=0x00000000`, no
`exception:` line. The trace is read-only with respect to the boot: both wrappers record and call
through, and the two blocks they add to the record are the two the source says the boot takes.
