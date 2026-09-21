# Experiment 495 — the OS calls a driver back

Date: 2026-09-21
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured twice
Artifacts: `stages/stage90/xnu_platform/MSM8974Timer.cpp`, `tools/check_driver_catalogue.py`

**Result: the boot now needs a driver.** 492–494 made a driver *read* its device, first as a number out
of a memory descriptor and then as a word out of the register the OS's own mapping reaches; none of it
made the machine do anything the driver asked. 495 asks the OS for a timeout - `IOWorkLoop::workLoop()`,
`IOTimerEventSource::timerEventSource( this, msm8974_timer_timeout )`, `addEventSource`, `enable()`,
`setTimeout( 100 ms )` - and the kernel calls the driver back through four objects it created for it,
three times, on the thread-call daemon, with the work loop's gate held:

    run 2, `MSM8974Timer::start`           the arming
    _ms          = 0x64   = 100 ms         the interval, from one `#define`
    _wl          = 0xc050f640              a kernel thread exists because this driver asked for one
    _ts          = 0xc0543e40              the timer event source
    _add         = 0                       addEventSource -> kIOReturnSuccess
    _en          = 1                       the source reported itself enabled after `enable()`
    _to          = 0                       setTimeout -> kIOReturnSuccess
    _arm_lo      = 0x06de06b6              mach_absolute_time() immediately before the arming
    _due_lo      = 0x06fb52df              the OS's own clock_interval_to_deadline( 100 ms )
    _due - _arm  = 1 920 041 ticks = 100.002 ms

    the callback, called by the kernel    fire 1       fire 2       fire 3
    _fires                               = 1            2            3
    _fire_lat_ns                         = 101.101 ms   101.041 ms   101.042 ms   (arm -> this fire)
    _fire_lo                             = 0x06fba541   0x07193fd6   0x0736da84
    _gap_ns                              = -            101.048 ms   101.049 ms   (previous fire -> this)
    _rearms                              = 1            2            -            re-armed from inside
    _rearm_rc                            = 0            0            -            the re-arm was accepted

**The falsifier the step was built around fires in the affirmative, and it is a second, independent
record of the same arming.** The entry image already wraps the old `timer_call_*` family, and
`thread_call_enter_delayed_with_leeway` reaches `timer_call_enter_with_leeway` from
`_arm_delayed_call_timer` (`thread_call.c:618`, flags `TIMER_CALL_SYS_CRITICAL|TIMER_CALL_LEEWAY` = the
`0x21` the wrapper recorded). So the kernel's own instrumentation logged the deadline `setTimeout`
armed, and it is **43 ticks - 2.2 µs - from the `_due` this driver computed for itself**:

    xnu_live_tmr_enter_seq       = 0x34        the 52nd of the 64 the record keeps in full
    xnu_live_tmr_enter_src       = 2           timer_call_enter_with_leeway, not the quantum timer
    xnu_live_tmr_enter_call      = 0x8051f320  &group->delayed_timers[TCF_ABSOLUTE]
    xnu_live_tmr_enter_flags     = 0x00000021  SYS_CRITICAL | LEEWAY, exactly `:618`'s pair
    xnu_live_tmr_enter_now       = 0x06de07af  the driver's `_arm_lo` + 249 ticks = +13.0 µs
    xnu_live_tmr_enter_deadline  = 0x06fb530a  the driver's `_due` + 43 ticks = +2.2 µs
    xnu_live_tmr_enter_delta     = 1 919 835 ticks = 99.991 ms

Three readings agree that this is this driver's arming and not a coincidence: it is the record
immediately after `workloop_ret = 0xc050f640` (the `_wl` this driver published), its `now` is 13.0 µs
after the driver's own `mach_absolute_time()` - the cost of `setTimeout`'s conversion plus the walk to
the timer - and its deadline is 2.2 µs from the driver's `_due`, which is the separation of the *two*
`clock_interval_to_deadline` calls. The fire then lands **1.096 ms after the deadline the OS actually
armed** (1.098 ms after `_due`).

Both runs are through the gate, both exit 0, both end `No errors detected` with the device back on
Android on its own. The boot's console frontier did **not** move: the OS console block is byte-identical
to 494's (1288 characters, sha256 `a0593af0…`), the fault counters are 493's and 494's
(`_seen = 0x20`, `_armed = 0x1c`, `_redirected = 0x1a`, no `stub_hit`, no panic), and every one of 494's
device keys reads the same value it did (`_hwok = 1`, `_maptyper = _probetyper = 0x468`,
`_rd0 = 0x00000000`, `_resolve = 1` on both nodes) - the register word is read through a mapping at yet
another VA (`0xc2094000`/`0xc2095000` here). Nothing here claims the boot advanced.

