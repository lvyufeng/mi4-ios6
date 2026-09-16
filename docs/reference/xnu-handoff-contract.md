# The iBoot-Equivalent Handoff Contract

What public ARM XNU's `_start` actually requires from the thing that boots it, read off
`external/xnu-4570.1.46/osfmk/arm/start.s`, and where the Stage90 payload does not yet
provide it. This is roadmap Phase 2's input: the contract has to be met before XNU's own
instructions can run at all.

Line references are to `external/xnu-4570.1.46/osfmk/arm/start.s` unless stated otherwise.
Field offsets are from `external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h`.

## What `_start` does, in order

| Line | What happens |
| --- | --- |
| 87–91 | Entry: `r0` = **physical** address of `boot_args`, `r1` = 0, and it saves `arm_init` as the return target. |
| 92 | `cpsid if` — masks IRQ and FIQ. |
| 95–100 | Invalidates the I-cache, then sets `SCTLR.ICACHE` and `SCTLR.PREDIC`. **With the MMU still off.** |
| 104–106 | Reads `physBase`, `virtBase`, `memSize` out of `boot_args`. |
| 149–156 | Writes `topOfKernelData \| TTBR_SETUP` to **both TTBR0 and TTBR1**, and `TTBCR = TTBCR_N_1GB_TTB0`. |
| 158–170 | Invalidates a fixed run of translation table entries at `topOfKernelData`. |
| 172–184 | Builds a section-descriptor template: `ARM_TTE_TYPE_BLOCK`, attributes `ARM_TTE_BLOCK_ATTRINDX(CACHE_ATTRINDX_DEFAULT)`, protection `AP_RWNA` ("kernel rw, user no access"), plus `ARM_TTE_BLOCK_AF` and `ARM_TTE_BLOCK_SH`. |
| 186–189 | Maps a **V=P section around the current PC**, so the code that is executing keeps executing. |
| 195–207 | Maps the kernel: for each 1 MB, stores `physAddr \| template` at the TTE for `virtBase + n MB`, walking the physical address from `physBase` upward for `memSize` bytes. |
| 209+ | Handles a non-1 MB-aligned end by allocating an L2 table. |

So XNU does not want a page table handed to it — **it builds its own**, in the memory at
`topOfKernelData`, and it does so while executing at physical addresses with the I-cache on.

## The four requirements this places on `boot_args`

1. **`virtBase` must be the kernel's link-time virtual base.**
   `LOAD_ADDR` (`osfmk/arm/asm.h:235`) loads the value of a non-lazy pointer, i.e. the *virtual*
   address of a label. `LOAD_PHYS_ADDR` in this file (lines 108–111) is exactly
   `LOAD_ADDR(reg,label) - virtBase + physBase` — it converts a virtual address to a physical
   one using these two fields. If `virtBase` is not the real link base, every one of those
   conversions is wrong, starting with the exception-vector patching at lines 112–140.

2. **`physBase` and `virtBase` must be 1 MB aligned.**
   Lines 195–207 OR the physical address straight into a section descriptor
   (`orr r11, r7, r6`) and increment it by 1 MB. A `physBase` with low bits set produces a
   descriptor with those bits set inside the attribute field — for `0x8000` that sets AP[2],
   changing the mapping's protection and producing nonsense. Section descriptors require the
   physical address to be 1 MB aligned, so this is not a stylistic point.

3. **`topOfKernelData` must be writable RAM, inside `[physBase, physBase+memSize)`, above the
   kernel image, with room for the bootstrap tables.**
   XNU writes its L1 there (line 149–170: `topOfKernelData|TTBR_SETUP` is the table base, and
   the invalidation loop runs from it), plus the L2 table it may steal at line 211, plus the
   trampoline and CPU tables. It must not overlap the kernel image or anything XNU will later
   use for kernel data.

4. **`memSize` is the size of the contiguous region XNU may map with sections.** The loop walks
   `memSize` bytes of physical memory starting at `physBase` and maps it at `virtBase`. It has
   to be real, contiguous RAM, and the kernel image has to be inside it.

## Where the current Stage90 payload stands

`stages/stage90/boot_args.c` sets:

