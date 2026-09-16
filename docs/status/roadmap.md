# Roadmap: XNU bring-up on MSM8974 — re-planned 2026-09-16

This document re-plans the project after Stage90. It supersedes the "Next milestones"
section of the root `README.md` and the framing in `method-c-progression-summary.md`,
both of which describe a plan that the Stage90 result invalidates.

It is written to be checked, not believed: every claim below is tied to a file in this
repository that can be read.

---

## 1. What the project has actually proven

These are hardware results. Each was validated by non-persistent `fastboot boot` and
recovered through `/proc/last_kmsg`, under the safety rules in the root `README.md`.

| Capability | Status | Evidence |
| --- | --- | --- |
| Non-persistent custom boot on a locked-down bootloader | ✅ | `docs/reference/boot-tooling.md`, QCDT `dt_size` requirement |
| Bare-metal ARMv7 payload, C runtime, exception vectors | ✅ | `experiment-03` … `experiment-07` |
| GIC (MSM8974 QGIC2) SGI + timer-PPI IRQ delivery | ✅ | `experiment-11`, `experiment-12`, `experiment-88` |
| ARMv7 two-level pmap (1 MB sections + 4 KB pages) | ✅ | `experiment-13`, `experiment-86` |
| High-virtual alias at `0x80000000` (`VA = 0x80000000 + PA`) | ✅ | `experiment-88` |
| Code execution, IRQ, data-abort and undef handlers at high VA | ✅ | `experiment-88` … `experiment-91` |
| Mach-O parsing and a loader that *can* materialize a fixture | ✅ | `experiment-92`, `stages/stage90/xnu_macho_loader.c` |
| Crash evidence survives a hang (`ram_console` at top of DRAM) | ✅ | `docs/reference/no-teardown-debugging.md` |

This is a real and unusually complete bring-up layer for a platform with no vendor
documentation. Nothing in this re-plan asks for it to be thrown away.

## 2. What the project has *not* done — despite the wording in earlier notes

Three things need stating plainly, because earlier notes read as if they were done and
future planning that assumes them would be wrong.

**Nothing from public XNU has ever executed on the device.** The object manifests say so
themselves, from Stage76 through Stage90 — e.g.
`stages/stage90/targets/cancro.stage90.objects`:

> `None of the public-XNU objects are linked into or executed by the booted StageNN payload.`

The entire public-XNU compile graph is five host-only objects: `pexpert/gen/{device_tree,
bootargs,pe_gen}.c` from `xnu-upstream`, and `pexpert/arm/{pe_bootargs,
pe_consistent_debug}.c` from `xnu-4570.1.46`. It is a *linkability proof*, valuable as a
host-side checklist and worth nothing as runtime evidence. Every `arm_init`-shaped,
`_start`-shaped and `arm_vm_init`-shaped symbol in the payload is Stage-owned code with an
XNU name. Phrases such as "public XNU `_start` executes" in
`method-c-progression-summary.md` (Stage76) refer to a Stage-owned stub called from a
Stage-owned `start.S`.

**The Stage90 handoff target is not executable code.** `xnu_macho_loader.c` resolves the
jump target as:

```c
if (found_entry) r->xnu_entry_va = r->entry_point_offset;   /* LC_UNIXTHREAD pc */
else             r->xnu_entry_va = r->text_segment_va;
```

`entry_point_offset` is the fixture's `LC_UNIXTHREAD` PC, which
`tools/mkmacho_fixture.py` writes as `VM_BASE` — `0x80008000`. That address is the Mach-O
*header*. The fixture's `__TEXT,__text` payload is the ASCII string `"ST90-TEXT-NOEXEC"`,
and the generator's own docstring calls the fixture "intentionally inert… it is never
executed by the target payload". So the handoff jumped to a magic number, and it could not
have started XNU.

**What that jump actually did is worse than "executed non-code", and it is worth stating
correctly** — an earlier revision of this section, and the README, said the device hung
because it executed ASCII. Checking the addresses shows otherwise:

- The handoff installs the candidate L1 first. In that table the high-VA window is built by
  `map_l1_page_table(l1, 0x80000000, l2_kernel)` followed by 256 × `map_l2_page(l2, va, pa)`
  with `va = 0x80000000 + page*4096` and `pa = 0 + page*4096`, i.e. **VA `0x80000000+X` maps
  to PA `X` for X < 1 MB**.
- So `0x80008000` resolves to PA `0x8000`.
- And `0x8000` is `_start` (`arm-none-eabi-nm` on the built payload: `00008000 T _start`,
  with `stage90_vectors` at `0x80a0`), because `linker.ld` sets `. = 0x00008000` and
  `KEEP`s `.text._start` first.

**The jump re-entered the payload's own entry point.** Not data, not ASCII — a restart.
That matters because `stage90_main` calls `log_init()` first, which resets the ram_console
(`rc->size = 0`), so a restart loop **wipes its own log on every iteration**. That is a much
better explanation of "no log output, no automatic recovery, manual power-cycle required"
than executing non-code, which would have produced a fault at the first instruction.