**Run 1 kept because it found the step's real defect, in the driver's own record.** `_fire_lat_ns`
was computed against a timestamp written once, before the first arming, and never moved by the
callback's re-arm - so the key was a *sum* that grows by one interval per fire: fire 1 `101.105 ms`,
fire 2 `202.151 ms`, fire 3 `303.199 ms`, three numbers under a name that says "how long the OS took to
call this driver back". The run's own `_gap_ns` had the same shape. The fix is one line - the callback
takes a fresh `mach_absolute_time()` immediately before each re-arm, exactly as `start` reads its own
arming instant - and run 2's column is what the key was supposed to be: one measurement repeated three
times, 101.101 / 101.041 / 101.042 ms. **A key named for a latency that holds a sum of intervals is
this project's oldest defect class in a new place: the record was not wrong about the machine, it was
wrong about itself.**

## The chain, and where each link is read out of Apple's file

Nothing in this sequence is this image's own code: it is five calls into IOKit and then the kernel
coming back. That is why each link is read out of the tree rather than assumed to work, and why the
check refuses a file that has any of them in the wrong order.

  * **`IOWorkLoop::workLoop()` is a kernel thread.** `IOWorkLoop::init` (`IOWorkLoop.cpp:117-176`)
    ends in `kernel_thread_start( cptr, this, &workThread )` at `:170`, with
    `OSMemberFunctionCast( thread_continue_t, this, &IOWorkLoop::threadMain )` as the body. So `_wl`
    non-zero is not a bookkeeping object: it is the reason a thread exists. The run's own
    instrumentation says so without being asked - `xnu_live_workloop_ret` appears **twice** in 494's
    log and **three times** in 495's, and each is preceded by the same `kthread_cont = 0x8018b208` /
    `kthread_ret = 0` pair the OS's own two work loops use. The third one is this driver's, and its
    value is `_wl`.
  * **The event source is built on that work loop and handed this file's function.**
    `timerEventSource( OSObject *, Action )` (`IOTimerEventSource.cpp:308-312`) forwards to the
    `PriorityKernelHigh` form (`:293-306`, `kIOTimerEventSourceOptionsDefault`, header `:83`), and
    `setTimeoutFunc` (`:205-270`) selects `&IOTimerEventSource::timeoutAndRelease` as the callout for
    that priority and allocates it with `thread_call_allocate_with_options` at `:268`. `_ts` is the
    source; `addEventSource` is what gives it its `workLoop`, and `wakeAtTime` arms nothing without one
    (`:478`). `_add = 0` is that call's `kIOReturnSuccess`.
  * **`enable()` before `setTimeout` is not style.** A disabled source stores the time and enters
    nothing (`:474-476`), so `_en = 1` published beside `_to = 0` is the pair that separates "there is a
    deadline" from "there is a number".
  * **`setTimeout( interval, scale )` is the OS's own conversion, in the driver's own words.**
    `setTimeout(UInt32, UInt32)` (`:381-387`) is `clock_interval_to_deadline( interval, scale, &end )`
    followed by `wakeAtTime( end )` - *the same function* this driver calls separately to publish
    `_due_*`. That is what makes `_due` the moment the callback should have run rather than a derived
    guess, and the run's 43-tick agreement between the two conversions is what says both readings are
    of one deadline.
  * **The call is entered as a thread call.** `wakeAtTime( uint32_t, AbsoluteTime, AbsoluteTime )`
    (`:470-499`) calls `thread_call_enter_delayed_with_leeway( calloutEntry, ... )` at `:492`, retaining
    the source and the work loop first when the flag is `kPassive` (`:485-488`, which is where
    `PriorityKernelHigh` lands at `:262-268`). `calloutEntry` is the `thread_call_t` allocated in
    `setTimeoutFunc`, and `_arm_delayed_call_timer` (`thread_call.c:595-623`) is what turns it into a
    `timer_call` on the group's own timer - the enter the run's log carries.
  * **The callback runs on the thread-call daemon with the work loop's gate held.**
    `timeoutAndRelease` (`IOTimerEventSource.cpp:146-178`) is the callout: it takes `wl->closeGate()`,
    invokes the action, opens the gate again and releases the two retains. The daemon itself is started
    by `thread_call_initialize()` from `kernel_bootstrap_thread` (`startup.c:445-446`), and the timer
    queue is expired by `rtclock_intr`, which this image's own IRQ handler calls
    (`entry_irq.c:204` on the line `STAGE90_GIC_TIMER_INTID`). No key in the log names that thread -
    the `xnu_live_tmr_enter_*` records are the *old* `timer_call_*` family, and the `xnu_live_tmr_qexp_*`
    records are the scheduler's quantum timer - so this link is a reading of Apple's file, and what the
    run adds is that it happened.
  * **Re-arming from inside the callback is legal, and the run says so.** The gate is already held when
    the action is invoked, so a driver that calls `setTimeout` from its own timeout is doing what a real
    driver does; `_rearms = 2` with `_rearm_rc = 0` twice is the reading that the call was accepted from
    there. A work loop whose gate deadlocked would have shown `_fires = 1` and no `_rearms` at all.