```c
args->virtBase = 0;                  /* Stage84 boot args are still identity-based before MMU handoff. */
args->physBase = STAGE90_BASE;       /* 0x00008000 */
args->memSize  = RAM_CONSOLE_BASE - RAM_PHYS_BASE;
args->topOfKernelData = (uint32_t)(uintptr_t)__stage90_image_end;
```

The struct layout is already right — `stages/stage90/stage90.h`'s `struct boot_args` and
`struct boot_video` match `boot.h` field for field and type for type, and
`BOOT_LINE_LENGTH`/`Revision`/`Version` agree. The *values* do not:

| Field | Now | Requirement | Verdict |
| --- | --- | --- | --- |
| `virtBase` | `0` | link-time virtual base (`0x80000000` for a 4570-derived kernel) | **wrong** |
| `physBase` | `0x00008000` | 1 MB aligned | **wrong** |
| `memSize` | `0x5e500000` | contiguous real RAM inside the mapped region | plausible |
| `topOfKernelData` | `__stage90_image_end` ≈ `0x00115000` | writable, above the image, inside `[physBase, physBase+memSize)` | plausible *only if* `physBase` is fixed |

`virtBase = 0` is the deliberate identity-based choice of the Stage-owned ladder, and the
ladder's own validation depends on it (`stage90_arm_init_stub` requires
`args->physBase == STAGE90_BASE`). So this is not a bug to patch in place — the ladder's
`boot_args` and a conforming XNU `boot_args` are **two different objects**, and Phase 2 needs
the second one produced alongside the first — which is what
`stages/stage90/xnu_boot_args_conformant.c` now does, behind `STAGE90_XNU_BOOT_ARGS`
(default off, so it cannot affect a run that does not ask for it).

The `memSize` figure in the table is the ladder's, and it is the one value worth calling out
as *not* simply wrong: `0x5e500000` is `RAM_CONSOLE_BASE - RAM_PHYS_BASE`, a statement about
the RAM window at `0x80000000`+, which is a different question from "how much RAM is there
below `0x80000000`". The conforming object answers the second question instead.

## The unaligned `physBase`, and why it dissolves

