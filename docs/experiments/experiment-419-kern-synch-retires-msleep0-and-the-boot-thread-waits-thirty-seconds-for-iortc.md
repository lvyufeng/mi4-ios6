# Experiment 419 — `bsd/kern/kern_synch.c`: `msleep0` retires, and the boot thread is found blocked on an `IORTC` the environment never publishes

*(The 30 s deadline it waits on cannot fire here either: this environment has no timer. See
"the wait has no other way to end" below — the two facts together are what made this run silent.)*

**Step:** one object linked — `bsd_kern_kern_synch.o`, the pool's only definer of `msleep0`
(`bsd/kern/kern_synch.c`) and therefore of 418's stop — appended to `LINK_OBJS` after
`bsd_vfs_vfs_cluster.o`.

**Prediction:** `stub_hit=ubc_init`, caller key `0x8003B1B8` (`bsd_init + 0x7C4`).
**Measured: no stub was hit at all.** The log is 294735 bytes / 3932 lines, of which 3930 are the
payload's; after the entry-time records and the disarm block it ends

    line 3930: MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
    line 3931:
    line 3932: No errors detected

and **`grep -c 'real XNU entry'` = 0** — the teardown never ran, so there is no `stub_hit`, no
`xnu_entry_stub_caller_v`, no `abort_entries`, no `exception:` and no `panic` line. The link did take
effect: the stub names read **787 / 667 / 120** exactly as predicted, `msleep0`, `msleep`, `tsleep`,
`wakeup`, `resetpriority` and `compute_averunnable` are gone, `CURSIG` is a stub, the payload sha256
changed, and `image_bytes` moved `0x001FADC0 -> 0x001FADD0`.

## The step's effect

     7 resolved (6 function, 1 storage) / 1 added (1 function, 0 storage)
      resolved  msleep0, msleep, tsleep, wakeup, resetpriority, compute_averunnable, and the
                **storage** stand-in `averunnable` (`D 0x10`)
      added     CURSIG

    793 -> 787 stub names, 672 -> 667 function, 121 -> 120 storage

All three counts are exactly as the tool predicted; the storage column falls because a `D` stand-in
was retired.

## The reading: a real `msleep0` blocks, and that is enough to silence the run

418's stop was the clean thread's `msleep0` — a stub, which reported and never returned. This step
makes it real. The clean thread then does what the code says:

    while ((bp = TAILQ_FIRST(&bufqueues[BQ_LAUNDRY])) == NULL)
            (void) msleep0(&bufqueues[BQ_LAUNDRY], buf_mtxp, PRIBIO|PDROP, "blaundry", 0, ...);

`hint = 0` is an indefinite wait and the laundry queue is empty on a fresh boot, so the clean thread
**blocks**. Expected next: the boot thread resumes and reaches `ubc_init`. It did not — and the
silence itself is the evidence for where it was instead.

