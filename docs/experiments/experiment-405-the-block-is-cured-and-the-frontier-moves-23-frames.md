# Experiment 405 — the block is cured and the frontier moves 23 frames: the platform expert announces one CPU, and `bsd_init`'s first statement stops on `throttle_init`

**Step:** no object linked. The step is stage-owned code — `stages/stage90/xnu_platform/MSM8974PlatformExpert.cpp`'s
`start` now calls `ml_init_max_cpus(1)` after `super::start(provider)` returns — and it is the smallest thing that can
clear the block experiments 403 and 404 measured.

**Measured: `stub_hit=throttle_init` at caller key `0x8003A9FC` = `bsd_init + 0x8`, name and key exactly as
predicted.** The boot thread returns from `ml_get_max_cpus`, `commpage_populate` completes the ~100 instructions after
it, the six frames between it and `bsd_init` run for the first time in this walk's history, and the walk stops on the
**first statement of the BSD kernel's own init** — the stub `throttle_init`.

## Why this call, and why `1`

`ml_get_max_cpus` (`osfmk/arm/machine_routines.c:184`) is a wait, not a read:

    if (max_cpus_initialized != MAX_CPUS_SET) {
        max_cpus_initialized = MAX_CPUS_WAIT;
        assert_wait((event_t) & max_cpus_initialized, THREAD_UNINT);
        (void) thread_block(THREAD_CONTINUE_NULL);
    }

and `commpage_populate` calls it under the comment `// NB: this call can block` (`osfmk/arm/commpage/commpage.c:222`).
The only writer of that flag is `ml_init_max_cpus` (`machine_routines.c:163`), whose only caller in the linked image is
`IOCPUInterruptController::initCPUInterruptController(int, int)+0x94` (`iokit/Kernel/IOCPU.cpp:765`) — and the platform
hook that would construct that controller, `createCPUInterruptController`, **is not in the open-source tree at all**;
only Apple's closed-source platform experts implement it. `ml_install_interrupt_handler`'s only caller in the image is
the same class's `enableCPUInterrupt`, so no interrupt handler is installed on this boot either.