`physBase` is not just unaligned, it is unaligned *because of how the payload is loaded*:
`stages/stage90/build.sh`'s `mkbootimg` invocation uses `--base 0x00000000 --kernel_offset
0x00008000`, so the payload's `_start` runs at physical `0x8000` — the legacy Android
boot-image convention. That looked like it forced a choice between moving the load address
and relocating the image at handoff.

It does not. **`physBase = 0x00000000` is 1 MB aligned, and the image at `0x8000` is simply
*inside* the region `[physBase, physBase + memSize)`.** That is exactly what the boot image's
`kernel_offset` means — an offset within the kernel region, not a base. Reporting `0` is both
conforming and true, and nothing has to move.

Three independent checks that this is the right choice rather than merely a convenient one:

0. **The device's own kernel config.** `arch/arm/configs/cancro_user_defconfig:31` sets
   `CONFIG_PHYS_OFFSET=0x00000000`, i.e. this device's RAM genuinely begins at physical
   address 0 and Linux's linear map starts there. That is not an argument about
   conventions; it is the cancro kernel asserting the same thing this field reports.

1. It agrees with the pmap the project already builds. With `virtBase = 0x80000000` the
   correspondence is `VA = 0x80000000 + PA`, and `full_pmap` maps `0x80000000–0x800fffff` to
   PA `0–0xfffff` through L2 pages — the same translation XNU's own section mapping would
   produce, arrived at independently.
2. It costs nothing at handoff time: no relocation pass, no position-independent payload, no
   change to where the image is loaded, so no new way for the existing 90 stages of behaviour
   to break.

### `memSize` from the device's memory map

`memSize` is a claim about which physical addresses are RAM, and XNU maps that span with
1 MB sections. Two facts from the device pin it down:

- RAM starts at PA 0 (`CONFIG_PHYS_OFFSET` above).
- The cancro device tree removes the first block at `0x5d00000`
  (`arch/arm/boot/dts/msm8974.dtsi:2390`):
  `qcom,memblock-remove = <0x5d00000 0x7d00000  0xfa00000 0x500000>;` — and
  `Documentation/devicetree/bindings/arm/msm/msm_memory_hole.txt` confirms the format is
  `<address size>`.

So the contiguous span from PA 0 is `0x00000000..0x5d00000`, which is 93 MB and exactly
1 MB aligned (93 × `0x100000`). The alignment is not a nicety: XNU's loop steps 1 MB at a
time from `physBase` and stops when `memSize` reaches zero — it has no notion of a hole — so
the region has to end on a section boundary to stop *cleanly* at the boundary. 93 sections
cover it exactly, and the payload now asserts the section multiple rather than trusting the
derivation to stay right.

The earlier value was `0x00200000` — two megabytes, itself a guess and merely a small one.
Safe, but useless: a kernel given 2 MB cannot do anything. This is the same kind of claim
made against the device's memory map instead of against caution.

It still does not describe the *whole* device — there is more RAM above the hole, and using
it needs a region list rather than one span. That is Phase 3's problem; the single-span
contract is what XNU's `_start` itself consumes.

**Implemented** in `stages/stage90/xnu_boot_args_conformant.c`, behind `STAGE90_XNU_BOOT_ARGS`
(default off — it builds a *second* `boot_args` and validates it; the ladder's identity-based
one is untouched, because the ladder itself requires `physBase == 0x8000`).

| Field | Value | Why |
| --- | --- | --- |
| `virtBase` | `0x80000000` | Same as `STAGE90_VIRT_BASE`, so it agrees with the existing pmap. |
| `physBase` | `0x00000000` | 1 MB aligned; the image at `0x8000` is inside `[0, memSize)`. Confirmed by the device's own kernel: `CONFIG_PHYS_OFFSET=0x00000000` in `arch/arm/configs/cancro_user_defconfig:31`. |
| `memSize` | `0x05d00000` (93 MB) | The contiguous span from PA 0, ending at the first block the device tree removes — see below. |
| `topOfKernelData` | `align_up(__stage90_image_end, 16 KB)` | Above the image, and 16 KB aligned because TTBR's low 14 bits carry `TTBR_SETUP`. |

`topOfKernelData` also has to hold the tables `start.s` clears: 10240 TTEs, i.e. 40960 bytes.
That number comes out of the invalidation loop (`PGBYTES>>2`, then `*5`, then `*2`) and
independently out of the page tally in its own comment at line 246 ("4 + 4 + 1" pages plus the
high-vector table) — 4 trampoline L1 pages + 4 CPU L1 pages + 1 L2 page + 1 high-vector page
= 10 pages. Two derivations agreeing is why the payload asserts the region is at least that
big rather than the smaller figure a single reading might suggest.

## Checking the ABI, because a mismatch is silent

`start.s` loads four fields by hand at fixed offsets that come from `offsetof()` in the same
tree (`osfmk/arm/genassym.c`: `BA_VIRT_BASE` 4, `BA_PHYS_BASE` 8, `BA_MEM_SIZE` 12,
`BA_TOP_OF_KERNEL_DATA` 16). So if our struct's field list or types drift from
`pexpert/pexpert/arm/boot.h`, XNU neither fails to build nor faults — it reads the wrong
32-bit word and uses it as the physical base of memory. That is a failure mode with no
symptom until a kernel misbehaves somewhere else entirely.

Two checks, both cheap:

- `tools/check_boot_args_abi.py` computes both layouts for ARM ILP32 and compares field by
  field; `build.sh` runs it (skipping with a warning if `external/` is absent). Its
  perturbation test — swapping a field and confirming it reports 18 differences and exits 1 —
  is part of its value: a checker that cannot fail is not a checker.
- `_Static_assert`s in `xnu_boot_args_conformant.c` for the size and the four hot offsets, so
  a drift cannot reach hardware even if the host tool is skipped.


## The device tree half

`tools/xnu_dt_requirements.py` scans XNU's ARM sources for the device-tree lookups
(`pe_init.c`, `pe_identify_machine.c`, `pe_consistent_debug.c`, `pe_serial.c`,
`machine_routines.c`) and checks ours against them. It classifies by **consequence**, which
is the only classification that matters: a raw scan finds ~40 property names, most of which
XNU reads opportunistically and falls back from, and reporting those as failures would drown
the ones that actually stop a bring-up.

Two were genuine blockers, both found by this check rather than by a device:

- **`/cpus/cpu@N` had no `state` property.** `machine_routines.c:474` panics
  *"unable to retrieve state for cpu 0"* under `MACH_ASSERT`, and
  `pe_identify_machine.c:117` silently **skips any cpu node** whose state is not `"running"`
  — so every CPU's `timebase-frequency` was being ignored and the clock stayed at the
  hardcoded fallback. Fixed: each cpu node now carries `state = "running"`.
- **There was no node named `arm-io`.** `pe_identify_machine.c:232` locates the SoC through
  it and takes `gPESoCBasePhys` from `ranges[1]`; without it that value is 0, and
  `pe_arm_map_interrupt_controller` then returns early (`:541`) so **neither the interrupt
  controller nor the timer is ever mapped**. Fixed: an `/arm-io` node with `device_type`,
  `ranges` and `chip-revision`, and `apple_dt_selftest_and_log` now asserts it by name, so a
  hardware run positively confirms that node rather than only confirming the tree walks to
  the right length.

The same tool also checks each node header's `nProperties` against what its block emits,
because a wrong count does not fail to build and does not fail at `DTInit` — the walker
reads the number it was given and lands in the middle of the next property name. Both checks
are negative-tested: perturbing a count makes them fail.

**And the payload's own runtime check has now been tested too, not just asserted.** The
reason a device-tree edit is considered safe without a hardware run is that
`apple_dt_selftest_and_log` catches a wrong count *on the device* — load-bearing, and until
now untested. `tools/host_dt_selftest_probe.c` (run as part of `tools/host_dt_check.sh`)
builds the real tree, locates a node with the payload's own `apple_dt_find_child`, corrupts
exactly one header field, and asks the payload's own selftest whether it notices. Each case
asserts the unmodified tree passes first, so a broken test cannot masquerade as coverage:

```
  arm-io  nProperties  4+1  detected      cpus  nProperties  3+1  detected
  arm-io  nProperties  4-1  detected      cpus  nChildren    4+1  detected
  timer   nProperties  6+1  detected      cpus  nChildren    4-1  detected
  timer   nProperties  6-1  detected      /     nChildren   20+1  detected
  chosen  nProperties  4+1  detected
  memory  nProperties  4+1  detected      -> 10/10 detected, 0 invalid
