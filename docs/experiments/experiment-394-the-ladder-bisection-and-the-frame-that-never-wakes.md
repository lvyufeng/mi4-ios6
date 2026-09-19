# Experiment 394 — the ladder bisection, and the frame that never wakes: `ml_get_max_cpus` blocks in `commpage_populate` for a driver this platform never starts (394–404)

**Step:** no link change. Eleven measurement builds on the 381 link, eleven device runs, and one
host-side reading of the image's call graph — the bisection that 383's note made necessary: since 381
no stub has ever reported, so the frontier is not a missing symbol at all but a **block**, and the
question is which frame it is in.

**Measured:** the boot climbs `kernel_bootstrap_thread`'s whole pre-context-switch ladder as far as
`commpage_populate`, and stops **inside `ml_get_max_cpus`**, in its `thread_block`, waiting for
`max_cpus_initialized` to become `MAX_CPUS_SET`. Nothing in this image can set it: the only function
that does is `ml_init_max_cpus`, and its only caller in the whole image is
`IOCPUInterruptController::initCPUInterruptController(int, int)` — an IOKit CPU-interrupt-controller
method, whose platform-expert constructor is not in the open-source tree and is not provided by this
stage's platform expert. **The boot is not hung in the sense of a spinning CPU; it is parked, and the
hardware watchdog is what returns the device.**

## The eleven runs

