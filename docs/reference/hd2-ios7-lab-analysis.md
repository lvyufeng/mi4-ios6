# The HD2 iOS7 lab package: what is usable for the mi4 port (2026-10-08)

Read `/mnt/data/ios7-payload/README-FOR-MI4.md`, the v2 package
(`/mnt/data/ios7-payload/v2/ios7/`), and the HD2 kernel source
(`github.com/Garysss123/xnu-hd2`, branch `codex/hd2-source`, = the v2
`KernelSha256` `3e03ee13…`; parent `HTC-Leo-Revival-Project/xnu`). This note is a
**judgement of reusability**, not a plan. The plan is in
`docs/experiments/` (see the end).

## What the package actually is

The HD2 lab is a **complete, working iOS-6-class stack on a different SoC** —
HTC HD2 (`leo`), QSD8250, single-core Scorpion (ARMv7). Its XNU is a *modern*
XNU (Darwin 13.0.0 — **iOS 7 / OS X 10.9 era**) patched to boot on the HD2. That
is a *different and later* XNU than the mi4 target (`external/xnu-2050.18.24` =
**iOS 6**, Darwin 12.x). The source repo does **not** carry the bootloader (the
README says boot chain is out of scope; `startup.txt` shows MAGLDR's AD SDDir, a
Windows-CE-derived loader). The 7-file package is an **overlay**: `zImage` is a
612-byte stub, and `KernelSha256` (9,769,772 B `mach_kernel`) points at a
separate kernel partition.

Verified file facts:

- `rootfs.hfs` is a **real HFS+ volume**. Its volume header at offset `0x400`
  begins `HX 0005` (HFS+ signature `'H+'` + version 5) with `attributes`
  `0x00000100` — `kHFSVolumeUnmountedBit` set, **journaled bit clear**. Same
  shape as the 903 volume: an unmounted, unjournaled HFS+ root, so XNU can mount
  it read-write with no journal replay. 939,524,096 B.
- `initrd.gz` is **not gzip**. It is the **XNU `UNXF`/`PRELINK` container** —
  magic `UNXF` at offset 4 (`0x554E5846`), preceded by an ARM branch
  (`0c 00 00 ea` = `b` over the header), then a size/offset table
  (`0x009617ec` = 9,836,524 B ≈ the file size). Essentially **all of `initrd.gz`
  is the kernel payload**; `BOOT-MAP.json` `Payload.nested_sha256` and
  `mach_sha256` (`3e03ee13…`) are *inside* it. So the initrd and the kernel are
  one artifact — not a ramdisk, not a filesystem image.
- `BOOT-MAP.json` is 321 KB of **build provenance**: Mach-O segments
  (vm_low `0x80004000`, entry_va `0x801378e0`), a checkpoint map (stage 1–10
  probe calls compiled into `arm_init`/`PE_init_SocSupport_stub`), `Payload`
  (prefix/context/initrd hashes, DT 1088 B, entry 0x801378e0), `RootImage`,
  `MutableLog` (`BOOTLG43.BIN`), `Limits`. `status =
  SEALED_FINITE_APP_LAUNCH_GRACE_HARDWARE_PENDING`, `Hardware070Verified=false`.

## The boot contract (this is the reusable part)

From `xnu/pexpert/arm/pe_qsd8250_leo.c` and `common/`:

1. Entry is **`arm_init(boot_args *args)`**, i.e. the standard XNU ARM
   `boot_args` handoff — the same ABI the mi4 port already implements
   (`src/entry/assym.s` `BA_*`, checked by `tools/check_xnu_struct_abi.py`).
2. `boot_args` carries `CommandLine`, `physBase`, `memSize`, `topOfKernelData`,
   `Video…`, `MemoryMap`/`DeviceTree`. The HD2 port reads the command line
   (`PE_boot_args() = ((boot_args*)PE_state.bootArgs)->CommandLine`) and stores
   `PE_state.bootArgs`. Nothing exotic.
3. **The HD2 port does NOT probe RAM and does NOT define a memory map in the
   kernel.** It is handed its memory. `pe_qsd8250_leo.c` receives
   `entry_atags` **by PA in the `boot_args`**, parses the ATAG core/bank list
   (ATAG_MEM), and calls `ios7leo_memory_policy_expected_plan(bank_count,
   bank0_base, bank0_bytes, initrd, atags, loader, plan)`. That policy is
   **hardcoded to the HD2 board**: `bank_count == 1`, `bank0_base == 0x11800000`,
   `bank0_bytes == 0x04000000`, safe end `0x2e7c0000`. For any other bank shape
   it returns `PROFILE_NONE` and admits nothing. Its comment is explicit: *"a
   board-profile admission, not a physical RAM probe"*. The `MemoryPolicy.h`
   rejects everything that is not exactly the HD2's one 64 MB bank.
4. The HD2 has **no high RAM bank** (`0x11800000`, 64 MB, single bank). The mi4
   has a low bank `0x80000000–0xde6fffff` (~1.5 GB) **plus** a high bank near
   `0x100000000` (~1.35 GB) for 3 GB total.

**Consequence.** The HD2 lab confirms the *shape* of the solution but supplies
neither the numbers nor the mechanism for the mi4's 3 GB problem. It is the
**same class of gap** the mi4 has: a single-bank profile that cannot describe a
second bank. The HD2 fixed its case with a *constant*, not a probe.

## The two decisive lessons the HD2 lab delivers