```

A single-field miscount at any level is caught — including the root's own child count, which
the walk-length check covers by requiring `skip_node` to land exactly at the buffer end.

**And when it is caught, the cost is a reboot rather than a hang** — worth checking rather
than assuming, since a hang is what the safety constraint is about. The order in a default
run is:

| Site | Step |
| --- | --- |
| `stage90_main:905` | hardware watchdog armed — the net exists |
| `stage90_main:936` | device tree built |
| `stage90_main:953` | `kernel_entry()` |
| `xnu_kernel:29` | `apple_dt_selftest_and_log` |
| `xnu_kernel:64` | software dead-man armed |

The selftest runs *after* the hardware net and *before* the software one, and its failure
path (`return 0`) reaches `platform_reboot()`, which forces a watchdog bite. So a
device-tree mistake costs one reboot and a log line — not a hung phone. That is the specific
combination the earlier ordering work was for.

### Not resolved here: `reg` is absolute, XNU expects an offset

`pe_arm_map_interrupt_controller` computes `gPicBase = soc_phys + reg[0]`, so Apple's model
expects `reg` to be an **offset from the SoC base**, while our `/interrupt-controller` and
`/timer` carry absolute addresses. Left as a decision for Phase 3, with the arithmetic and
both consequences spelled out in *What `PE_init_platform(FALSE, args)` then does* below.


## What `PE_init_platform(FALSE, args)` then does — and two concrete blockers

`start.s` is only the first step. The Stage-owned ladder already models the next one
(`xnu_pe_init_platform_false.c`), so it is worth reading what the real function requires,
because two of its requirements our device tree currently fails.

`PE_init_platform(boolean_t vm_initialized, void *args)` (`pexpert/arm/pe_init.c:283`) with
`vm_initialized == FALSE` does, in order:

1. `PE_state.bootArgs = args`, and copies the `Video` fields into `PE_state.video`
   (`:294-307`). Harmless with a zeroed `Video` — `v_scale` is derived from `v_depth` and the
   result is 1.
2. `DTInit(boot_args_ptr->deviceTreeP)` — so `deviceTreeP` must point at a tree the walker
   accepts *before* anything else runs.
3. `pe_identify_machine(boot_args_ptr)`.
4. Then a set of *optional* reads keyed on `DTFindEntry("name", "device-tree")`:
   `target-type`, `model`, `firmware-version`, `unique-chip-id`, `dram-vendor-id` (`:318-368`).
   All behind `kSuccess` checks with sane fallbacks, so absence is fine.

Step 3 is where it stops being forgiving.

### Blocker 1 — `interrupt-controller` must have the value `"master"`

*(Still open, and deliberately: see "Walking the tree with XNU's own reader" below for why
adding the property before the `reg` model is fixed would make things worse rather than
better.)*

`pe_arm_map_interrupt_controller` (`pe_identify_machine.c:534`) does:

```c
gSocPhys = pe_arm_get_soc_base_phys();          /* from the arm-io node's ranges[1] */
if (soc_phys == 0) return 0;                    /* <- no arm-io, no interrupts at all */
if (DTFindEntry("interrupt-controller", "master", &entryP) == kSuccess) {
        DTGetProperty(entryP, "reg", &reg_prop, &prop_size);
        gPicBase = ml_io_map(soc_phys + *reg_prop, *(reg_prop + 1));
}
if (gPicBase == 0) return 0;
```

Two things follow. Our `/interrupt-controller` node has `interrupt-controller = 1` but no
property *named* `interrupt-controller` with the *value* `"master"`, so `DTFindEntry` cannot
match it and `gPicBase` stays 0 — **the function returns failure and there is no interrupt
controller.** That property name/value pair is an Apple platform convention, and it is now
checked by `tools/xnu_dt_requirements.py` rather than left to be discovered.

### Blocker 2 — the interrupt/timer bring-up is a closed set of Apple SoCs

This is the structural one, and it reframes Phase 3.

`pe_arm_map_interrupt_controller` returning failure is not the end of it. Its caller is
`pe_arm_init_interrupts`, which calls it and then `pe_arm_init_timer`
(`pe_identify_machine.c:558`). That function ends in a chain of

```c
#if defined(ARM_BOARD_CLASS_S5L8960X)
        if (!strcmp(gPESoCDeviceType, "s5l8960x-io")) { ... }
        else
