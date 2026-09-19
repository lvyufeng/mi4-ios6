# Experiment 420 — the platform expert publishes `IORTC`, and the boot thread reaches `ubc_init`

**Step:** no object linked and no symbol added. One statement, four instructions / sixteen bytes, at the
end of `MSM8974PlatformExpert::start` (`stages/stage90/xnu_platform/MSM8974PlatformExpert.cpp`):

    IOService::publishResource( "IORTC" );

**Prediction, written into the source before the build:** `stub_hit=ubc_init`, caller key
`0x8003B1B8` — the return address of the `bl <ubc_init>` at `0x8003B1B4`, `bsd_init + 0x7C4`.
**Measured: exactly that.**

    line 3930: MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
    line 3932: MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
    line 3938:  xnu_entry_stub_caller_v=0x8003b1b8
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973: MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ubc_init
    line 3974:  xnu_entry_stub_caller=0x8003b1b8
    line 3978: No errors detected

## The step's effect

     0 resolved / 0 added — the stub set is untouched, as predicted
     787 / 667 / 120 stub names, 667 function, 120 storage — **the same three numbers as 419**

The payload sha256 changed (`11c00602…` → `24f42358…`), so the link did take effect. But **every
marker in the pre-jump report is byte-identical to 419's**: `image_bytes=0x001FADD0`,
`bss_start=0x801FAE00`, `bss_end=0x80238358`, `args_pa=0x8023A000`, `device_tree_pa=0x80600000`,
`checksum=0x904020CD`, `checks=5`, `failures=0`. This is defect 130 again, and this step is the
cleanest instance of it so far because **it was predicted in the comment before the build**: a link
change that neither moves a section boundary nor changes the stub set is invisible to the whole
report, and the stop is the only thing that can prove the new image ran.

**Why nothing moved.** The added call is sixteen bytes of code, and the `.text` output section is
`0x1DCD00` in both links. The section ends with an `ALIGN(32)` whose cursor this time lands exactly on
the boundary — `.ARM.exidx` ends at `0x801DCD00` and the `ALIGN` at `entry.map:10074` is a no-op. Two
links, one size, one of them sixteen bytes larger: the previous link's cursor was therefore sixteen
bytes *below* the boundary and its `ALIGN` paid the difference. The added code fits inside padding the
link already carried. And `bsd_init`'s own addresses cannot move at all from this step: the statement
is inside an object linked at `0x801B01E4`, after every XNU object `bsd_init` can reach.

## The reading: the wait ended, and the frontier was a resource

419 left the boot thread in `IOKitInitializeTime` → `waitForService(resourceMatching("IORTC"), 30 s)`,
with both ways out closed. This step closes the question the other way — by making the match succeed —
and the run says it did, because **the thread that reported is the boot thread**: the caller key is a
site inside `bsd_init`.

The mechanism is now read end to end, and each link in it is Apple's own code, already real in this
image:

  * `resourceMatching("IORTC")` is `serviceMatching("IOResources")` plus a `IOResourceMatch` entry —
    and `serviceMatching` sets **`IOProviderClass`**, not `IOClass` (`IOService.cpp:4801-4817`).
  * That is the one spelling `copyExistingServices` has a dedicated fast path for
    (`IOService.cpp:4282-4299`): if the table's `IOProviderClass` is `gIOResourcesKey` it tests
    `gIOResources` **directly**, requiring `inState == (service->__state[0] & inState)` — with
    `inState = kIOServiceMatchedState`. Before this step the nub had no matched state, which is why
    419's lookup found nothing; `registerService()` is what sets it.
  * `matchPassive` then calls the two-argument virtual `matchPropertyTable(table, &score)`, which
    `IOService::matchPropertyTable` implements as `return( matchPropertyTable(table) )`
    (`IOService.cpp:968-974`) — so `IOResources`' one-argument override runs
    (`IOService.cpp:5083-5091`), and it is a pure property test: `0 != getProperty("IORTC")` on the
    resource nub.
  * `publishResource(key, 0)` is what puts that property there: `value` defaults to
    `(OSObject *) gIOServiceKey`, `gIOResources->setProperty(key, value)`, then
    `gIOResources->registerService()` (`IOService.cpp:3532-3544`).

**This is not a shortcut around Apple's code — it is the idiom.** `IOService::setPlatform` publishes
`"IOKit"` with the same call (`IOService.cpp:1212`), and `setPlatform` is called by
`IOPlatformExpert::start`, i.e. from inside `super::start(provider)` — a few statements *earlier in
this very function*. `publishResource("IONVRAM")` (`IOPlatformExpert.cpp:1209`) and
`publishResource("IOBSD")` (`IOKitBSDInit.cpp:95`) are the other two. And the nub is live: `gIOResources`
is created by `IOService::initialize()` (`IOService.cpp:433`), called from `IOLibInit()` at
`IOStartIOKit.cpp:91` — before `rootNub->registerService()` at `:165`, which is what eventually calls
this `start`.

