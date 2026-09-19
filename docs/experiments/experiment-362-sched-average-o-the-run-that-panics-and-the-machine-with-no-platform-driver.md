# Experiment 362 — `sched_average.c`: the run that panics, and the machine that has no platform driver

**Step:** link one object, `osfmk/kern/sched_average.c` (`osfmk_kern_sched_average.o`) — the pool definer of 361's
stop `compute_averages`. Nothing else changes.

**Prediction:** *5 resolved (1 function, 4 storage) / 1 added — 790 → **786** undefined, 682 → **682** function,
108 → **104** storage;* `.text` ending **0x801A3720**; `.data` **0x801A4000** (0x19A58) unmoved; `.sysctl_set`
**0x801BDA58**; `.init_array` **0x801BDBA8** (0x78) ending 0x801BDC20; `.bss` **0x801BDC40** (0x0390D8);
`__bss_end` **0x801F6D18**; image **1825824**; headroom **1086184**; `args` **+2064384**; `topOfKernelData`
**+3145728**; and the stop at `ccdrbg_factory_yarrow` at key `0x8003827C`, with `throttle_init` at
`bsd_init+0x8` (key `0x8003A9FC`) as the fallback.

**Result:** every count and every row exact, the pad rule's **tenth** confirmation a third zero-fill — and **the
run did not stop at all: it panicked inside real code**, at `IOPanicPlatform::start`'s own
`panic("Unable to find driver for this platform: \"%s\".\n", …)`. There is no `stub_hit`, no `stub_caller`, and
`abort_entries=0`: the instrument's own classification is `exception: undefined instruction`. The machine has no
platform driver, and the kernel's only defence against that is the panic Apple wrote for it.

## What the layout did

| | 361 measured | 362 predicted | 362 measured |
|---|---|---|---|
| `.text` | 0x801A3280 | end **0x801A3720** | **0x801A3720** |
| `.data` | 0x801A4000 (0x199B0) | 0x801A4000 (0x19A58) | **0x801A4000** (0x19A58) |
| `.sysctl_set` | 0x801BD9B0 (0x150) | **0x801BDA58** | **0x801BDA58** (0x150) |
| `.init_array` | 0x801BDB00 (0x78, 30) | **0x801BDBA8** (0x78) | **0x801BDBA8** (0x78, thirty) |
| its end | 0x801BDB78 | **0x801BDC20** | **0x801BDC20** |
| `.bss` | 0x801BDB80 | **0x801BDC40** | **0x801BDC40** |
| `.bss` size | 0x391D8 | **0x390D8** | **0x390D8** (0x0390D8) |
| `__bss_end` | 0x801F6D58 | **0x801F6D18** | **0x801F6D18** |
| image | 1825656 | **1825824** | **1825824** |
| headroom | 1086120 | **1086184** | **1086184** |
| `args` | +2064384 | **+2064384** | **+2064384** |
| `topOfKernelData` | +3145728 | +3145728 (unmoved) | **+3145728** |

`786 symbol(s) undefined` in the 682 function / 104 storage split, `the copied image ends at 0x801bdc20, 32 bytes
below __bss_start`, and the pad rule's tenth confirmation in its third zero form: the object's 0x38 lands at
0x801F4508 exactly in 361's `*fill* 0x38`, ends at 0x801F4540 where `realstubs.o`'s `.bss` (0x27C4) begins, and
the `.bss` run reads as one unbroken chain with not a byte of fill in it —
`IOEventSource.o` 0x801F4480 (0x18), `vm_shared_region.o` 0x801F4498 (0x70), `sched_average.o` 0x801F4508 (0x38),
the three zero-size shims, `realstubs.o` 0x801F4540 (0x27C4).

## The run: a trap, not a stop

```
MI4IOS6_STAGE90_XNU real XNU entry: exception: undefined instruction
 xnu_entry_kv_written=0x000002f9   xnu_entry_kv_in_dram=0x0000031d   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x8017ea20          xnu_entry_why_byte=0x00000065     'e'
 xnu_entry_stub_caller_v=0x00000000   (and _a and _e, all three zero)
 xnu_entry_abort_entries=0x00000000
 xnu_entry_undef_lr=0x8002dd8c     xnu_entry_undef_pc=0x8002dd88     xnu_entry_undef_spsr=0x60000093
 xnu_entry_trap_r9_fmt=0x8019de16  xnu_entry_trap_r8_args=0xc80abeb0 xnu_entry_trap_sl_options_hi=0x00000000
 xnu_entry_frame_sp=0x801bec28     xnu_entry_frame_r4=0x00000000     xnu_entry_frame_r5=0x00000001
 xnu_entry_frame_r8=0xc80abeb0     xnu_entry_frame_r9=0x8002dd8c     xnu_entry_zone_map_min=0xc05ae000
```