#endif
#if defined(ARM_BOARD_CLASS_T7000)
        if (!strcmp(gPESoCDeviceType, "t7000-io") || ...) { ... }
        else
#endif
        ... S7002, S8000, T8002, T8010, T8011 ...
                return 0;
```

and `pexpert/pexpert/arm/board_config.h` — the 32-bit ARM one — defines exactly **three**
board classes: `ARM_BOARD_CLASS_S7002`, and `ARM_BOARD_CLASS_T8002` (for both T8002 and
T8004). There is no generic path and no MSM8974 path.

**So on MSM8974 `pe_arm_init_timer` returns 0 unconditionally, and
`pe_arm_init_interrupts` with it, whatever our device tree says.** No device-tree value can
change that; it would need an `ARM_BOARD_CLASS_*` that does not exist.

Three consequences for the plan:

1. **Phase 3's shim is mandatory, and it must replace `pe_arm_init_interrupts` as a whole**
   — not patch the mapping helper inside it. There is no configuration in which the stock
   function succeeds on this platform.
2. **The `reg` model question does not affect a working system.** The function returns 0
   first, so whatever address it computes is never used to drive a real driver. Resolving
   `reg` is therefore *not* on the critical path — which is the opposite of how the earlier
   section framed it, and a better outcome: one fewer blocking decision.
3. **But the protective absence of `interrupt-controller = "master"` is still right, and for
   a sharper reason.** `ml_io_map` is not a bookkeeping call — it is
   `io_map(phys_addr, size, VM_WIMG_IO)` (`machine_routines.c:698`), a real pmap operation.
   With our absolute `reg[0]` and `ranges[1]`, adding that property would have XNU install a
   mapping for the 32-bit-wrapped `0xf2000000` *before* the function returns 0. The absence
   prevents a wrong mapping being installed on the way to a failure.

The honest shape of Phase 3 is therefore: **write an MSM8974 replacement for the ARM
platform bring-up**, rather than trying to satisfy Apple's platform code through device-tree
values. That is a larger and clearer piece of work than "fix the `reg` model", and it is
better to know that now than after attempting the latter.

### Blocker 3 — `reg` is an offset from the SoC base, and ours is absolute

*(Not on the critical path — see Blocker 2 above. Recorded because it still governs what
`interrupt-controller = "master"` would do if anyone adds it.)*

`gPicBase = soc_phys + *reg_prop` is the arithmetic. Apple's model is that a platform node's
`reg` is an **offset** from the SoC base, and on an Apple SoC the first entry is typically
`0`. Our `/interrupt-controller` and `/timer` carry absolute addresses (`0xf9000000`,
`0xf9020000`) because that is what the project's own iokit contract selftests read.

With `soc_phys = 0xf9000000` (our `ranges[1]`) and `reg[0] = 0xf9000000`, XNU would map
`0x1f2000000` — wrapped into 32 bits, `0xf2000000`, which is not the GIC. The wrong address,
mapped, with no error. This is the same item the earlier section recorded as unresolved, now
with the arithmetic spelled out and two consequences rather than one:

1. Our selftests want absolute `reg`; XNU's Apple platform code wants offsets. Both cannot
   hold in one property.
2. The second `reg` entry is used as the **size** (`*(reg_prop + 1)`), so the pairs must stay
   `<reg, size>` and not become a flat list.

The resolution belongs to Phase 3, and the honest framing is that Phase 3 replaces
`pe_arm_map_interrupt_controller` with an MSM8974 shim — at which point the shim itself decides
what `reg` means and the Apple convention stops binding. Trying to satisfy both now would
mean choosing a `reg` encoding for a reader we are about to replace.


## A boot-arg that turns on an asserting path

Worth knowing before anyone edits the `CommandLine`, because it is a trap in the other
direction: `PE_init_iokit` (`pexpert/arm/pe_init.c:171`, called from
`osfmk/kern/startup.c:545`) contains two bare `assert(*delta >= 0)` calls in its boot-progress
calculation (`:62`, `:65`). They are only reached if the boot-arg `-progress` is present:

```c
if (PE_parse_boot_argn("-progress", &show_progress, sizeof(show_progress)) && show_progress) {
        ... assert(*delta >= 0); ...
}
```

Our command lines do not carry `-progress`, so the path is skipped — but the values feeding
those asserts come from `PE_state.video.v_width`/`v_height` and the boot images, i.e. from
display state we deliberately leave zeroed. Adding `-progress` to the command line would enter
a graphics path, with asserts, using a zeroed `Video`. Phase 2 has no reason to add it; this
is recorded so that if someone does, the failure is recognised rather than mysterious.

More generally: `PE_parse_boot_argn` means the `CommandLine` is an input to paths that assert.
It is not inert text.


## Walking the tree with XNU's own reader, on the host

`tools/host_dt_check.sh` compiles `external/xnu-upstream/pexpert/gen/device_tree.c` — XNU's
real walker — for the build host and runs it over the tree our builder produces. It is
wired into `stage90/build.sh` and it is the stronger half of the device-tree check:
`tools/xnu_dt_requirements.py` reads *source text*, so it can see that a property *name*
exists somewhere; only a walker can answer whether a node actually carries the *value* XNU
matches on. Those are different questions, and confusing them produced a false pass.

It works because pexpert's `device_tree.c` is portable C once `kalloc`/`kfree` are ordinary
allocation — so this is XNU's code, not a re-implementation of it. The builder is not a
re-implementation either: `host_dt_check.sh` extracts `build_stage90_apple_dt` **verbatim**
from `stage90_main.c`, extracts `align4` from `runtime.c`, and pulls the constants out of
`stage90.h`'s own preprocessor output. If the extraction ever stops finding the function it
aborts rather than quietly testing nothing.

### What it found

**`/timer` had no `device_type = "timer"`.** `pe_arm_map_interrupt_controller` locates the
timer with `DTFindEntry("device_type", "timer")` — by property *value* — so having
`name = "timer"` is not enough, and a node without it leaves `gTimerBase` at 0. The
source-level scan had reported this as satisfied, because it only asked whether some node
had a `device_type` property, and `/arm-io` and the cpu nodes do.

That is the whole argument for the harness in one example: the weak check passed, the strong
check failed, and the strong check was right.

The property is now emitted. It is inert today — and deliberately so: `pe_arm_map_interrupt_controller`
returns `0` before reaching the timer lookup, because the interrupt controller is not found
either (below), so the wrong-address problem is not yet reachable.

### The device-tree buffer is 89% full, and always has been

The harness reports how much of `g_apple_dt` the tree consumes, because that margin is a
real failure mode: `apple_dt_finish` returns 0 when the builder exceeds its capacity, and
`stage90_main` then calls `platform_reboot()`. An overgrown tree therefore reboots the
payload at the DT build — visible in the log as `apple_dt build failed`, but as a reboot
rather than a message, so the reason would have to be inferred from where output stops.

Measured, at both revisions:

| Revision | Tree size | Of 32 KB |
| --- | --- | --- |
| Before the `/arm-io` + `state` fixes | 28932 | 88.3% |
| Now (also with `device_type = "timer"`) | 29332 | 89.5% |

So the three additions cost 400 bytes and the buffer was **already 88% full** — this is a
pre-existing tight margin, not something the fixes introduced. It is reported on every build
and the harness warns above 95%. Worth knowing before adding nodes: there is room for roughly
3 KB more, which is a handful of nodes, not an unbounded amount.

(This is also why the count is worth having in the log: `built_apple_dt_len` is emitted on
device, so a run shows the same number the host measured.)

### What it verifies now

```
paths XNU looks up with DTLookupEntry:   /chosen ok, /cpus ok
nodes XNU locates by (property, value):  name=device-tree ok, name=arm-io ok,
                                         device_type=timer ok