**Falsifiers, named in advance, and what each would have meant — all silent:**

  * **the same silence**, the outcome that would have falsified the analysis — no: the report came;
  * a stop inside `waitForService`, `resourceMatching`, `copyExistingServices`, `setNotification` or
    `IORecursiveLockSleepDeadline` — no: every one of those names is real in this image, and with the
    match found `waitForMatchingService` breaks before `setNotification` and before `SLEEPNOTIFYTO`;
  * `ast_taken_kernel` — the stub three guards inside `clock_initialize_calendar`, reachable only if
    an AST is pending when `ml_set_interrupts_enabled` runs. It did not fire, so
    **`clock_initialize_calendar` ran clean — its first execution ever in this image**, since
    `PEGetUTCTimeOfDay`'s virtual dispatch needs `gIOPlatform` set and `gIOPlatform` is only set by
    `IOService::setPlatform`, from `IOPlatformExpert::start`. `getGMTTimeOfDay` returns 0
    (`IOPlatformExpert.cpp:333`), so the calendar is set from a zero clock: harmless, and the reason
    no clock panic appeared;
  * `CURSIG` (419's name, for a signalled thread), `vfsinit` at `+0x7C8` key `0x8003B1BC` (which would
    have meant `ubc_init` had already been retired) — neither;
  * a `panic` — none: `abort_entries=0`, no `exception:`, `checks=5` / `failures=0`.

**And 418/419's diagnosis is now confirmed rather than inferred.** The prediction that the frontier was
an *unmet resource* rather than a missing symbol was written before the build and hit to the caller
address. 418's switch to the buffer-clean thread was a true observation of a thread 415's `bufinit`
started; 419's silence was that thread parked on a real `msleep0` while the boot thread slept in
`waitForService` on a resource nothing published.

## Layout

    stubnames    787 records (667 function, 120 storage) — 419's set, unchanged
    text size    1953024 (0x1DCD00)  — 419's, because the added 16 bytes fit the final ALIGN(32)
    data         0x801E0000 .. 0x801FABB8 (0x1ABB8), sysctl_set 0x801FABB8, init_array 0x801FAD40
    image bytes  2076112 (0x1FADD0)   — the image ends at `.init_array`'s end
    bss          0x801FAE00 .. 0x80238358 (251224 bytes)
    layout       args +2334720 (0x8023A000), topOfKernelData +4194304 (0x80400000),
                 tree +6291456 (0x80600000, len 0x7358), window 8388608
    headroom     1866920 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 24f423581cb34703e934b5529b11f9a9c71c17b2d539f851b16abaa47565baf8
    entry map    .text's last input is `.ARM.exidx` ending at 0x801DCD00, then a no-op ALIGN(32)

The image the device ran checks out against the image on disk: `ubc_init` is at `0x801B2E10` and its
`bl` is at `0x8003B1B4`, so the return address the stub reported is `0x8003B1B8` to the byte.
`xnu_entry_stub_caller_w0=0x33303038` / `w1=0x38623162` are that address as ASCII — `8003` and `b1b8`.

## Where the frontier is now

**`ubc_init`** (`bsd/kern/ubc_subr.c:761`, linked at `0x801B2E10`, pool object `bsd_kern_ubc_subr.o`), reached from `bsd_init + 0x7C4` — the first *stub* stop on `bsd_init`'s
own statement list since the walk turned into a resource hunt, and the first time the boot thread's
line has produced the report since 415. Behind it on the same line, one statement apart each:
`vfsinit` (`+0x7C8`, key `0x8003B1BC`), `proc_uuid_policy_init`, `mcache_init`, `mbinit`,
`net_str_id_init`, `knote_init`, and the rest of the fifteen stubs 415 listed, on toward
`vfs_mountroot`.

**The timer is still owed and is now the largest open item.** This step removed exactly one wait, and
it removed it by supplying the resource — no timer was involved, which is why it was worth doing
first. Every later deadline in the boot is still unbounded while `cpu_set_decrementer_func` is NULL:
`IOKitBSDInit`'s `waitForService`s, `IOFindBSDRoot`, the workloop's event sources. The faithful fix
remains what 419 named — the environment becoming the platform's timer driver, `ml_init_timebase`
with an MSM8974 `tbd_ops_t` driving the GPT at `0xf9020000` (19.2 MHz, the timer the payload's own IRQ
handler already counts at IAR `0x13`) — together with 405's `IOCPUInterruptController`, since
`initCPUInterruptController` is also what installs the per-CPU interrupt handler that timer needs.

**And the net's horizon now matters again.** With the boot thread moving, a stop longer than 29 s is
unobservable rather than absent: the hardware watchdog's bark register is 20 bits at 32765 Hz, so 29 s
is the longest bark this hardware can hold, and this run armed 25 s bark / 28 s bite
(`0x000C7FB5` / `0x000DFFAC`, both read back). A future step that legitimately waits longer than that
will look exactly like a hang.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`,
`checks=5` / `failures=0`, no `exception:`, no `panic`; 301802 bytes / 3978 lines, ending
`No errors detected`. The only net across the jump armed and running
(`hw_watchdog_counter_running=0x00000001`, `hw_watchdog_bark_after=0x000c7fb5`,
`hw_watchdog_bite_after=0x000dffac`, `hw_watchdog_bite_truncated=0x00000000`) and it is what ended the
run, ~28 s after the jump; the dead-man PPI was deliberately disarmed before it
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Device returned to Android on its own and was
confirmed there (`adb devices` shows `4a2fe00b`).