**Stated as inference, not evidence:** whether the original run was in fact this loop cannot
be confirmed without `last_kmsg`, which the hang prevented capturing. Two readings fit — the
loop above (candidate L1 installed, the normal FULL-mode path), or a prefetch abort if the
candidate install failed and the identity table was still live, since the identity table maps
only PA 0–2 MB so `0x80008000` is unmapped there. The loop is the better fit for the absence
of *any* log, because an abort would have been logged.

Either way it changes one thing downstream: **Phase 0's criterion (b) cannot be tested by
jumping to the fixture VA.** Under the candidate L1 that is a valid mapping to real code, so
the test would produce a boot loop rather than a logged fault. And the existing handoff
*structurally forbids* any other target — it requires the target to be inside the candidate
L2 window — so criterion (b) was untestable, not merely misdirected.

**Implemented:** `STAGE90_HANDOFF_FAULT_INJECT_VA` (default 0 = off). When set, the handoff
jumps at that VA instead of the Stage-owned alias, skips the target content check (so the
fault happens at the branch as a prefetch abort, reaching `stage90_exception_common`, which
logs PC/LR/SPSR and reboots — rather than a data abort from the check itself), and inverts
the window guard: a target *inside* the candidate L2 window is refused, because that is
exactly the boot-loop case this mode exists to avoid.

The default `0x80100000` is chosen to be a hole in **both** tables — the candidate L2 window
ends at `0x800fffff`, the RAM direct map starts at `0x80200000`, and the identity table maps
only PA 0–2 MB — so the test does not depend on which table is live. That removes the
ambiguity noted above.

Expected evidence in `last_kmsg`: an `exception pabort ... lr=0x80100000` line followed by
the `platform_reboot` sequence. A silent hang or a boot loop instead means the address *was*
mapped, and the run says so: the return path logs "returned from an UNMAPPED target".
Gated behind `--allow-fault-inject`.

**That is a test result, not only a defect.** The project's crash-visibility layer is what
made this findable at all; the missing piece is that a jump into non-code produced *no log*.
Turning that into a captured, logged, self-recovering fault is the first work item below —
and the fixture becomes a fault-injection test case rather than a fake XNU.

## 3. The three findings that reshape the plan

**F1 — The jump contract is wrong, not just the jump target.** Real ARM XNU entry
(`external/xnu-4570.1.46/osfmk/arm/start.s`) states its expectations in its first
instructions:

- the MMU is **off** on entry; `start.s` does `cpsid if` and enables the **I-cache**
- XNU itself writes `TTBR0` and `TTBR1` from `BA_TOP_OF_KERNEL_DATA` and sets `TTBCR`
- `r0` = `boot_args` physical address, `r1` = `cpu_data` physical address
- `SP` is loaded from `CPU_INTSTACK_TOP`

The current stage hands over the opposite of several of these: MMU on and running on a
Stage-built candidate L1, caches off, `r1` unused, and no `topOfKernelData` region
containing bootstrap page tables. Stage90's `arm_init` ladder is a *simulation* of the
sequence; the real entry point is not reachable from it by adding a jump.

**F2 — Every mapping is strongly-ordered, and there is no cacheable mapping anywhere in
the tree.** Both descriptor constants are unambiguous:

```c
#define L1_DESC_SECTION_SO 0x00010c02u   /* TEX=0 C=0 B=0 S=1 -> Strongly-Ordered, shareable */
#define L2_DESC_PAGE_SO    0x00000012u   /* TEX=0 C=0 B=0    -> Strongly-Ordered */
```

`mmu.c` then enables the MMU with `SCTLR.C` and `SCTLR.I` explicitly cleared, and Stage90
carries a status bit `..._SAT_NO_CACHE_POLICY_CHANGE` asserting that this never varies.
Grepping the whole stage for a Normal-memory descriptor returns nothing.

This is why the bring-up layer has been so reliable — everything is immediately visible,
so `ram_console` logging survives a hang, and there are no cache-coherency bugs to find.
It is also a hard ceiling:

1. XNU's `start.s` enables the I-cache within its first few instructions and assumes
   cacheable Normal memory for kernel text and data.
2. **ARMv7 `LDREX`/`STREX` are architecturally only defined on Normal memory.** On
   Strongly-Ordered/Device memory the exclusive monitor is unpredictable. The current pmap
   therefore cannot support a single lock, spinlock or atomic — and XNU takes locks
   immediately.
3. Any DMA-capable driver (the eMMC the kernel will need) requires correct
   Normal/Device attribute distinctions.

"Turn on caches" is not a performance tweak; it is the prerequisite for the kernel having
working atomics.

**F3 — There is no public iOS 6-era ARM XNU.** The checks were run against the checkouts in
`external/`:

| Tree | `osfmk/arm` |
| --- | --- |
| `xnu-2050.18.24` (Darwin 12 / iOS 6-era) | absent — i386/x86_64 only |
| `xnu-upstream`, `apple-xnu-rel-2050` | absent |
| `xnu-4570.1.46` (Darwin 14-era) | present — 90 files, plus `pexpert/arm` (6), `iokit` (70), `bsd` (409 `.c`) |

The iOS 6.1.3 OSS distribution contains userland projects, not the kernel. So a
source-derived kernel from this era is not obtainable, and `xnu-2050` can serve only as a
userland/ABI-era reference. Any XNU that actually runs on this phone will be
**4570-derived**. The project's own naming should stop implying otherwise.

## 4. Re-planned target

The goal is restated as: **get real, unmodified public XNU-4570 ARM code executing on
MSM8974, with the crash visibility to iterate.** "iOS 6" stays as historical motivation
and as the userland-era reference; it is not the deliverable and it is not the plausible
endpoint (§7).

Three tiers, so the achievable part is not held hostage to the unachievable part:

| Tier | Deliverable | Honest estimate |
| --- | --- | --- |
| **T1** | Real XNU code running on the device with self-recovering hangs, captured fault PC/LR/DFAR, and a documented boot_args / Apple-DT / attribute contract. | Weeks–months. Achievable with the existing layer. |
| **T2** | 4570's `arm_init` → `arm_vm_init` bootstrap proceeds past its own platform callouts and reaches a first scheduler tick, printing through a platform `kprintf`. | Months–a year+. Plausible with sustained work. |
| **T3** | A Darwin-like userland. Not iOS. | Years, and probably blocked on proprietary drivers (§7). |

Phases 0–3 below are identical under all three tiers. T1 is worth reaching on its own
terms: for a platform with no vendor kernel documentation, "real XNU instructions execute
and faults are captured" is a novel, publishable artifact.

## 5. Technical roadmap

Each phase has a hardware exit criterion. No phase advances on host-side evidence alone —
that rule is what this re-plan is *for*.

### Phase 0 — Make failure visible and recoverable (days)

*Why first:* the Stage90 hang consumed a manual power-cycle and produced zero information.
Every later phase will hang repeatedly, and iteration speed is set entirely by how much a
hang tells you. This is also the cheapest phase.

**Status (2026-09-16).** Two mechanisms are in the tree; one has been tried on hardware and
failed, and finding out why reshaped this phase.

- The harness that was supposed to isolate the hang could not report anything: the three
  0/1 switches shadowed each other, and the entry-stub bisect wrote synthetic results the
  loader preflight could never accept. Both are fixed (`STAGE90_HANDOFF_MODE`,
  `STAGE90_ENTRY_LADDER_LEVEL`).
- A `PREFLIGHT_WATCHDOG_ONLY` hardware run on 2026-09-16 **did not self-recover**: the device
  presented no USB in any mode for ~20 minutes and needed a power-button hold
  ([`docs/experiments/experiment-93-stage90-phase0-preflight-watchdog.md`](../experiments/experiment-93-stage90-phase0-preflight-watchdog.md)).
- Reading the reset path afterwards explained the gap: `platform_reboot()`/PS_HOLD is *proven*
  (experiments 03–78 all returned to Android automatically), but the sampling watchdog was
  only ever armed *inside* the handoff. The DT build, `boot_args`, pexpert discovery and the
  whole `arm_init` ladder ran with no recovery at all.
- Fixed by arming a **dead-man reset** at the end of `kernel_entry`'s GIC validation — before
  the loader preflight and everything after it — and re-arming it after the handoff returns.
  `STAGE90_DEADMAN_SELFTEST=1` spins forever after arming, making the dead-man the only route
  back to Android: the direct hardware proof, safe by construction.
- **But the dead-man has the same blind spot as the thing it protects.** It needs the GIC, the
  timer, IRQ delivery and the vector table, and it needs IRQs unmasked. If a hang is caused by
  any of those, it cannot fire — and a hang that leaves IRQs masked is not a contrived case.
  So the payload also arms the **MSM8974 hardware watchdog**
  ([`stages/stage90/hw_watchdog.c`](../../stages/stage90/hw_watchdog.c), on by default, 30 s):
  a hardware counter with no software involvement, whose base address and register
  programming come from the cancro device tree and the cancro kernel's own
  `msm_watchdog_v2.c`, and which is the same mechanism Android relies on to produce a
  readable `/proc/last_kmsg` after a panic. One accurate qualification: the vendor binding
  says the bite resets via the *secure* watchdog, so the independence claimed is from the
  payload's state, not from software entirely. `platform_reboot()` also forces an immediate
  bite after its PS_HOLD write, so the reboot no longer depends on the PMIC.
- `STAGE90_HANDOFF_MODE` now defaults to `HARD_SKIP`, and
  `stages/stage90/preflight_boot_check.sh` refuses to hand over a boot command for an image
  built with a mode the caller has not explicitly allowed.

Remaining in this phase:

- **Prove the hardware watchdog first.** `STAGE90_HW_WATCHDOG_SELFTEST=1` arms the SoC's own
  counter and spins forever; the device must come back to Android unattended (~30 s). Until
  this passes, every run below risks another manual power cycle — so it is the highest-value
  run available, and the 2026-09-16 hang is precisely the case it exists for.
- **Re-establish the known-good baseline.** Boot the default build (`HARD_SKIP`, both nets
  armed) and confirm it completes and reboots on its own.
- **Prove the dead-man** with `STAGE90_DEADMAN_SELFTEST=1` and confirm the device comes back
  to Android unattended (~60 s, the dead-man budget), then read the PC ring out of
  `/proc/last_kmsg`.
- Verify `VBAR` still points at the high-VA vectors **after** the candidate L1 install, and
  that a timer IRQ is still delivered through them — the interesting case is after the
  switch, not under the original mapping.
- The entry-validity guard is in: the handoff rejects a target equal to the fixture entry VA,
  an unaligned target, or one whose first word is the fixture's `__TEXT` marker. Still to
  demonstrate is criterion (b) below actually producing a logged fault.

#### The unvalidated stack, audited

Six changes are waiting on one run, so
[`unvalidated-change-audit.md`](unvalidated-change-audit.md) bounds each one's risk by reading
it and names the log line that confirms or clears it — ordered as the boot executes. Its
useful conclusions: Phase 1a is **inert** in the default build (verified to emit zero Normal
descriptors), the hardware watchdog's MMIO is in the same 1 MB section as the GIC read that
has preceded the payload's MMU setup in every successful run for ninety stages, and both the
DT and F-AM1 failures would be *visible and specific* rather than silent. It also lists what
it cannot bound: the watchdog's register semantics (what the selftest run is for), and the
`memSize` claim behind the conforming `boot_args`.

#### The queued runs, in order

`STAGE90_HANDOFF_MODE=HARD_SKIP` still runs the whole `arm_init` ladder — early pmap,
`PE_init_platform`, post-PE bootstrap, `arm_vm_init` with the candidate L1 install/verify/
restore, the high-VA handler windows and the Mach-O loader. It only stops short of the
three things that can hang: the candidate-L1 *switch*, the watchdog loop and the jump. So
each of these runs buys a lot for very little risk.

```bash
cd stages/stage90

# 1. Prove the hardware watchdog FIRST. It arms the SoC's own counter and then spins
#    forever, so nothing but the hardware countdown can bring the phone back (~30s).
#    Do this one before anything else: once it passes, every later run has a
#    guaranteed reset and stops costing a manual power cycle.
STAGE90_EXTRA_CFLAGS='-DSTAGE90_HW_WATCHDOG_SELFTEST=1' ./build.sh
./preflight_boot_check.sh --allow-hw-watchdog-selftest

# 2. Baseline + Phase 1 exclusives baseline, one safe boot with both nets armed.
#    Validates the repaired ladder end to end, the F-AM1 alias fix, and records what
#    LDREX/STREX do under the current strongly-ordered mapping.
STAGE90_EXTRA_CFLAGS='-DSTAGE90_EXCLUSIVE_PROBE=1' ./build.sh
./preflight_boot_check.sh

# 3. Prove the software dead-man separately (build without the hardware net so a
#    success is attributable to the dead-man alone).
STAGE90_EXTRA_CFLAGS='-DSTAGE90_DEADMAN_SELFTEST=1 -DSTAGE90_HW_WATCHDOG=0' ./build.sh
./preflight_boot_check.sh --allow-selftest
# 4. Only then, step the handoff mode up: PREFLIGHT_WATCHDOG_ONLY, then FULL.
```

The `fastboot` and log-capture steps are the same for all of them, and the gate prints them
too — but they are the part that, if mistyped, costs the run:

```bash
# in the same directory, with the image the gate just approved
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot boot "$PWD/../../out/stage90/stage90-qcdt.img"

# capture BEFORE anything else reboots the phone: ram_console lives at the top of DRAM
# and survives a warm reboot, but a power cycle clears it
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-last_kmsg.txt
grep -a -n 'MI4IOS6_STAGE90' /tmp/cancro-last_kmsg.txt | tail -60
```

`-s 4a2fe00b` is not optional: two devices are attached, and without it adb answers
`more than one device/emulator`. If the device does not come back at all, the recovery
procedure and the exact symptom to look for are in
[`../reference/recovery-and-rollback.md`](../reference/recovery-and-rollback.md) §4a.

`STAGE90_EXTRA_CFLAGS` is how to build a variant without editing `stage90.h`; the switches
land in `CFLAGS`, so `stage90-build-config.txt` records them and `preflight_boot_check.sh`
gates on what the image was actually built with. Rebuild without it to return to the default.

**Exit criteria:** (a) an induced hang self-recovers to Android without a manual
power-cycle; (b) a jump to the Stage90 fixture header produces a logged undef/abort with
PC/LR, not a hang; (c) a real Stage-owned function executes via the candidate L1 at high VA
and returns, with the IRQ handler still live afterwards.