properties XNU reads:                    /arm-io ranges, device_type, chip-revision;
                                         /device-tree target-type, model - all ok
cpu topology:                            4 children, all with state="running"
```

`interrupt-controller = "master"` is reported as **deliberately absent** rather than a gap,
and the reason is worth stating precisely: adding it today would let XNU find the node,
read `reg[0]`, and compute `soc_phys + reg[0]`. It would still return 0 from the `gPicBase`
check — but the moment the `reg` model is fixed, that same property would turn a clean
failure into a wrong-address mapping. **The absence is protective.** Phase 3 resolves the
`reg` model first; the harness records the decision rather than hiding it.

## Other things to check before trusting a handoff

- **`PRRR`/`NMRR` are not programmed by the payload.** `CACHE_ATTRINDX_DEFAULT` is
  `CACHE_ATTRINDX_WRITEBACK`, and `ARM_TTE_BLOCK_ATTRINDX()` encodes an attribute *index*
  (TEX[0], C, B) that the ARMv7 PRRR/NMRR attribute indirection resolves. XNU's own
  `arm_init`/pmap sets these up, and on ARMv7 the reset value makes every index Normal
  Non-cacheable — which is safe but uncached. Worth confirming what the payload leaves behind,
  rather than assuming.
- **`TTBR_SETUP`** is `TTBR_RGN_WRITEBACK|TTBR_IRGN_WRITEBACK|TTBR_SHARED`: XNU puts the
  table-walk memory attributes in TTBR itself. Those bits land in the low half of TTBR, which
  is why `topOfKernelData` must be at least 16 KB aligned (it needs bits 13:0 clear).
- **`machineType`.** The payload passes `0x9074`, an invented MSM8974 tag. XNU's
  `PE_init_platform` and device-tree matching use this, so it has to be a value the platform
  code recognises — that is Phase 3's problem, not this contract's, but it is on the list.
- **Device tree.** XNU reads `deviceTreeP`/`deviceTreeLength` and expects a real flattened
  Apple device tree (`/cpus`, `/memory`, `/chosen`, and the platform node its `PE_*` code
  matches against). The payload builds a plausible one, but "plausible" has not been tested
  against what XNU's device-tree walker actually demands.