Every one of this walk's stops has read `why_byte=0x61` ('a', "a symbol this image does not provide was
called"); `0x65` is the instrument's own "exception: …" message. `0x2f9`/`0x31d` are the largest kv counts this
walk has reported and the first that belong to an event which is not a stub. The log is 302277 bytes, 3995 lines,
its last line the kernel's own `No errors detected`, and it contains no panic text at all — the kernel's paniclog
goes to its own ram_console, not to the loader's log.

## The trap, byte by byte

`undef_lr=0x8002dd8c` and `undef_pc=0x8002dd88` (the instrument prints `lr − 4`, the ARM architectural return
address of an undefined-instruction exception). The image's own disassembly of that address:

```
8002dd60 <DebuggerTrapWithState>:   push {r4, r5, fp, lr}; sub sp, sp, #16
8002dd68: ldr lr,[sp,#44] / ldr r5,[sp,#40] / ldr r4,[sp,#32] / ldr ip,[sp,#36]
8002dd78: stm sp, {r4, ip} / str r5,[sp,#8] / str lr,[sp,#12]
8002dd84: bl  8002ddc8 <DebuggerSaveState>
8002dd88: udf #65006  ; 0xfdee          <- the faulting instruction, and a deliberate one
8002dd8c: bl  80008e00 <current_processor>
```

`udf #65006` is `TRAP_DEBUGGER` for `__arm__` (`osfmk/kern/debug.c:120-121`, `__asm__ volatile("trap")`), and the
only instruction before it is the `bl DebuggerSaveState` that stores the op, message, panic string, args,
options and caller (`debug.c:341-368`). So the trap is `debug.c:382`'s `TRAP_DEBUGGER` inside
`DebuggerTrapWithState` — and of the four calls to that function in the tree (`debug.c:458` DBOP_DEBUGGER,
`debug.c:645` DBOP_PANIC, `profile_runtime.c:116` DBOP_RESET_PGO_COUNTERS, `kern_stackshot.c:278`
DBOP_STACKSHOT) **only `debug.c:645` carries a format string at all**:

```c
DebuggerTrapWithState(DBOP_PANIC, "panic", panic_format_str,
        panic_args, panic_options_mask, TRUE, panic_caller);
```

The trap frame agrees with that call on every field it can: `frame_r4 = 0` is `panic_options_mask`'s low half
(the source passes `0` from `IOPanicPlatform::start`), `frame_r5 = 1` is `db_proceed_on_sync_failure` (`TRUE`),
and `trap_sl_options_hi = 0` is the mask's high half.

## The format string names the call site

`trap_r9_fmt=0x8019de16`, and the image's own disassembly of `IOPanicPlatform::start` names it:

```
80160458 <_ZN15IOPanicPlatform5startEP9IOService>:
8016045c: cmp r1, #0                          ; provider == NULL
80160460: beq 80160480
80160464: ldr r0, [r1] / ldr r2, [r0, #240]   ; provider->getName(), vtable slot 60
80160474: blx r2 / mov r1, r0 / b 80160488    ; platform_name = provider->getName()
80160480: movw r1, #0xddfe / movt r1, #0x8019 ; r1 = 0x8019DDFE = "(unknown platform name)"
80160488: movw r0, #0xde16 / movt r0, #0x8019 ; r0 = 0x8019DE16 = the format, and the trap's r9
80160490: bl 8002deb4 <panic>
```

`r0 = 0x8019DE16` is exactly the address the trap frame reports in `r9`, so the panicking call site is
`IOPanicPlatform::start` and `r9` is the panic *format*, not a return address. The bytes at 0x8019DE16:

```
8019de10 6e616d65 29002255 6e61626c 6520746f   name)."Unable to
8019de40 5c222573 5c222e5c 6e220022 48616c74   \"%s\".\n"."Halt
```

i.e. `"Unable to find driver for this platform: \"%s\".\n"` — **with the quotes and the backslashes in the
image** — because under `CONFIG_EMBEDDED` Apple's own `panic` macro stringifies its first argument
(`osfmk/kern/debug.h:414-419`: `#define panic(ex, ...) (panic)(# ex, ## __VA_ARGS__)`). The literals in the image
are therefore *source text*, and a trap's format field is a source-text address; reading it as the value a C
compiler would have produced is the mistake this shape invites. The string occurs once in the image in that form
and zero times unescaped, and the neighbouring run of strings (`(unknown platform name)`, `"Halt/Restart Timed
Out"` from `IOPlatformExpert.cpp:783`) is the same file's.

## Why the machine panics: the kernel's only personality is Apple's fallback

`iokit/Kernel/IOStartIOKit.cpp:155` is `rootNub = new IOPlatformExpertDevice`, `:165` is
`rootNub->registerService()` — and the kernel's catalogue of personalities is *one entry*:

```
iokit/KernelConfigTables.cpp:35
  gIOKernelConfigTables = "( { 'IOClass' = IOPanicPlatform;
                               'IOProviderClass' = IOPlatformExpertDevice; 'IOProbeScore' = 0:32; } )"
```