**Why the boot thread could not resume: it was not preempted, it was blocked.** In 418 the switch to
the clean thread was observed through that thread's own stub; in 419 the clean thread's real
`msleep0` parked it, and control still never returned to the boot thread, which would have hit
`ubc_init` within microseconds. A merely preempted thread is runnable and would have been selected.
So the boot thread was unrunnable, and the only unrunnable-making call between `kernel_thread_start`
and `ubc_init` is this one — `thread_deallocate` returns on the `refcount - 1 > 0` path (the clean
thread's refcount is 2 while it is running, `osfmk/kern/thread.c:676`), `vm_set_buffer_cleanup_callout`
is a single `OSCompareAndSwapPtr` (`osfmk/vm/vm_pageout.c:4074`), and between `bufinit`'s return and
`ubc_init` `bsd_init` executes exactly one statement:

    8003b1ac:  bl 8003a95c <bsd_bufferinit>
    8003b1b0:  bl 8011b0b0 <IOKitInitializeTime>      <- this step's real cause
    8003b1b4:  bl 801b2e00 <ubc_init>                 <- a stub since the walk entered bsd_init's list

`IOKitInitializeTime` is 0x3C bytes (`iokit/Kernel/IOStartIOKit.cpp:67`) and is not a pass-through:

    void IOKitInitializeTime( void )
    {
        mach_timespec_t t;
        t.tv_sec = 30;
        t.tv_nsec = 0;
        IOService::waitForService( IOService::resourceMatching("IORTC"), &t );
        clock_initialize_calendar();
    }

The disassembly is that source, call for call: `resourceMatching("IORTC")` at `+0x10`,
`waitForService` at `+0x20`, `clock_initialize_calendar` at `+0x30`, and nothing else. With no `IORTC`
resource published, `IOService::waitForMatchingService` takes the notify lock, finds
`copyExistingServices(matching, kIOServiceMatchedState, kIONotifyOnce)` empty, registers a notifier
and calls `SLEEPNOTIFYTO(&result, deadline)` — `IORecursiveLockSleepDeadline` on `gNotificationLock`
with **30 s** on the clock. The boot thread sleeps; the only other threads are the buffer-clean
thread (now blocked for real) and the idle thread; nothing left on either line calls a stub; the
hardware watchdog resets the SoC and the log ends where it does.

**Why this is a wall and not a slow step: the net cannot outlive the wait, and the wait cannot end.**
The watchdog's bark and bite registers are 20 bits wide at 32765 Hz
(`stages/stage90/hw_watchdog.c:100-111`), which caps the bark at `0xfffff / 32765 = 32.0 s`, less the
vendor driver's 3 s bark→bite gap — **29 s is the largest bark this hardware can hold**, and the
shipped value is 25 s. This run armed exactly that: bark `0x000C7FB5` = 819125 ticks = 25.0 s, bite
`0x000DFFAC` = 917420 = 28.0 s. **28 s < 30 s.** So even if the deadline could fire, no link change
could move the frontier past `IOKitInitializeTime`; and as the next paragraph shows, it cannot fire
either. The run's observable horizon is the net, and the frontier is now longer than the net *and*
unbounded.

**Falsifiers, named in advance, and what ruled each out:** a stop inside `msleep0`, `_sleep` or
`clock_interval_to_deadline` — absent, consistent with all three being real; a stop at `CURSIG`, the
name this step creates — it is called only from `_sleep`'s two pending-signal checks
(`kern_synch.c:96`, `:209`), and a kernel thread created by `kernel_thread_start` with no signal takes
neither branch; a stop at `tsleep` or at 416's `VNOP_BWRITE` — both behind a non-empty laundry queue;
a `panic`; a stop at `vfsinit` (`+0x7C8`, key `0x8003B1BC`), which would have meant `ubc_init` had
already been retired — it is a stub in this image (`0x801b2e00`).

**And the wait has no other way to end: this environment has no timer.** `lck_mtx_sleep_deadline`
(`osfmk/kern/locks.c:865`) is `assert_wait_deadline(event, interruptible, deadline)` followed by
`thread_block` — the deadline can only fire if a *thread timer* fires, and nothing else will ever
wake this event, because nothing in this environment publishes `IORTC` and therefore nothing calls
`IORecursiveLockWakeup` either. Whether a thread timer can fire is decidable from the image the
device ran, and the answer is no:

  * `ml_set_decrementer` (`osfmk/arm/machine_routines_asm.s:1029`, disassembled at `0x8000D8C8` in
    this image) first calls the per-CPU `cpu_set_decrementer_func` if it is non-NULL. It is
    `ldr r2, [r3, #0x60] / cmp r2, #0 / bxne r2` — and when it is NULL the code takes the
    `!__ARM_TIME__` branch: `msr CPSR_c, #0xD1; str ip, [r8, #0x58]; msr CPSR_c, r2`. **A store to
    per-CPU memory, and no write to any timer register at all** — there is no
    `mcr p15, 0, r0, c14, c3, 0` on that path, because this configuration defines only
    `__ARM_TIME_TIMEBASE_ONLY__` (`osfmk/arm/proc_reg.h:98`).
  * That function pointer is filled from `rtclock_timebase_func.tbd_set_decrementer`
    (`osfmk/arm/cpu.c:459-461`), which is only ever assigned by `ml_init_timebase`
    (`osfmk/arm/machine_routines.c:447`) — and `ml_init_timebase` has exactly one caller in the tree,
    `pe_identify_machine` (`pexpert/arm/pe_identify_machine.c:666`), **inside a chain of
    `strcmp(gPESoCDeviceType, "t8000-io")`-style Apple board tests that `return 0`s for every other
    SoC**. `pe_identify_machine` is real and linked here (`0x80005B88`), so it ran, took its
    `return 0` for MSM8974, and left `rtclock_timebase_func` zero — which also means
    `cpu_get_fiq_handler` is NULL, so the `fleh_decirq_handler` the decrementer's interrupt would
    arrive through has nothing to run.

So the thread timer cannot fire, the sleep is **unbounded rather than thirty seconds long**, and the
payload's disarm of its own timer PPI is not what stands in the way. The two readings of 419's
silence are therefore one: the boot thread is blocked in `waitForService` and the watchdog is the
only thing that can end the run.

## Layout

    stubnames    787 records (667 function, 120 storage)
    text size    1953024 (0x1DCD00)
    data         0x801E0000 .. 0x801FABB8 (0x1ABB8), sysctl_set 0x801FABB8, init_array 0x801FAD40
    image bytes  2076112 (0x1FADD0)   — the image ends at `.init_array`'s end
    bss          0x801FAE00 .. 0x80238358 (251224 bytes)
    layout       args +2334720 (0x8023A000), topOfKernelData +4194304 (0x80400000),
                 tree +6291456 (0x80600000, len 0x7358), window 8388608
    headroom     1866920 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 11c00602c092ae38f20f753899f0359bde585687b837e9766fe60e3e7c92bd03

The run's markers agree to the byte: `image_bytes=0x001FADD0`, `bss_start=0x801FAE00`,
`bss_end=0x80238358`, `args_pa=0x8023A000`, `device_tree_pa=0x80600000`, `checksum=0x904020CD`,
`checks=5`, `failures=0`.

## Where the frontier is now

**Not a stub.** It is `IOService::waitForService(resourceMatching("IORTC"), 30 s)`, reached from
`IOKitInitializeTime` at `bsd_init + 0x7C0` — the first frontier in this walk that is the *OS asking
the environment for something*, rather than the link missing a symbol. Behind it: `clock_initialize_calendar`
(real, 0x1E0 bytes, walked clean), then `ubc_init` (`+0x7C4`, key `0x8003B1B8`), `vfsinit`, and the
fifteen more stubs in the next 0x50 bytes of `bsd_init`'s line that 415 already listed.

**The obligation is now the platform expert, and 419 has measured two of its parts at once.** The
first is the resource itself. The match goes through `IOResources::matchPropertyTable`
(`iokit/Kernel/IOService.cpp:5083`), which reads the `IOResourceMatch` key that `resourceMatching` set
and then requires the *matched* service to carry a property of that name — `IORTC`.
`IOService::matchPropertyTable` itself returns `true` unconditionally (`:972`), so the only reason the
lookup fails is that the single `IOService` this environment creates — `IOResources`, from
`iokit_post_constructor_init` → `IOService::initialize()` → `IOResources::resources()` (`:433`),
already running because `ENTRY_LAST_KERNEL_CONSTRUCTOR_OBJ` is `libsa/lastkernelconstructor.c`) —
**overrides the matcher and has no `IORTC` property**. `IOService::publishResource` is exactly the
call the platform expert's own environment-side code uses for its other resources:
`publishResource("IONVRAM")` (`iokit/Kernel/IOPlatformExpert.cpp:1209`), `publishResource("IOKit")`
(`IOService.cpp:1212`), `IOService::publishResource("IOBSD")` (`iokit/bsddev/IOKitBSDInit.cpp:95`).
Publishing `IORTC` on the resource nub makes *this* wait return immediately, without a timer — the
environment supplying what the OS asks for, which is what the RTC driver does in real XNU.

