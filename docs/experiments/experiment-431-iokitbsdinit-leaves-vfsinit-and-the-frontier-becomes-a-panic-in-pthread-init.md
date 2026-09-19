# Experiment 431 — `iokit/bsddev/IOKitBSDInit.cpp`: the walk leaves `vfsinit` for the first time, and the frontier becomes a **panic** in `pthread_init` — a stop that is not a stub

**Step:** one object linked — `iokit_bsddev_IOKitBSDInit.o`, the pool's only definer of
`IOServicePublishResource` (430's frontier) and of eight more names this walk has been stepping past
— appended to `LINK_OBJS` after `bsd_kern_decmpfs.o`.

**Effect:** **9 resolved (9 function, 0 storage) / 4 added (4 function, 0 storage)** — counts
**773 -> 768** undefined, **621 -> 616** function, **152 -> 152** storage, all three exactly
`tools/entry_object_effect.py`'s prediction. Three of the nine are the names 430 created; the other
six are the BSD-device front door: `IOFindBSDRoot`, `IOSecureBSDRoot`, `IOKitBSDInit`,
`IOBSDGetPlatformUUID`, `IOBSDMountChange` and `IOTaskHasEntitlement`.

    iokit_bsddev_IOKitBSDInit.o   9 resolved / 4 added (di_root_ramfile, mdevadd, mdevlookup, mdevremoveall)

**Placement, predicted from a chain and measured to the byte.** `entry.ld:45`'s `*(.text .text.*)`
places each object's `.text` in command-line order with no gap beyond an input's own alignment, and
the last five objects of `LINK_OBJS` are contiguous in the 430 map:

    bsd_kern_sys_generic.o   0x801ED620 + 0x3D64 = 0x801F1384
    bsd_vfs_vfs_quota.o      0x801F1384 + 0x1900 = 0x801F2C84
    security_mac_vfs.o       0x801F2C84 + 0x756C = 0x801FA1F0
    bsd_vfs_kpi_vfs.o        0x801FA1F0 + 0x53A0 = 0x801FF590
    bsd_kern_decmpfs.o       0x801FF590 + 0x347C = 0x80202A0C   <- this object's `.text` starts here

so `IOServicePublishResource` was predicted at `0x80202A0C + 0x1C` and **measured at 0x80202A28**.
That is the fifth consecutive check of the reading and the fourth time it has decided an address.

## The prediction, and how it was wrong

**Prediction, written before the build:** `stub_hit=nwk_wq_init`, caller key `0x8003B1FC` —
`bsd_init` at `0x8003A9F0` (linked long before this step's object, so it cannot move), the
`bl <nwk_wq_init>` at `bsd_init + 0x808`, return address `+0x80C`. The reasoning was that everything
between `bl <vfsinit>` at `bsd_init + 0x7D8` and that call is **fifteen `bl`s with no branch of any
kind**, and that
`tools/xnu_entry_callwalk.py --root bsd_init` answered `nwk_wq_init` — the same answer it has given
since rule 426 and the one 425 wrote down in advance.

**Measured on hardware: the run went further than any run before it and stopped somewhere else.**

    MI4IOS6_STAGE90_XNU real XNU entry: exception: undefined instruction
     xnu_entry_undef_pc=0x8002dd88            # DebuggerTrapWithState + 0x28, the `udf`
     xnu_entry_undef_lr=0x8002dd8c            # the instruction after it
     xnu_entry_trap_r9_fmt=0x802302a0         # the panic format string
     xnu_entry_abort_entries=0x00000000
    MI4IOS6_STAGE90_XNU xnu_entry_checks=0x00000005   xnu_entry_failures=0x00000000
    No errors detected

The string at `0x802302A0` is

    "pthread kernel extension not loaded (function table is NULL)."

and it occurs exactly once in the whole tree, at `bsd/kern/pthread_shims.c:275`:

    void
    pthread_init(void)
    {
        if (!pthread_functions) {
            panic("pthread kernel extension not loaded (function table is NULL).");
        }
        pthread_functions->pthread_init();
    }

`bsd_init` calls `pthread_init` at `bsd_init + 0x7F4` — the eleventh of the fifteen calls between
`vfsinit` and the predicted stop. Ten of them ran clean. **The run did what the prediction said it
would (leave `vfsinit`, walk `bsd_init`'s line) and then stopped on a `panic`, which is not a stub.**

## How the report was read, and why `r9` is the panic message

The registers are the only place the message survives: `debugger_panic_str`, `debugger_message` and
`debugger_panic_caller` are `B` objects in `osfmk_kern_debug.o` and they are **zero** in this run
(`xnu_entry_panic_str=0x00000000`), because `handle_debugger_trap` sets them from the CPU's debugger
context and restores them to NULL before returning, and it runs before `DebuggerTrapWithState`. The
vector trampoline loads SP and branches without pushing anything, so the handler's first statement
sees the trapping instruction's registers intact, and `panic_trap_to_debugger` has already moved its
first four arguments into callee-saved registers at `+0x10`. So:

    r9  = panic_format_str   -> 0x802302A0, the string above
    r8  = panic_args
    r4/sl = db_panic_options (low/high)
    r5  = panic_caller

This is the mechanism experiments 236–240 built, and 238 read a message through it the same way
(`zfree: freeing invalid ...`). `xnu_entry_trap_r9_fmt` is therefore not a hint about the frontier —
**it is the frontier**, read out of the register file rather than out of a cleared global.

**And the call chain is confirmed by disassembly rather than by the string alone.** `panic` at
`0x8002DEB4` is a 0x48-byte wrapper whose only call is `bl <panic_trap_to_debugger>` at `+0x34`;
`panic_trap_to_debugger` (`debug.c:645`) is what calls
`DebuggerTrapWithState(DBOP_PANIC, "panic", panic_format_str, ...)`; and `DebuggerTrapWithState` at
`0x8002DD60` is `bl <DebuggerSaveState>` then `udf #65006` at `+0x28` — which is exactly the
`undef_pc` the payload recorded.

## Why the walk could not have said this, and it is the step's real lesson

**The walk's two lists are both built from the stub set.** The first is "the first stub on the
straight-line path"; the second is "guarded call sites **whose callee is a stub or leads to one**"
(the tool's own `guards_to_stub(callee)` filter). `pthread_init`'s panic is a guarded call to
`panic`, which is **real**, so it appears in neither list. Asked directly:

    $ tools/xnu_entry_callwalk.py --elf out/stage90/xnu_arm_entry.elf --root pthread_init
    walk from pthread_init reached no stub on the straight-line path.
      either everything on it is real, or it continues through an indirect call ...
      indirect calls the walk could not follow: ...

— true, and silent about the only thing that happened.

So the walk answers *"the first stub"*, and the method read that as *"where the run stops"*. Those
were the same thing for 428 steps; they are not the same thing now. **A real function that decides to
panic is not representable in the model at all**, and no amount of reading the second list would have
found it.

**This is a fourth kind of stop, and it is the second kind in its most visible form.** The standing
taxonomy was: a missing symbol (a stub that names itself), an invented zero (a stand-in that reads
zero, silently), a boot-arg/DT string. Here the value that decides the branch is a **real `.bss`
pointer that nothing wrote**:

    pthread_functions   0x802927C8, 4 bytes of .bss, defined by bsd_kern_pthread_shims.o

whose only writer is `pthread_kext_register` (`pthread_shims.c:712`), whose only caller in the source
tree is **`pthread.kext`** — an Apple binary this tree does not contain and which no object in the
695-object pool supplies. `grep -rn pthread_kext_register external/xnu-4570.1.46/` returns the
definition, the declaration in `bsd/sys/pthread_shims.h:355`, and a comment. Nothing calls it.

So the zero is neither silent (as a storage stand-in is) nor self-naming (as a stub is): it is a real
function with a real body whose guard on a zeroed value takes the `panic` branch. The first runs of
this walk met this kind as a *stub* whose body was fine; this one met it as a *panic* whose body is
fine.

## Layout

    entry text   0x2353E0 (2315232)
    entry image  0x2542AC (2441900 bytes)
    bss          0x802542C0 .. 0x802961D8 (270104 bytes, zeroed by the payload)
    layout       args 0x80298000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1482280 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 da0d904cc3e917c14e1003e70a6d7b8ba5bd3388471bd52045be6690541f9da7 (5462016 bytes)
    entry bin    2441900 bytes; sha256 d37794dd62747de536a2746eb25cb4dd824215ddedf00bf65b7e47e1ecee87e6

Run markers agree with the link: `xnu_entry_image_bytes=0x002542AC`,
`xnu_entry_bss_start=0x802542C0`, `xnu_entry_bss_end=0x802961D8`, `xnu_entry_args_pa=0x80298000`,
`xnu_entry_top_of_kernel_data=0x80400000`, `xnu_entry_checks=0x00000005`,
`xnu_entry_failures=0x00000000`, `xnu_entry_checksum=0x9040E1F1`, `xnu_entry_entering_at=0x80000074`.
Every marker differs from 430's. `.text` grew 0x14C0 and the image grew 0x4000 — the `.data` boundary
again: `.text`'s end moved from 0x80233F20 to 0x802353E0, so `.data` was pushed from the 0x80234000
boundary to 0x80238000 and the whole of the padding landed in the image. `.bss`'s *size* did not
change at all (270104 in both), and its start moved exactly 0x4000 with `.data`.

The image is reproducible from the committed sources: rebuilding both stages after the comment block
above was finished produced byte-identical `xnu_arm_entry.bin` (`d37794dd…`) and
`stage90-qcdt.img` (`da0d904c…`).

## Where the frontier is now, and what 432 has to be

**`pthread_init`'s guard** (`bsd/kern/pthread_shims.c:275`), reached from `bsd_init + 0x7F4`. The
predicted next stub — `nwk_wq_init` (`bsd_net_nwk_wq.o`, `bsd_init + 0x808`, key `0x8003B1FC`) — is
still there and still next, but it is now the answer *after* the table, not the answer.

**And nothing in `LINK_OBJS` can move this frontier: there is no object to link.** The table the
kernel expects is supplied by `pthread.kext`. So 432 is a different shape from every step in this
walk: **the first table the image supplies rather than links.** A Stage-owned
`struct pthread_functions_s`, sized and laid out from Apple's own header
(`bsd/sys/pthread_shims.h` — one definition, not a mirror), registered with
`pthread_kext_register` from an `.init_array` constructor, which runs during `kernel_bootstrap`
(`PE_init_iokit` → `StartIOKit` → `OSlibkernInit` → `OSRuntimeInitializeCPP`) and therefore long
before `bsd_init`.

Two design points are already decided, both by rules this walk has paid for:

* **the table's size and field offsets come from the header**, never from a hand-written mirror — the
  one-value-two-definitions defect this project has a memory about;
* **every entry this project does not implement points at a stand-in that stops the run and names
  itself.** A NULL entry is a fault rather than a stop, and a silent no-op is the wrong-value hazard.
  The caller key that stand-in reports names the *slot*: `pthread_init` ends in a **tail branch**
  (`pop {r4, lr}; bx r0`), so a stand-in called from it records `bsd_init + 0x7F8` — the return
  address of `bl <pthread_init>` — and any other entry called from a `pthread_shims.c` shim records
  that shim's call site. The key names the call, exactly as it has since experiment 244.

So 432's prediction is `stub_hit=stage90_pthread_init` (or the slot's own name) with caller key
`0x8003B1E8`.

**Still owed and unchanged: the timer.** `ml_init_timebase` with an MSM8974 `tbd_ops_t` over the GPT
at `0xf9020000`, plus 405's `IOCPUInterruptController`. It has been one step away since 429 and it
has not moved, because the frontier has not reached the mount machinery yet; `nwk_wq_init`'s
neighbourhood and then `vfs_mountroot` is where it stops being deferred.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure); nothing flashed, nothing written to storage.
25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`,
`checks=5` / `failures=0`, no `panic` line and no data abort — the `exception: undefined instruction`
is XNU's own debugger trap, taken deliberately by the panic path; the run ends there and the hardware
watchdog (`hw_watchdog_counter_running=0x00000001`, `hw_watchdog_bite_truncated=0x00000000`) is the
only net that was armed across the jump, with the dead-man PPI disarmed before it
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Device returned to Android on its own and was
confirmed there (`adb devices` shows `4a2fe00b`). 302462 bytes / 3998 lines, ending
`No errors detected`.

Per-run logs stay apart: `/tmp/run425_kmsg.txt` … `/tmp/run430_kmsg.txt`, `run431_kmsg.txt`.