### Phase 1 — Cacheable memory policy (1–3 weeks)

*Why:* F2. This is the wall between "an MMU demo" and "a kernel".

- Introduce Normal, cacheable, shareable descriptors for RAM and kernel text/data, keeping
  Device-nGnRnE for GIC, UART, timer and PS_HOLD. The attribute map must be written down,
  not inferred from constants, because every later fault will be blamed on it first.
- Turn the I-cache on first (XNU does), then the D-cache, with explicit clean/invalidate on
  the TTBR switch and after any code or page-table write — the loader materializes tables
  and copies segments at runtime, which Strongly-Ordered memory currently hides.
- **Do not enable the D-cache without fixing `ram_console` first.** `log_puts()` writes to
  `RAM_CONSOLE_BASE` and then issues `dsb sy; isb`. With caches off that is a complete
  guarantee; with the D-cache on it is not, and the log would sit dirty in the cache across
  `platform_reboot()` — so `/proc/last_kmsg`, the dead-man dump, and every diagnostic in
  Phases 2–4 would silently return stale or garbage data, and the loss would look like a
  payload failure. This must be a cache-clean in the write path, and the dead-man dump path
  deserves a full clean-and-invalidate of the log region before the reboot.
- Prove exclusive access works: a `LDREX`/`STREX` loop that increments a shareable counter
  reliably. This is the single test that distinguishes a working kernel pmap from the
  current one.

**Baseline prepared.** `stages/stage90/exclusive_probe.c` (switch `STAGE90_EXCLUSIVE_PROBE`,
default off) measures what exclusives do *today* under the current Strongly-Ordered mapping,
without changing any mapping or cache bit: it operates on one word of the payload's own
`.bss`. The discriminating sub-test is T3 — a plain store between `LDREX` and `STREX` clears
the exclusive monitor, so a real monitor must make that `STREX` fail. An implementation that
always reports success is indistinguishable from a working one unless T3 is checked, and
everything built on such a `STREX` would be silently wrong. This is the comparison point the
attribute-map change has to beat.

**Phase 1a implemented (2026-09-16), not yet on hardware.** `STAGE90_PMAP_ATTR_MODE`
(`SO_ONLY` default / `NORMAL_NC`) selects the memory type for DRAM mappings; MMIO stays
Strongly-ordered in both. `NORMAL_NC` is deliberately **non-cacheable** — it is the smallest
change that makes exclusives architecturally defined, and with no cache enabled it needs no
cache maintenance, no `ram_console` flush, and no change to how page-table writes become
visible.

What made it tractable was working out the real constraint, which is *not* "keep everything
Strongly-ordered" but **consistency per physical address**: the ARMv7 cache is physically
indexed, so two VAs mapping one PA with different memory types is UNPREDICTABLE. Listing every
mapping by PA — in [`docs/reference/pmap-attribute-map.md`](../reference/pmap-attribute-map.md)
— shows DRAM and MMIO form disjoint PA sets, so a single switch can move DRAM without
touching a device register. Two consequences that look like mistakes and are not: the page
tables become Normal (they live in `.bss` inside PA 0–2 MB and must not disagree with it; the
requirement is *non-cacheable*, which holds), and so does the payload's own code and stack.

`mmu.c`'s `ttbr_section_desc_for_pa()` classifies IMEM, the GIC block and PS_HOLD explicitly
rather than inheriting the caller's intent, because the TTBR0 roundtrip selftest builds a
recovery table containing both and installs it briefly — a GIC register mapped Normal in that
window, with a timer interrupt able to arrive during it, is exactly the kind of fault that
gets misattributed.

Verified off-device: SO_ONLY emits **zero** Normal descriptors, so the default build cannot
change any mapping's memory type; all mode × ladder × dead-man combinations compile clean;
`preflight_boot_check.sh` refuses `NORMAL_NC` without `--allow-attr-normal-nc` and handles
both the symbolic and the numeric (`-D`) form of every switch.

Still to do this phase: run the probe in both modes on hardware and confirm `monitor_tracks`
and `exclusives_usable` go to 1 under `NORMAL_NC`. Then caches — I-cache first, then D-cache,
with the `ram_console` clean that the D-cache makes mandatory.

**Exit criteria:** identity and high-VA mappings with caches on; `ram_console` still
logging; timer IRQ still delivered; a documented attribute map; a passing `LDREX`/`STREX`
test. Expect this phase to break the logging that made earlier stages easy — budget for it.

### Phase 2 — The iBoot-equivalent handoff contract (2–6 weeks)

*Why:* F1. XNU's entry reads state out of `boot_args` and writes page tables from it. Get
this wrong and the first real XNU instructions fault in ways that look like pmap bugs.

- Produce `boot_args` as `external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h` expects:
  `physBase`, `virtBase`, `memSize`, `topOfKernelData`, `deviceTreeP`/`Length`,
  `CommandLine`, `machineType`, `bootArgsVersion`, revision/version.