**The second part is the timer, and it is the larger one.** A published `IORTC` removes this one
wait, but every later `msleep`/`IOSleep`/`waitForService` deadline is still unbounded while
`cpu_set_decrementer_func` is NULL — and this boot is full of them (`IOKitBSDInit`'s `waitForService`s,
`IOFindBSDRoot`, the workloop's event sources). A working tick means the environment acting as the
platform's timer driver: registering `tbd_funcs` through `ml_init_timebase` — the same call
`pe_identify_machine` makes for Apple's SoCs, with an MSM8974 implementation whose `tbd_set_decrementer`
programs the platform timer at `0xf9020000` (19.2 MHz, the GPT the payload's own IRQ handler already
counts at IAR `0x13`) and whose `tbd_fiq_handler` reads and EOI's it. That, `IORTC`, and 405's
`IOCPUInterruptController` are the same piece of work: **the environment becoming the platform
expert**, which is what this frontier has been walking toward since `bsd_init` began asking for
services instead of symbols.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts; nothing flashed. 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, no `exception:`, no `panic`,
`checks=5` / `failures=0`; the only net across the jump armed and running
(`hw_watchdog_enabled=0x00000001`, `hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_timeout_s=0x00000019`), and the dead-man PPI deliberately disarmed before the jump
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Device returned to Android on its own and was
confirmed there (`adb devices` shows `4a2fe00b`).