| # | checkpoint | variant | measured |
| --- | --- | --- | --- |
| 394 | `mapping_adjust` | plain | `stub_hit=mapping_adjust`, caller `0x8000E608` = `kernel_bootstrap_thread + 0x88` |
| 395 | `mapping_adjust` | `_AFTER` | `cp_calls=1` `cp_ret=0xC05BB7FC` — it returns (the value is `thread_deallocate`'s, which its tail branch returns) |
| 396 | `PE_init_iokit` | plain | `stub_hit=PE_init_iokit`, caller `0x8000E6B4` = `kernel_bootstrap_thread + 0x134` |
| 397 | `vm_shared_region_init` | plain | `stub_hit=vm_shared_region_init`, caller `0x8000E6CC` = `kernel_bootstrap_thread + 0x14C` — so `PE_init_iokit` *ran and returned*, `early_boot_complete = TRUE` was stored, and **`spllo()` (`ml_set_interrupts_enabled(TRUE)`) executed**: interrupts are enabled from here on |
| 398 | `arm_vm_prot_finalize` | plain | **silent** — the ladder's step 15 is not reached |
| 399 | `panic` | plain | **silent** — nothing panics anywhere on this boot |
| 400 | `OSCompareAndSwap64` | plain | **silent** — its first site on the walk, `commpage_populate + 0x238`, is not reached |
| 401 | `pmap_create_sharedpage` | plain | `stub_hit=pmap_create_sharedpage`, caller `0x800E6C34` = `commpage_populate + 0xC` |
| 402 | `pmap_create_sharedpage` | `_AFTER` | `cp_calls=1` `cp_ret=0x8050E000` — it returns, having mapped `_COMM_PAGE_BASE_ADDRESS` and returned `phystokv(pa)` |
| 403 | `ml_get_max_cpus` | plain | `stub_hit=ml_get_max_cpus`, caller `0x800E6C98` = `commpage_populate + 0x70` — **its first invocation on this boot** |
| 404 | `ml_get_max_cpus` | `_AFTER` | **silent** — it does not return |

## Why the search space was finite before the device was touched

`kernel_bootstrap_thread`'s ladder after `mapping_adjust` is long — `clock_service_create`,
`device_service_create`, `kdp_init`, `kpc_init`, `bootprofile_init`, `ktrace_init`, `kdebug_init`,
`prng_cpu_init`, `bsd_early_init`, `PE_init_iokit`, `vm_shared_region_init`, `vm_commpage_init`,
`vm_commpage_text_init`, `mac_policy_initmach`, `arm_vm_prot_finalize`, five `read_random`,
`vm_set_restrictions`, `bsd_init`, `OSKextRemoveKextBootstrap`, `kdebug_free_early_buf`,
`serial_keyboard_init`, `vm_page_init_local_q`, `thread_bind`, then a tail branch into `vm_pageout` —
and every one of them is real by name. The bracket came from one line of `bsd_init`:

    8003a9f8: bl 80187da8 <throttle_init>      ; bsd_init + 0x8, and throttle_init IS a stub

`bsd_init`'s **first call** is to a stub, so a walk that reached `bsd_init` would have to report
`stub_hit=throttle_init` at key `0x8003A9FC`. It never has. **The hang is therefore strictly before
`bsd_init`** — a bracket of 21 frames, which is why 398's silence (not `arm_vm_prot_finalize`) and
400's (not the CAS64 loop in `commpage_populate`) divide it rather than extend it.

## The two readings that made the last four runs decisive

**398 and 399 rule out the two endings the log alone cannot distinguish.** 398's silence is "the walk
did not reach `arm_vm_prot_finalize`", and 399's is "**`panic` was never called**" — so the ending is
neither a fault nor a panic, and with 401 and 402 in hand (`commpage_populate` entered, its first call
returning) the block is inside `commpage_populate`'s body. A data abort would have named its vector,
and `entry_stub_hit` is reached by any stub that fires; neither happened.

**400 is the run that pointed at the block rather than at a stub.** `pmap_create_sharedpage` returns
(402), so the walk resumes at `commpage_populate + 0x38`; and `commpage_populate`'s body from `+0x38`
to the CAS64 loop at `+0x238` contains **no backward branch at all** — the whole range is register
arithmetic and stores into the shared page at `sharedpage_rw_addr + 0x00..0x14x`. Only nine calls are
in it: `ml_cpu_get_info`, `ml_get_max_cpus`, two `ml_set_interrupts_enabled`, `get_mvfr1`,
`cpuid_get_cpufamily`, `PE_i_can_has_debugger`, `user_timebase_allowed`,
`user_cont_hwclock_allowed`; `get_mvfr1` and both `user_*` are 8 bytes (`mov r0, #imm` / `bx lr`), and
`ml_cpu_get_info`, `ml_set_interrupts_enabled` and `PE_i_can_has_debugger` each have call sites
*earlier* in this same boot (`zone_bootstrap + 0x48`, `spllo` at `kernel_bootstrap_thread + 0x144`,
`kernel_bootstrap + 0x1A0`). Two candidates were left, and the larger of them is `ml_get_max_cpus`.

**And a note on the tool, not the image.** `tools/xnu_entry_callwalk.py` reported "reached no stub on
the straight-line path" for `clock_service_create`, which is true of the *straight line* and false of
the frame: `ipc_clock_init` is at `+0x48`, inside a loop entered by `cmp r0, #1 / blt`, and the tool
prints it — in its **second** list, the guarded sites, headed *"if a conditional branch on that path is
taken instead"*. The tool was right and the first reading of it was wrong: the two-list output is not
one verdict.

## The root cause, read out of the image

`ml_get_max_cpus` is 0x54 bytes, and it is a **wait**, not a read:

    8000796C: mov r0, #0
    80007970: bl ml_set_interrupts_enabled            ; disable
    80007974: mov r4, r0
    80007980: ldr r1, [0x801CEA10]                    ; max_cpus_initialized
    80007984: cmp r1, #1                              ; MAX_CPUS_SET
    80007988: beq 800079A4                            ; set -> just read machine_info
    8000798C: mov r1, #2                              ; MAX_CPUS_WAIT
    80007990: str r1, [0x801CEA10]
    80007998: bl assert_wait                          ; &max_cpus_initialized, THREAD_UNINT
    800079A0: bl thread_block                         ; <-- blocks until it is set
    800079A4: ...
    800079B4: ldr r0, [0x801FFC60 + 8]                ; machine_info.max_cpus

which is `osfmk/arm/machine_routines.c:184` verbatim, including the comment one file over at
`osfmk/arm/commpage/commpage.c:222` — `cpus = ml_get_max_cpus();  // NB: this call can block`. On
Apple's builds the wait is satisfied before it happens: `PE_init_iokit()` (ladder step 10) starts the
IOKit CPU drivers, `IOCPU::start` asks the platform expert for a CPU interrupt controller, the
platform expert constructs one, and `IOCPUInterruptController::initCPUInterruptController`
(`iokit/Kernel/IOCPU.cpp:736`) ends in

    ml_init_max_cpus(numSources);                    // IOCPU.cpp:765

which is the *only* writer of `max_cpus_initialized = MAX_CPUS_SET` anywhere in the tree
(`machine_routines.c:163-178`). In the linked image:

    ml_init_max_cpus            0x800078FC      1 call site:
      _ZN24IOCPUInterruptController26initCPUInterruptControllerEii + 0x94
    ml_install_interrupt_handler                    1 call site:
      _ZN24IOCPUInterruptController18enableCPUInterruptEP5IOCPU + 0x40
    IOCPUInterruptController::initCPUInterruptController(int,int)   0 call sites
    IOCPUInterruptController::start(IOService *)                    0 call sites

and `createCPUInterruptController` — the platform-expert hook that would instantiate it — **is not in
the open-source tree at all** (`grep -rn createCPUInterruptController` over
`external/xnu-4570.1.46` finds nothing; only Apple's closed-source platform experts implement it).
This stage's platform expert (`stages/stage90/xnu_platform/MSM8974PlatformExpert.cpp`, 107 lines)
overrides `start`, `deleteList` and `excludeList` and nothing else.

**Three consequences, and they are the shape of every run since 381.**

1. **Nothing wakes the boot thread.** `ml_init_max_cpus` is the only `thread_wakeup` on that event, and
   it never runs; the timer's own wakeups cannot reach it either, because the only caller of
   `ml_install_interrupt_handler` in the image is the same absent driver — **no interrupt handler is
   ever installed on this boot**, so the timer `load_context` armed (`timer_start` at `+0xBC`/`+0xD4`)
   is armed into nothing.
2. **That is why the ending is silent and safe.** A parked thread with interrupts enabled and no
   handler is a CPU that is idle, not stuck: no `exception:`, no `panic`, no stub report, and the
   hardware watchdog resets the device on its own — the third ending of the four this walk has, and
   the only one that fits a boot which is *waiting* rather than lost.
3. **It is a fourth kind of stop, and it is the kind this project had not seen.** 285–287's stops were
   decided by a missing *symbol*, an invented *value* and a supplied/omitted *string*; this one is
   decided by a **cross-subsystem ordering dependency**: XNU's own boot thread cannot pass
   `commpage_populate` until an IOKit driver that the platform must publish has run, and the
   open-source tree contains the driver's class but nothing that instantiates it.

## What the next step is

Not another link step — the walk is past every stub on this path. The platform expert is where the
dependency lives, and it is stage-owned code: the shim must do what Apple's closed-source platform
expert does, i.e. create an `IOCPUInterruptController` (`OSDefineMetaClassAndStructors`, concrete in
`iokit/Kernel/IOCPU.cpp` and already in the link), call `initCPUInterruptController(sources, cpus)` on
it — which both sets `max_cpus_initialized` and allocates the per-vector locks — and then
`registerCPUInterruptController()` so the platform can hand out the interrupt machinery the timer
needs. The alternative, smaller and less faithful, is for `start` to call `ml_init_max_cpus` directly
(`osfmk/arm/machine_routines.h:486`); it unblocks the boot without giving the machine a timer, so it
is a probe rather than a fix.

## Safety

Nothing was linked in this series; every run is the 381 image plus one wrapper, booted
non-persistently (`fastboot boot`, never flash) through `preflight_boot_check.sh --allow-xnu-entry`
and `run_and_capture.sh --allow-xnu-entry`. No run reported an `exception:` line or a panic; each
log carries 25 records of `persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`
with no non-zero reading, `abort_entries=0`, and the device returned to Android on its own every time
and was confirmed there (`adb devices` shows `4a2fe00b`). The silent runs' logs are byte-identical
(294553 bytes, 3929 lines), the reporting runs' differ only in the report — which is what a
deterministic block in a deterministic image should look like.