- Produce an Apple-format flattened device tree — root, `/cpus` with
  `timebase-frequency`, `/arm-io`, the interrupt controller, the timer — not the Android
  DT the bootloader hands over.
- Reserve and populate a `topOfKernelData` region containing the bootstrap page tables XNU
  will adopt, and validate our tables against what `start.s` writes.
- Validate by reading the structures back with 4570's own `PE_boot_args()` /
  device-tree reader code, on hardware, before anything jumps.

**Contract read off the source (2026-09-16).** [`docs/reference/xnu-handoff-contract.md`](../reference/xnu-handoff-contract.md)
works through `osfmk/arm/start.s` line by line. The shape of the requirement is not what the
bullet list above implies — XNU does **not** want a page table handed to it. It builds its own,
in the memory at `topOfKernelData`, while executing at physical addresses with the I-cache on
but the MMU off. Four consequences:

- `virtBase` must be the kernel's **link-time virtual base**, because `LOAD_PHYS_ADDR`
  (`start.s:108`) converts virtual to physical as `VA - virtBase + physBase`.
- `physBase` and `virtBase` must be **1 MB aligned**: `start.s:195-207` ORs the physical
  address straight into a section descriptor. A `physBase` of `0x8000` sets AP[2] and produces
  a descriptor with the wrong protection.
- `topOfKernelData` must be writable, 16 KB-aligned, inside `[physBase, physBase+memSize)` and
  above the image, with room for the L1, an L2, the trampoline and CPU tables.
- `memSize` must be real contiguous RAM containing the image.

Against that, the payload currently passes `virtBase = 0` and `physBase = 0x8000`, and
`physBase` is unaligned *because the boot image loads the payload at PA 0x8000*
(`--kernel_offset 0x00008000`). So the ladder's `boot_args` and a conforming XNU `boot_args`
are two different objects — the ladder's own validation requires `physBase == 0x8000` — and
Phase 2 is to produce the second alongside the first. The unaligned load is resolved either by
moving the load address to a 1 MB boundary (smaller change) or by relocating the image to the
DRAM base before handoff (what real iBoot does, and what a kernel expecting to own memory from
`0x80000000` will want).

**Implemented (2026-09-16), not yet on hardware.** `stages/stage90/xnu_boot_args_conformant.c`
builds and validates a conforming `boot_args` behind `STAGE90_XNU_BOOT_ARGS` (default off — it
produces a *second* object; the ladder's identity-based one is untouched, since the ladder
requires `physBase == 0x8000`).

The unaligned-`physBase` problem the contract doc first flagged turns out to dissolve:
`physBase = 0x00000000` is 1 MB aligned and the image at `0x8000` is simply inside
`[physBase, physBase + memSize)` — which is what the boot image's `kernel_offset` has always
meant. Nothing has to be relocated, and the choice agrees with the `VA = 0x80000000 + PA`
correspondence the project's own `full_pmap` already builds.