**`_to = 0` is not the arming, and the record says which is.** `wakeAtTime` has exactly one early
return - `if( !action ) return kIOReturnNoResources` (`:472`) - and returns `kIOReturnSuccess` on every
path after it, *including* the path where `enabled`, the absolute time or the work loop was not there
and no thread call was entered. So a driver that published only its return codes would report an
accepted deadline for a timeout that does not exist; what makes this step's record a reading is the
callback's own keys, which the check requires: `_fires` (published as 0 before the arming, so "armed and
never called" is a value and not an absence), `_fire_lat_ns`, and the re-arm.

## The dispatch latency, and what the interval was held against

Three conversions, one clock (19.2 MHz: `_cntfrq = 0x0124f800`):

| measured | ticks | time |
| --- | --- | --- |
| `_due - _arm`, the OS's own conversion of the 100 ms asked for | 1 920 041 | 100.002 ms |
| the driver's `_due` to the deadline `setTimeout` armed | 43 | 2.2 µs |
| `_arm` to the enter inside `setTimeout` (`_arm_lo` to `tmr_enter_now`) | 249 | 13.0 µs |
| the OS's armed deadline to fire 1 | 21 047 | 1.096 ms |
| the asked-for 100 ms to fire 1 / 2 / 3 (`_fire_lat_ns`) | - | 101.101 / 101.041 / 101.042 ms |
| fire to fire (`_gap_ns`, `_fire_lo`) | - | 101.048 / 101.049 ms |

So the OS delivered the interval it was asked for to within 1.04–1.10 ms, and that residue is the whole
of the dispatch: decrementer expiry, the payload's IRQ handler, `rtclock_intr`, the thread-call daemon
being made runnable, and the work loop's gate. The three latencies agree to 60 µs, and the two
inter-fire intervals to 1 µs, which is what makes the residue a measurement of the machine rather than
of one fire: run 1 measured 1.105 ms for its first fire and 101.046 / 101.049 ms between fires, so two
boots and five fires give the same two numbers.

## The build

Canonical four steps, all green. **Built twice**: run 1 is the first build (`.text` 5264672, image
5487228, `.bss` 362696, payload `.bin` 5982532 / `.img` 5986304 / `.qcdt.img` 8507392, sha256
`3413982d…` / `2dd18e5f…` / `3c5b3882…`), run 2 is the second, after `_fire_lat_ns` became the latency
of the fire it is written at (`.text` 5264672, image 5487228, `.bss` 362664, sha256 `0c93f6da…` /
`3dbbd4c9…` / `045d97b6…`). The pool is 626 C tried / 625 compiled and 83/83 C++ plus the platform
block; `gen_assym.sh` 266 defines; `assemble_arm_layer.sh` 17 ok / 0 failed; the entry build links 27
undefined symbols and 59 wraps (53 reached by a branch, 1 same-object-only, 1 never called, 4 by
address), and `.text` grew 1216 bytes over 494's 5263456 for the callback, the arming and the eleven
new keys. `_bss` is 4 words smaller than the first build's because `g_timer_first` - the static the
cumulative key needed - is gone.

The two OS console blocks, the fault counters and the device keys are the 494 values in both runs, so
none of this moved with it: the arming happens **after** 494's closing read (`_rd0` is the record two
lines above the five zeroes) and nothing the boot decides depends on it.

## The check: fourteen claims, fifty-seven mutations