`IOCatalogue::initialize` parses that string with `OSUnserialize` (`IOCatalogue.cpp:98-102`); matching allocates
the driver by *class name* out of the matched personality (`IOService.cpp:3296-3301`,
`OSMetaClass::allocClassWithName(props->getObject(gIOClassKey))`); and `IOPanicPlatform::start`
(`IOPlatformExpert.cpp:1725-1733`) is Apple's designed fallback — the class's own comment is "If no legitimate
IOPlatformDevice matches, this one does and panics the kernel with a suitable message". The panic is therefore
correct XNU behaviour reporting a real absence: **this machine has no platform driver**, and every `PE_*` call and
the whole device-tree plane hang off the one that is missing.

`registerService` does not match inline; it enqueues `_IOServiceJob::startJob`, the asynchronous branch that
360's block named as its first falsifier. That job can only run once the boot is preemptible (`spllo()` after
`PE_init_iokit`), and it can only *finish* the match once the objects the matching machinery calls are real (361
and 362). 361 answered the first event of that hand-off with a *stub*; 362 answers the next with a **kernel panic
inside real code** — the walk's eighth frontier kind, and exactly the outcome 361's falsifier (c) said the
instrument cannot report as a stop. The 362 prediction is refuted in its stop and correct in everything else: the
walk never reached `read_random`'s factory call, because the thread that panicked got there first.

## One decode table in the instrument carries another image's offsets

`fleh_undef`'s frame block was written against an image whose prologue saved five words. The 362 image's prologue
saves six — `80002cf4: strd r4,[sp,#-24]!`, `80002cf8: strd r6,[sp,#8]`, `80002cfc: str r8,[sp,#16]`,
**`80002d00: str lr,[sp,#20]`** — so the block's +20 word is the trapping context's `lr`, printed under the name
`frame_r9`, and the +24/+28 keys (`frame_sl`, `frame_lr`) read uninitialized stack. The run's own values say so
in four places at once: `frame_r9 = 0x8002dd8c` equals `undef_lr`, `frame_r8 = 0xc80abeb0` equals `trap_r8_args`,
`frame_r4 = 0` is the options mask's low half and `frame_r5 = 1` is `db_proceed_on_sync_failure` — the same
cross-checks the comment claims, one slot earlier. `frame_sl = 0x253` is therefore *not* the options mask's high
half here (`trap_sl_options_hi` reads 0 and the source's mask is 0), and the older runs' reading of `frame_r5` as
"the instruction after the `bl panic`" does not hold in this image either. The names are the check, and the check
moved; the comment in `entry_stubs.c` now carries the current prologue and the current mapping.

## Falsifiers

* **A second panic site passing 0x8019DE16** — refuted: the string is `IOPanicPlatform::start`'s own literal and
  occurs once in the image (zero times unescaped), and that function loads exactly this address into `r0` before
  `bl panic`.
* **`Debugger()` rather than `panic`** — refuted by the format: `DebuggerWithContext` is a different function
  whose string argument would be the caller's message; the address here is the panic format, and the options mask
  in the frame is 0, which is what `IOPanicPlatform::start` passes.
* **A nested panic** (`CPUDEBUGGERCOUNT > 1`, where `DebuggerSaveState` keeps the *first* panic's string) — not
  separable from this frame; the string is consistent with the first panic either way.

## Safety

Non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 302277 bytes whose last line is the kernel's own
`No errors detected`, and the device back on Android on its own (`MI 4LTE`, release 10).

## Frontier: 363 must supply the platform expert, because the tree has none

Three facts, each read out of the tree rather than guessed:

* `IOPanicPlatform` is the only *concrete* platform expert in the link, and its `start` panics by design.
* `IODTPlatformExpert` — Apple's whole device-tree platform expert (`probe`, `configure`, `createNub`,
  `createNubs`, `processTopLevel`, `getModelName`, `getMachineName`, `getNubResources`, `haltRestart`, all of it
  in `iokit_Kernel_IOPlatformExpert.o`, real in this image since 355) — is refused by its own metaclass:
  `IOPlatformExpert.cpp:1243` is `OSDefineMetaClassAndAbstractStructors(IODTPlatformExpert, IOPlatformExpert)`,
  so `allocClassWithName` cannot instantiate it.
* `ApplePlatformExpert` (`IOKit/platform/ApplePlatformExpert.h:61`) is `OSDeclareAbstractStructors` *and* has no
  implementation in the tree at all (`iokit/Kernel/` holds only `IOPlatformExpert.cpp`).

So the missing piece is not an object to link but a class to *author*: a concrete subclass of
`IODTPlatformExpert` whose entire content is its metaclass, inheriting Apple's implementation, plus a personality
that names it ahead of the fallback. That is the step the Stage64..75 plane already modelled
(`/msm8974-platform-driver` with `IOClass = MSM8974PlatformExpert` and `IOProbeScore = 0x00000650`, and "does not
execute a Stage-owned platform driver" among those contracts' boundaries): the scaffold's facts become the
kernel's own driver, and Apple's `IOPanicPlatform` stays in the table behind it, so that a failure to start is
still a panic with a name rather than a silent no-driver boot.