The two values that were guesses are now sourced from the device. `physBase = 0` is what
`CONFIG_PHYS_OFFSET=0x00000000` says the cancro kernel's own RAM base is, and `memSize` is
93 MB — the span from PA 0 to the first block the cancro device tree removes
(`msm8974.dtsi`'s `qcom,memblock-remove = <0x5d00000 ...>`), which is exactly 93 × 1 MB and so
stops the section map cleanly at the hole. The old 2 MB was itself a guess, just a small one;
a kernel given 2 MB cannot do anything.

`tools/check_boot_args_abi.py` closes the failure mode that has no symptom: `start.s` loads
`virtBase`/`physBase`/`memSize`/`topOfKernelData` by hand at fixed offsets, so a struct drift
means XNU silently reads the wrong word as the physical base of memory. The tool compares both
layouts field by field (and its own perturbation test confirms it reports a mismatch rather
than always passing); the payload carries `_Static_assert`s for the four hot offsets as well.

The device-tree half produced two real blockers, found by
`tools/xnu_dt_requirements.py` scanning XNU's own ARM lookups rather than by a device:
`/cpus/cpu@N` had no `state` property (XNU panics on it under `MACH_ASSERT` and *silently
skips the CPU* otherwise, so the timebase-frequency was being ignored), and there was no node
named `arm-io` (so `gPESocBasePhys` was 0 and neither the interrupt controller nor the timer
would ever be mapped). Both are fixed. Reading further into what `start.s` hands off to — the real
`PE_init_platform(FALSE, args)` and `pe_identify_machine` — turned up a third: our
`/interrupt-controller` node has no property *named* `interrupt-controller` with the value
`"master"`, which is the Apple convention `DTFindEntry` matches on, so `gPicBase` would stay 0
and `pe_arm_map_interrupt_controller` would return failure. **No interrupt controller at all.**

And a fourth, which the source-level scan could not see: `/timer` had no
`device_type = "timer"`, so the same function's timer lookup would also have failed. Found by
`tools/host_dt_check.sh`, which compiles XNU's *real* `pexpert/gen/device_tree.c` for the host
and walks our tree with it — the source scan can only see that a property *name* exists
somewhere, and `/arm-io` and the cpu nodes do have `device_type`, so it reported satisfied.
That check now runs as part of `stage90/build.sh`, and the property is emitted.

Worth stating plainly because it is the general lesson: the weak check passed and the strong
check failed, and the strong check was right.

And that led to the finding that actually shapes Phase 3. `pe_arm_map_interrupt_controller`'s
caller, `pe_arm_init_interrupts`, ends in `pe_arm_init_timer`, which is a chain of
`#if defined(ARM_BOARD_CLASS_*)` checks on `gPESoCDeviceType` — and the 32-bit ARM
`board_config.h` defines exactly three board classes (S7002, T8002, T8004), all Apple, with
**`return 0` as the fallthrough**. So on MSM8974 that function fails unconditionally,
whatever our device tree says; no device-tree value can change it.

Three consequences, and the first one matters for planning:

1. **Phase 3 must replace `pe_arm_init_interrupts` as a whole**, not patch the mapping helper
   inside it. There is no configuration in which the stock function succeeds here.
2. **The `reg` model is therefore not on the critical path** — the function returns before
   its computed address is used for anything. One fewer blocking decision than the previous
   paragraph assumed.
3. The protective absence of `interrupt-controller = "master"` is still right, for a sharper
   reason: `ml_io_map` is a real `io_map(...)` pmap operation, so adding it would install a
   mapping for the wrapped `0xf2000000` *before* the failure return.

So Phase 3's shape is now known: write an MSM8974 replacement for the ARM platform bring-up,
rather than trying to satisfy Apple's platform code through device-tree values. Larger, and
clearer. See the contract doc.

### Phase 3 — specification written

[`phase3-msm8974-shim-spec.md`](phase3-msm8974-shim-spec.md) bounds the shim work and makes
the hardware-specific values explicit before code is written. Two findings that shape it:

- **The stock `pe_arm_init_interrupts` cannot be configured into working on MSM8974** — the
  board-class set is closed (three Apple SoCs, `return 0` fallthrough) — so Phase 3 replaces
  it, which also means the `reg`-offset question stops mattering.
- **The ARM generic timer's physical timer is delivered on intr 19 on this device, not the
  architectural 30.** Withheld from the ARM ARM it would look like a broken GIC. The
  evidence is a hardware log (`experiment-12`), not reasoning.
- **`ml_init_timebase` is a pure registration function whose guard is `cpu_data_ptr ==
  &BootCpuData`** — passing anything else makes registration a *silent* no-op. The spec
  requires asserting the registration took rather than trusting the call.
- **XNU requires a platform FIQ handler on this build.** The FIQ vector slot
  (`locore.s:147`) branches to `r9` — the shim's `tbd_fiq_handler` — because `__ARM_TIME__`
  is used 16 times in the ARM tree and defined in none of them. There is no build in which
  XNU takes the timer as an IRQ, so the payload's IRQ-only experience does not transfer, and
  FIQ-on-MSM8974 (TrustZone interaction) becomes Phase 3's largest single risk. XNU's own
  `fleh_fiq_generic` shows the handler's contract, including that the EOI is literally a
  write of `int_value` to `int_address`, and that the generic `tbd_ops` leaves both
  decrementer callbacks NULL.
- **An open tension worth naming:** `fleh_fiq_generic` maintains a *software* timebase
  (TBL incremented per tick) while `__ARM_TIME_TIMEBASE_ONLY__` makes `ml_get_timebase` read
  the real `CNTPCT`. Those imply different timer programming — periodic versus free-running —
  so which is authoritative is the next reading task, not a detail.

**Exit criteria:** boot_args and DT dumped from the device and accepted by 4570's readers;
`TTBR0`/`TTBR1`/`TTBCR`/`SCTLR` verified correct after 4570 code has written them. Still open:
both checks are source-level so far, and the `reg` model decision is Phase 3's.

### Phase 3 — Platform shim layer and compile-graph expansion (months)

*Why:* this is the actual port, and it is currently ~0 % built. The five allowed objects
are a linkability proof; the payload has no XNU runtime.

- Expand the compile graph in dependency order: `pexpert/arm` (`pe_init`,
  `pe_identify_machine`, `pe_serial`, `pe_kprintf`) → the `osfmk/arm` pieces that do not
  need the scheduler (`start.s`, `arm_init`, `arm_vm_init`, `pmap`, `machine_routines`,
  `caches`, `cpu`, `exception`) → then whatever they call.
- Build the shim layer those functions need: a GIC-backed stand-in for Apple's AIC, a
  19.2 MHz `timebase` registration, `ml_*`/`PE_state` glue, and a `kprintf` that writes to
  the existing `ram_console` (no new debug channel needed — this one already works).
- Keep the host-side link proof as a gate: zero undefined symbols outside an explicit,
  reviewed shim list. Remove the four explicit exclusions in
  `targets/cancro.stage90.objects` (`osfmk/arm/{start.s,arm_init.c,arm_vm_init.c,pmap.c}`)
  one at a time, each with its shim list and hardware run.

**Exit criteria:** a real subset reaching `arm_vm_init` links clean, and executes on
hardware under Phase 0's crash capture.

### Phase 4 — First real XNU instructions (weeks of iteration)

Enter 4570's `start.s` with Phase 2's `boot_args`, Phase 3's objects mapped at high VA per
Phase 1's attributes, caches on, functions relocated — and the crash capture live.

**Exit criteria:** crash #1 within the first few hundred instructions, *captured*: PC, LR,
DFAR, CPSR and the exact missing symbol or service. Then fix one thing per cycle. "It
hangs" is a failed experiment; a captured fault is a successful one.

### Phase 5 — Kernel subsystems and drivers (years)

Scheduler and threads, VM, IPC, IOKit registry, then MSM8974 drivers beyond the UART, GIC
and timer already working: eMMC, USB, PMIC, display. Each driver is its own project against
a SoC with no public documentation for the parts that matter. Expect to spend most of the
remaining calendar time here, and to reach a kernel that boots and prints long before one
that mounts a filesystem.

## 6. Process re-plan

The stage-copy convention has done its job and has stopped paying for itself.

**What to keep.** Non-persistent boot, fail-closed switches, `ram_console` evidence, the
existing hardware-proven payload, the six retained snapshots, `tools/stage-archive.sh`, and
the discipline of validating every claim on the device.

**What to change.** A complete copy per experiment was the right shape for 2-file deltas on
a tested base. It is the wrong shape for Phases 1–3, where the work is thousands of lines
in a shared platform layer and the interesting question is always "what changed relative to
the working baseline". Concretely:

1. Keep `stages/stage90/` as the frozen, hardware-validated base and stop copying it per
   experiment. New work evolves a single tree, with the Phase 1/2/3 feature switches and a
   `make` target per milestone.
2. Record each hardware-validated milestone as a **git tag plus one entry in the milestone
   log (`docs/history/milestones.md`)** — what was proven, the `last_kmsg` evidence, the
   commit. That is what a snapshot was for; a tag carries the same information without
   3 200 duplicated files.
3. **Retire the dry-run contract pattern.** 16 of Stage90's 58 `.c` files (~11 100 of
   ~39 300 lines) are `*_dryrun*` contracts. They were a legitimate device for safely
   deferring real work at a time when nothing could execute, but they are not evidence, and
   at this scale they are where the effort went instead of into executable tests. Phases 1
   onward get executable tests with hardware results, or nothing.