1. **Read the memory map the loader *hands you* — don't probe for it.** The HD2
   port never touches SMEM for memory: its loader (MAGLDR) hands `arm_init` a
   `boot_args` whose ATAG list **is** the map (`c.tags.bank[i].base/bytes`,
   validated for overlap and containment). The 911c SMEM probe
   (`armed-storage-cb4e17f1`, §21) instead mapped SMEM and walked the heap TOC,
   and found **no RAM-partition table** (`ptable_found=0`) — i.e. it probed a
   source that does not hold the map on this device.
   **Caveat, verified:** the mi4 is msm8974/Qualcomm — its bootloader (aboot/LK)
   passes a **flattened device tree (DTB)**, not ATAGs (`r1`/`r2`). So the
   *reusable lesson is the pattern* ("consume the handed map"), **not** HD2's
   ATAG code. **The mi4's handed map is the Qualcomm DTB** — same source our
   docs already use for the USB/GIC/timer nodes (`msm8974.dtsi`), and directly
   readable host-side as `/proc/device-tree/memory@…/reg` or `/proc/iomem` on
   the running stock Android kernel. Our payload already *synthesizes its own
   Apple DT* (`deviceTreeP=0x00010620`) and builds `boot_args`; the missing piece
   is to feed **measured** bank values into that builder instead of constants.
2. **The kernel-side memory admission is a policy over the *handed* list.**
   Generalize `ios7leo_memory_policy_window()`'s single-bank check into a
   **multi-bank** admission for the mi4: accept the loader's list, admit
   `[low_base, low_end)` and `[high_base, high_end)`, exclude initrd/ATAG/loader
   ranges, and publish the union so `sane_size`/`max_mem` reflect ≥3 GB. The HD2
   file shows the exact *form* (`LeoMemoryObservation` = version/known/status/
   bank_count/banks[8]/selected/managed_bytes) — reuse the **observation struct
   shape** for our own memory reading; it is a good, already-correct model.

## What is directly reusable vs. what is not

| Piece | mi4 usable? | Note |
|---|---|---|
| `boot_args` handoff contract | **Yes, already have it** | Same ABI; `assym.s`/`check_xnu_struct_abi.py` already enforce it |
| ATAG bank-list reader (the *pattern*) | **Yes — adopt it** | Replaces the SMEM probe's wrong assumption |
| `LeoMemoryObservation` struct shape | **Yes — model on it** | version/known/status/bank_count/banks/managed_bytes |
| `IOS7LeoMemoryPolicy` code | **No** | Hardcoded to `base 0x11800000`, `bytes 0x04000000`; rejects all else |
| `pe_qsd8250_leo.c` (display/MDP/timer/PE) | **No** | QSD8250 MDP scanout, `ios7leo_checkpoint`, leo timer — SoC-specific |
| `rootfs.hfs` | **Layout, not image** | A real HFS+ root is the right model (we already mount HFS+ in 903); this is an **iOS 7 rootfs**, our target is iOS 6 |
| `initrd.gz` (= `UNXF` prelink of the HD2 kernel) | **No** | It is the HD2 kernel itself; not an initrd payload |
| `BOOT-MAP.json` method | **Yes — as a format** | Its checkpoint/segment/provenance JSON is a good model for our own arm records |
| `startup.txt` (MAGLDR, `mtype 2524`) | **No** | Different loader; mi4 uses `fastboot boot` |

## Why this raises feasibility, and the one caveat

The HD2 proves that **the modern-XNU-on-ARM path is real and has produced a
booting iOS on a non-Apple, non-msm SoC**. That validates the project's whole
approach. But **the HD2's own caveats are hard**: `Hardware070Verified=false`,
`GuestBaseSDReadOnly=true`, `Limits` list a `launchd fault`, untested keyboard/
Calculator, and a *finite app-launch grace* — i.e. even the HD2 has not been
verified to *launch apps* on hardware. So the HD2 is evidence that **booting into
iOS is achievable**, not that a full app-capable iOS is proven on HD2. The mi4's
own 903/906 already sit at the same rung (kernel mounts storage, execs launchd).

## Next step

The single highest-value use of the HD2 lab is **the handed-map pattern**
(lesson 1) — the missing reader behind the unmet 3 GB clause. But the mi4's
handed map is the **Qualcomm DTB**, not HD2's ATAGs, so the next step is:

1. **Measure the memory map from the *handed* source, not SMEM.** On the running
   stock Android kernel read `/proc/device-tree/memory*/reg` and `/proc/iomem`
   (and `MemTotal`, already `2,935,868 kB`) to get the authoritative low/high
   bank base+size — this is free (no press) and settles the two-bank shape.
   Then, at payload entry, consume the **Qualcomm DTB** the same way (our
   `entry_arm_rtabi.s`/`xnu_entry_jump.c` currently take an opaque `boot_args`;
   the DTB PA is what aboot leaves behind, not an ATAG list).
2. Publish it in the `LeoMemoryObservation` **shape** (`bank_count`, `banks[]`,
   `managed_bytes`) as `xnu_live_*` keys; keep the SMEM probe as a cross-check.
3. Only then generalize the memory *admission* — the constants `entry_stubs.c`
   `ENTRY_DT_MEM_SIZE_MAX 0x40000000`, `MEM_SIZE_MAX 0x5e500000` (911b), and the
   911a/912a windows — into a **two-bank** union covering the full ~3 GB, so
   `memSize`/`sane_size`/`max_mem` reflect it.

The mi4's own 903/906 already sit at the HD2's rung (mount storage, exec
launchd), so the iOS-side layout is *not* the gap; **memory recognition is**.
See the plan in `docs/experiments/` (the 911 ATAG→DTB memory-map arm).