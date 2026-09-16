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
the second one produced alongside the first.

## The awkward one: the image is loaded at PA `0x8000`

`physBase` is not just unaligned, it is unaligned *because of how the payload is loaded*.
`stages/stage90/build.sh`'s `mkbootimg` invocation uses `--base 0x00000000 --kernel_offset
0x00008000`, so the payload's `_start` runs at physical `0x8000`. That is the legacy Android
boot-image convention, and the payload's own identity table maps PA 0–2 MB to make it work.

XNU's contract, by contrast, wants a 1 MB-aligned `physBase` that is the start of the region it
maps at `virtBase`. Two ways to reconcile that, and the choice is a real architectural
decision rather than a detail:

1. **Load at a 1 MB boundary.** Change the boot image's kernel offset to `0x00100000` and set
   `physBase = 0x00100000` (or `0x00000000` with the image at +1 MB), `virtBase = 0x80000000`.
   Cheapest, and it keeps a single contiguous region. It changes where the payload runs, so it
   needs its own hardware run, and the identity table's section-0 mapping has to keep working.
2. **Relocate before handing off.** Keep loading at `0x8000`, and have the loader copy the
   image to `0x80000000`+ and pass `physBase = virtBase = 0x80000000`. This is closer to what
   real iBoot does, and it means the payload must be position-independent across the copy —
   including its absolute pointers into `.bss` and the C runtime's state.

Option 1 is the smaller step and is worth doing first; option 2 is what a kernel that expects to
own memory from the DRAM base will eventually want.

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