This call is deliberately the **probe**, not Apple's own path. Apple's platforms set the fact from the CPU interrupt
controller's constructor, which also allocates one lock per interrupt source and, through `enableCPUInterrupt`, installs
the per-CPU interrupt handler the timer needs. Two reasons for the smaller thing:

  * **it is stage-owned code, so it costs no link** — the stub set is untouched, which is what makes the prediction
    below a statement about the *path* rather than about the step;
  * **the number announced is the number this payload actually brings up.** No CPU driver exists in this image, so
    nothing can start a second processor; `1` is the truth of the machine as it stands. Announcing four (the SoC's real
    core count, and what `initCPUInterruptController(numSources)` would be passed on Apple's platform) would size
    `cpu_data`, the scheduler's per-CPU arrays and the shared page's `_COMM_PAGE_NCPUS` for processors that never
    register.

**The faithful version is still owed, and this step does not pretend to be it:** the machine has no timer handler
installed and no per-CPU vector locks. Nothing has needed one yet — the scheduler's own wakeups carried the boot
thread's `thread_block(THREAD_CONTINUE_NULL)` at 392, and this run crosses 23 frames with interrupts enabled and not
one delivered interrupt — but a kernel that reaches user space will need them, and the place to build them is the same
`start` method.

## The prediction, written before the build

The step adds a reference to a symbol the image already defines, so the stub set is 404's: 644 function and 96 storage
stubs, `throttle_init` still record 544 of 740 (`func throttle_init T`). Every frame between the block and `bsd_init`
was walked in the image this step would start from:

    vm_commpage_init          reached no stub on the straight-line path
    vm_commpage_text_init     reached no stub on the straight-line path
    mac_policy_initmach       reached no stub on the straight-line path
    arm_vm_prot_finalize      reached no stub on the straight-line path
    read_random               reached no stub on the straight-line path
    vm_set_restrictions       reached no stub on the straight-line path (host_info+0x17c -> vm_purgeable_stats is off it)
    bsd_init                  first stub on the straight-line path: throttle_init

    bsd_init:
      8003a9f8: bl 80187dc8 <throttle_init>      ; +0x8 — the caller key is 0x8003A9FC

so the predicted stop was `stub_hit=throttle_init`, key `0x8003A9FC`. The falsifiers, named in advance: a stop at any
earlier stub (measured to be absent from all six straight lines), an indirect call landing on a stub (the class this
walk has hit at 365/374/378), a `panic` — note `commpage_populate`'s `panic` at `+0x84` is taken iff `ml_get_max_cpus`
returns 0, and it returns 1 — a fault (`exception: data abort`), or a second block of the kind 404 found.

## What the run measured

    xnu_entry_stub_hit                        throttle_init         (line 3970)
    xnu_entry_stub_caller_v=0x8003a9fc        = bsd_init + 0x8
    xnu_entry_stub_caller_w0=0x33303038       '8003'   (the address, as digits)
    xnu_entry_stub_caller_w1=0x63663961       'a9fc'
    xnu_entry_abort_entries=0x00000000
    xnu_entry_checks=0x00000005  xnu_entry_failures=0x00000000
    xnu_entry_checksum=0x904065c5             (unchanged — the payload's own checks are 381's)
    xnu_entry_why=0x801895e8  xnu_entry_why_byte=0x00000061

`why` is the payload's own message and not a name in the image: the bytes at `0x801895E8` are
`"a symbol this image does not provide was called"`, which `why_byte`'s `0x61` (`'a'`) confirms. The ending is
therefore a **stub stop**, as at 375–382, and not a fault.

**Three things this run measures besides its own prediction.**

1. **The block was the only thing between `commpage_populate + 0x70` and `bsd_init`.** 23 frames of
   `kernel_bootstrap_thread`'s ladder ran: `commpage_populate`'s remaining ~100 instructions — including **both**
   `OSCompareAndSwap64` sites at `+0x238`/`+0x288`, which run 400 could not reach and whose silence 400 therefore never
   explained — then `vm_commpage_text_init`, `mac_policy_initmach`, `arm_vm_prot_finalize`, five `read_random` calls
   and `vm_set_restrictions`. The walk had never been in any of those frames.
2. **Interrupts were enabled at 397 (`spllo()`) and no vector fired and faulted.** The boot crossed those 23 frames with
   `SCTLR.I` set, no handler installed and no `exception:` line: a machine with interrupts enabled and nothing armed is
   quiet, which is the same fact that made 404's ending silent and safe.
3. **No stub was retired, so nothing about the stub table moved**: `throttle_init` is still record 544 of 740,
   `func throttle_init T`, and the layout is 381's to the byte except for the step's own 32 bytes of `.text`.

## Layout

    text size    1768544 (0x1AFD40)   — 381's 1768512 + 0x20: the call and its branch in start
    image bytes  1877400 (0x1CA598)   — unchanged: the growth stays in the same 0x4000 bucket
    bss          0x801CA5C0 .. 0x802035D8 (233496 bytes)  — unchanged, start and end
    layout       args +2117632, topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     2083368 bytes below topOfKernelData    — unchanged
    payload      out/stage90/stage90-qcdt.img, 4896768 bytes, sha256
                 0f1f79371ef5ebb2d551cb4bcb572d7dcd4783e8eba1c69d25ca034bccab739c

`MSM8974PlatformExpert.o`'s text grew from the class's own 0x150 by the call's few instructions plus its new reference
to a real symbol — `U ml_init_max_cpus`, resolved against the image's `0x800078FC`, so no stub is created for it.

## Where the frontier is now

`throttle_init` is defined by `bsd/miscfs/specfs/spec_vnops.c:1287`, so the next step links
`bsd_miscfs_specfs_spec_vnops.o` — an object this walk already measured at 378 (**4 resolved / 91 added**), which is
what makes it a step whose stop will *not* be in its own object: 91 new stub names, none of them the one `bsd_init`
calls next. The frontier has left `osfmk`'s commpage and IOKit's platform plane and is now in the BSD kernel's init
sequence.

## Safety

A non-persistent `fastboot boot` of the image above through `preflight_boot_check.sh --allow-xnu-entry` and
`run_and_capture.sh --allow-xnu-entry`; nothing flashed. Preflight green. The log carries **25 records of
`persistent_write_attempted=0x00000000`** and **87 of `failure_mask=0x00000000`**, none non-zero; `abort_entries=0`
with all ten `abort_first_*` readings zero; `checks=5` / `failures=0`; no `panic` and no `exception:`; 301625 bytes,
3975 lines, last line `No errors detected`. The device returned to Android on its own (the hardware watchdog: the
payload's software dead-man never dumped — its arming lines are in the log and no dump follows) and was confirmed
there afterwards (`adb devices` shows `4a2fe00b`).