4. Rename the goal in the root `README.md` from "iOS 6 adaptation" to "XNU/Darwin bring-up
   on MSM8974", keeping iOS 6 as historical context. The current title sets an expectation
   the project cannot meet and hides the one it can.

## 7. Non-goals, stated plainly

- **Stock iOS 6 userland: not achievable.** It needs Apple's boot chain, Apple's drivers,
  AMFI/code signing, the dyld shared cache, and proprietary UIKit/SpringBoard/
  CoreAnimation — none of which exist for MSM8974.
- **An iOS-6-era kernel: not obtainable.** F3: no public ARM XNU from that era exists.
- **Any persistent write to the device: out of scope**, unchanged from the root `README.md`.
- **T3 (a userland) is not planned here.** If it is ever attempted it will be a
  Darwin-like userland of our own, on top of a kernel we control — not iOS.

Failure is not the same as no result: the bring-up method (non-persistent ARMv7 bring-up on
a locked bootloader, register-level MSM8974 findings, QCDT/boot-image constraints, hang
forensics through `ram_console`) is documented and reusable regardless of how far T2 gets.

## 8. Immediate next actions

In order, none of them requiring a new stage directory:

1. Finish the self-recovering watchdog: prove PS_HOLD warm reboot from an induced hang
   (Phase 0).
2. Add the post-switch visibility checks and the non-executable-entry guard; re-run the
   Stage90 jump and confirm the fixture header now produces a captured fault (Phase 0).
3. Write down the current attribute map, then introduce the first Normal, cacheable,
   shareable mapping and the `LDREX`/`STREX` test (Phase 1).
4. Only after 1–3: revisit what the handoff target may do.

## 9. Decision needed

One strategic choice, and it does not block Phase 0–2:

- **T1 only** — stop at "real XNU code executes, faults are captured, the contract is
  documented and published". Weeks–months, high confidence.
- **T2** — commit to the shim layer and driver work as an open-ended project. Phase 3 is
  where the cost curve turns vertical; the estimate is months to a year+ with no guarantee
  of a booting kernel.

The recommendation is to fund T1 to completion, publish it, and treat T2 as a separate
decision made after Phase 2's results are in hand — by which point the remaining cost will
be measurable rather than estimated.
