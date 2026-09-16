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

Two independent checks that this is the right choice rather than merely a convenient one:

1. It agrees with the pmap the project already builds. With `virtBase = 0x80000000` the
   correspondence is `VA = 0x80000000 + PA`, and `full_pmap` maps `0x80000000–0x800fffff` to
   PA `0–0xfffff` through L2 pages — the same translation XNU's own section mapping would
   produce, arrived at independently.
2. It costs nothing at handoff time: no relocation pass, no position-independent payload, no
   change to where the image is loaded, so no new way for the existing 90 stages of behaviour
   to break.

**Implemented** in `stages/stage90/xnu_boot_args_conformant.c`, behind `STAGE90_XNU_BOOT_ARGS`
(default off — it builds a *second* `boot_args` and validates it; the ladder's identity-based
one is untouched, because the ladder itself requires `physBase == 0x8000`).

| Field | Value | Why |
| --- | --- | --- |
| `virtBase` | `0x80000000` | Same as `STAGE90_VIRT_BASE`, so it agrees with the existing pmap. |
| `physBase` | `0x00000000` | 1 MB aligned; the image at `0x8000` is inside `[0, memSize)`. |
| `memSize` | `0x00200000` (2 MB) | Deliberately conservative: this is a claim that those physical addresses are RAM, and below `0x80000000` that is not safe to assume for a large span on MSM8974. 2 MB is what the payload has actually exercised (mmu.c's identity table maps PA 0–2 MB, and ninety stages have run from it). Raising it wants a memory map, not a guess. |
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
  `ranges` and `chip-revision`.

The same tool also checks each node header's `nProperties` against what its block emits,
because a wrong count does not fail to build and does not fail at `DTInit` — the walker
reads the number it was given and lands in the middle of the next property name. Both checks
are negative-tested: perturbing a count makes them fail.

### Recorded, not resolved: `reg` is absolute, XNU expects an offset

`pe_arm_map_interrupt_controller` computes `gPicBase = soc_phys + reg[0]`, i.e. Apple's DT
model expects a node's `reg` to be an **offset from the SoC base**. Our `/interrupt-controller`
and `/timer` nodes carry absolute addresses, because that is what the project's own iokit
contract selftests read. Both cannot be true at once, and getting it wrong maps the wrong
physical addresses — the kind of error that presents as an unrelated fault.

This is deliberately left as a decision rather than guessed at, because it is where Phase 2
meets Phase 3: satisfying Apple's convention means `reg` values relative to `0xf9000000`,
but `pe_arm_map_interrupt_controller` is Apple-platform code that on MSM8974 would be
replaced by a shim anyway. Phase 3 should decide it, with the shim in view.

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