`tools/check_driver_catalogue.py` grew from 13 claims / 46 mutations to **14 claims / 57 mutations**,
all mutations refused, and it runs inside `build_entry.sh` in both modes. Claim 14 (`claim_timer_callback`)
reads the chain out of the source rather than trusting an ordering: the event source must be created on
the work loop with `workLoop()` assigned, `addEventSource` must be called on **that** variable, the
function handed to `timerEventSource` must have a body **in this file** (read out of the call, not
looked up by name), `enable()` must precede the arming `setTimeout`, the interval passed to
`setTimeout`, to `clock_interval_to_deadline` and to the callback's own re-arm must be **one name** that
the file defines as an integer, the callback's body must publish `_fires` and `_fire_lat_ns` and re-arm,
the `_fires` zero must be published before the work loop is created, and the eleven keys that make the
five silent failure modes separable must all be in the record. A driver that arms no timeout is *noted*
rather than failed - `/interrupt-controller`'s is not about time - but at least one in the image must,
or the reading this step is for would be absent from every run.

The eleven new mutations, each refused for its own link: no work loop; the source added to a *different*
object; the action wired to a function this file does not define; no `enable()`; armed before enabling;
a second definition of the interval in the deadline; the callback re-arming with another interval; no
`_fires`; no `_fire_lat_ns`; no re-arm; the `_fires` zero published after the work loop.

**Two of the mutations were first written against the file's prose rather than its code**, and both are
this check's own defects rather than the driver's, found only by running it:

  * `live_keys()` bucketed a driver's keys by cutting at the **last** underscore, so `MSM8974Timer`'s
    ten new keys read as seven prefixes (`…_arm`, `…_due`, `…_fire`, `…_fire_lat`, `…_gap`, `…_rearm`)
    and the claim failed on its own reader. The keys are `xnu_live_<name>_<key>`: split there, and
    anything that does not fit the convention is bucketed separately instead of silently.
  * `driver_timeout()` searched the whole file for the first `setTimeout` and compared its offset with
    `enable()`'s - but the callback is *defined above* `start` and re-arms from its own body, so the
    "first" `setTimeout` was the re-arm and the order reported was about two different regions of the
    file. The search now runs from the work loop forward, and the offsets stay absolute.
  * The interval mutation replaced the `clock_interval_to_deadline` call **as quoted in a comment**
    rather than the call itself, because the needle did not carry the statement's `;`. It passed, and
    the reason it passed is the same reason the check exists: a claim about a comment is not a claim
    about the code.

## What is owed

  * **`/timer`'s second definition**: `MSM8974Timer.cpp` declares no payload symbol for its register
    read, so nothing on the OS side is compared with the payload's own read of the GPT. 494 removed that
    debt for `/interrupt-controller` only.
  * **An interrupt handler a driver installs, and a device *write*.** The payload's handler holds the
    line, and `ml_install_interrupt_handler` is already used by `entry_irq.c` for nub 0;
    `IOCPUInterruptController` (`IOCPU.cpp:731-839`, 28 symbols) with
    `IOPlatformExpert::registerInterruptController` is the framework-native route, and
    `IOService::registerInterrupt` is the per-service one. Until one of them is done, no driver in this
    image owns a line and the distributor is programmed by the payload alone.
  * **A timeout that outlives its asker** - every key here is live-only, and 495's own reading is of
    three fires 300 ms into a boot that runs for seconds.
  * **The other device nodes** (`/arm-io`, `/cpus`, …), **the two services the catalogue answers with
    nothing** (their class), **`MSM8974RootResource`'s `state0 = 0`**, the name/class reader wider than
    eight characters, the release as a reading, `vm_fault` as a caller-side record, 488's deferred
    flag-list derivation, 490's distinct-`(pc, lr)` frames band, and `xnu_live_dec_same`.

## Safety

Two runs, each through `preflight_boot_check.sh --allow-xnu-entry` then
`run_and_capture.sh --allow-xnu-entry`, non-persistent `fastboot boot` only, never a flash. Both exit 0,
both captured (`/tmp/stage90-495-run1.log` 553307 bytes, `/tmp/stage90-495-run2.log` 553595 bytes), both
ending `No errors detected`, both returning to Android on their own. Every watchdog key is 494's except
the one that is a live reading (`hw_watchdog_enabled = 1`, `_timeout_s = 0x19`,
`_bite_ticks_written = _bite_after = 0x000dffac`, `_readback_ok = 1`, `_counter_running = 1`,
`_countdown_plausible = 1`, `_bite_truncated = 0`, and `_countdown_second` 0xc25 -> 0xc0e, which is the
counter still running rather than a second definition of anything), so the net was armed, was running,
and did not bite - and the software dead-man's counters are unchanged at `_seen = 0x20`, `_armed = 0x1c`,
`_redirected = 0x1a`, `_storm = 9` with `_spent` absent. The timeout this step arms is three fires of a
driver's own callback and touches no device register: the only thing it can do to the machine is run a
function that writes five keys to a live buffer.
