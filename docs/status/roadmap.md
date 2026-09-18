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
| Mach-O parsing in the payload's loader | ✅ | `experiment-92`, `experiment-94`. **Not** materializing a fixture: the copy path refuses in every mode — see §5 Phase 0 |
| Crash evidence survives a hang (`ram_console` at top of DRAM) | ✅ | `docs/reference/no-teardown-debugging.md` |
| A hung payload resets the device itself, with no power press | ✅ | `experiment-94` runs 3 and 6 (MSM8974 hardware watchdog, `stages/stage90/hw_watchdog.c`) |
| MSM8974 hardware watchdog: arm, and read its live countdown back | ✅ | `experiment-94` runs 1–3 |
| Software dead-man reset (GIC + timer + IRQ path), proved alone | ✅ | `experiment-94` run 8 |
| Candidate-L1 install followed by a jump, with the fault captured | ✅ | `experiment-94` run 10 (`FULL` + fault injection) |
| Working `LDREX`/`STREX` (the exclusive monitor tracks) | ✅ | `experiment-96`, measured in four configurations |
| Cacheable Normal DRAM with both caches on | ✅ | `experiment-97` (I-cache), `experiment-98` (I+D) |
| Conforming `boot_args` checked on the device | ✅ | `experiment-99` (`xnu_ba_checks=10`, `failures=0`) |
| Public-XNU code executing on the device | ✅ | **all five** pexpert objects by `experiment-105` |
| **XNU's real `_start` executing, with its own page tables and MMU** | ✅ | `experiment-106` — the entry sequence ran to completion and branched to `arm_init` |
| **XNU's real `arm_init` executing on the device** | ✅ | `experiment-159` — `osfmk/arm/arm_init.c` compiled, linked, run; it stopped at `cpu_data_init`, which the image does not contain |
| A public-XNU subsystem doing work, not just observing | ✅ | `experiment-102`: the consistent-debug registry inherits, enables, allocates and writes a record |
| XNU console output landing in the device's crash log | ✅ | `experiment-105`: 62 bytes through `PE_putc`, read back from `/proc/last_kmsg` |
| XNU acting on this payload's boot arguments | ✅ | `experiment-105`: `pe_init_debug` parses `debug=0x144`, `PE_enter_debugger` acts on it |
| MSM8974 replacement for `pe_arm_init_interrupts`, verified on hardware | ✅ | `experiment-101` (`checks=8`, `failures=0`); **no caller yet** |

This is a real and unusually complete bring-up layer for a platform with no vendor
documentation. Nothing in this re-plan asks for it to be thrown away.

## 2. What the project has *not* done — despite the wording in earlier notes

Three things need stating plainly, because earlier notes read as if they were done and
future planning that assumes them would be wrong.

**XNU's own `_start` executes on the device (2026-09-17,
[`experiment-106`](../experiments/experiment-106-xnu-start-executes.md)).** `STAGE90_XNU_ENTRY=1`
copies a linked image containing XNU's real `osfmk/arm/start.s` to PA `0x00200000`, hands it a
`boot_args` with `physBase == virtBase`, and jumps. XNU then does what the source says: enables the
I-cache, reads `boot_args` at the offsets the ABI check guards, patches its exception vectors,
writes TTBR0/TTBR1/TTBCR, builds a V=P section and a `memSize`-sized kernel mapping at
`topOfKernelData`, cleans and invalidates both caches by set and way, programs DACR/PRRR/NMRR and
SCTLR with TEX remap and high vectors, enables the MMU, flushes the TLB, enables VFP and branches
to `arm_init`. The log's last line is written by the entry image with the MMU off, because
`ram_console` is outside the 2 MB window XNU maps.

**What that does not mean:** when this paragraph was written, `arm_init` was a Stage-owned stub, so
everything after it in a real kernel — `arm_vm_init`, `machine_startup`, the scheduler — did not
exist here. **XNU's entry point runs; XNU does not run.** That has moved once since:
[`experiment-159`](../experiments/experiment-159-the-real-arm-init-ran.md) replaced the stub with
XNU's own `arm_init.o`, and the real function now executes on the device until it calls
`cpu_data_init` — the first thing the image does not contain. **XNU's first real function runs; XNU
still does not run**, because the image is deliberately the size of one function's needs. How big it
has to be is the open question, and `experiment-158` is the answer to the neighbouring one: the
closure of `arm_init` is the whole kernel, so the image has to grow by the kernel or not at all.

**And before that, the whole of this section was true — for the record:** The object
manifests said so themselves, from Stage76 through Stage90 — e.g.
`stages/stage90/targets/cancro.stage90.objects`:

> `None of the public-XNU objects are linked into or executed by the booted StageNN payload.`

**That changed with [`experiment-100`](../experiments/experiment-100-first-public-xnu-execution.md):**
behind `STAGE90_XNU_REAL_DT=1` the payload links `xnu-objects/device_tree.o` — Apple's own
`pexpert/gen/device_tree.c`, unmodified — and runs `DTInit`, `DTLookupEntry`, `DTFindEntry` and
the entry/property iterators over the payload's tree. It found `/cpus` and its four children,
read each `state` and cpu0's `timebase-frequency` (19200000, correct), located `/arm-io` by
property value and `/timer` by `device_type`, and reported `failures=0`. It also *measured* the
`reg`-model gap: XNU's `soc_phys + reg[0]` evaluates to `0xf2020000` for a timer that is at
`0xf9020000`. The rest of this section is still true of the other four objects and of XNU proper.

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

**Run 2026-09-17 (experiment-94 run 10): the expected evidence, exactly.** The log shows
`installing candidate L1 for high-VA handoff` (`ttbr0 0x000b8000 → 0x0010c000`), the jump, then
`exception: prefetch-abort lr=0x80100000 spsr=0x60000113` and `platform_reboot`. The logged `lr`
is the faulting PC, not a return address — `vector_prefetch_abort` converts ARM's prefetch-abort
`LR = PC + 4` back with `sub lr, lr, #4` — so the handler printed the address the jump went to.

**Premises verified against the mapping code before writing any of it into a run:**

| Claim | Check | Result |
| --- | --- | --- |
| `0x80100000` is a hole in the candidate L1 | enumerated every `map_l1_section_*` / `map_l2_page` call: the L2 window ends at `0x800fffff`, the RAM direct map begins at `0x80200000` | hole |
| …and in the identity table | enumerated `build_identity_table()`: it maps only PA 0–2 MB and the MMIO windows | hole |
| The abort vector is reachable *after* the candidate L1 is installed | `VBAR = 0x80a0` (start.S), candidate L1 maps section 0 `VA 0x00000000 → PA 0` | reachable |
| The handler can log | `RAM_CONSOLE_BASE 0xde500000` is identity-mapped in the candidate L1 | yes |
| The handler can reboot | `0x0fa00000` (IMEM) and `0xfc400000` (PS_HOLD) are mapped in the candidate L1 | yes |

That third row is the non-obvious one and the reason this check was worth doing: the abort
happens **while the candidate L1 is live**, so everything the exception handler touches —
vector table, ram_console, IMEM, PS_HOLD — must be mapped *in the candidate table*, not just
in the identity table it replaced. They are, but nothing had established it before this;
Stage87 validated the IRQ path under the candidate L1, not the abort path.

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
2. ~~**ARMv7 `LDREX`/`STREX` are architecturally only defined on Normal memory** … the
   current pmap therefore cannot support a single lock, spinlock or atomic.~~
   **CORRECTED 2026-09-17 — measurably false on this device.** The claim was that
   Strongly-ordered memory makes the exclusive monitor unpredictable; `experiment-96` measured
   `LDREX`/`STREX` on MSM8974 with the MMU off, with Strongly-ordered DRAM, with
   Normal-Non-cacheable DRAM, and with Normal-Write-Back plus the D-cache on — and the monitor
   tracks in all four. Locks are **not** blocked by the memory type here, and Phase 1's
   "working `LDREX`/`STREX`" criterion was met before any of this work started. The item was
   wrong for a second reason worth keeping: it was quoted from the architecture rather than
   measured, and the probe that was built to measure it encoded the same premise in its own
   discriminator, so it reported failure for four hardware runs.
3. Any DMA-capable driver (the eMMC the kernel will need) requires correct
   Normal/Device attribute distinctions.

"Turn on caches" is therefore not the prerequisite for working atomics — atomics already work —
but it is still required, for reason 1 and reason 3: XNU's entry code enables the I-cache on its
first instructions and will allocate and use memory with its own cacheable attributes, and any
driver needs the Normal/Device split to be right. The attribute work is the groundwork for that,
not for locks.

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

**Status (2026-09-17). All three exit criteria are met, and the phase is closed.** Ten hardware
runs on 2026-09-17
([`docs/experiments/experiment-94-stage90-phase0-watchdog-and-baseline.md`](../experiments/experiment-94-stage90-phase0-watchdog-and-baseline.md)):

- **(a) met.** `STAGE90_HW_WATCHDOG_SELFTEST=1` was run three times. Runs 1 and 2 returned the
  device to Android on their own but reported the watchdog as not armed — two real bugs, a
  20-bit register truncating a 30 s timeout and a counter that counts up, not down. Run 3
  reported `armed and counting` and the SoC's own counter, not the 90 s fallback, is what reset
  the device. It is met a second time on an *uninduced* hang: the baseline run below hung in
  the Mach-O loader and the watchdog reset the device ~28 s later, no manual power-cycle.
- **The baseline is green.** `HARD_SKIP` + `-DSTAGE90_EXCLUSIVE_PROBE=1` reached
  `kernel_entry returned success` on its fourth attempt, after the run found a device-tree
  child count checked against a literal `19u` in two places in `mmu.c`, and a Mach-O loader
  that wrote through an unmapped VA — see below.
- **(c) met.** The same run: `high_va_code_exec_fn_called=1` at `fn_high_va=0x80048098` with
  `fn_result_correct=1`, the high-VA IRQ handler delivering timer IRQs (`irq_count 1 → 3`), and
  VBAR restored to `0x000080a0` afterwards.
- **The software dead-man is proved on its own** (run 8): built with `STAGE90_HW_WATCHDOG=0`, so
  the hardware net could not be the thing that returned the device. Timer IRQs preempted the
  spin, the interrupted PC was dumped — `addr2line` resolves the samples to the selftest's own
  call site and callee, so the dump is real — and `platform_reboot()` returned the device
  unattended. What it does not show is that the dead-man fires on hangs that take the GIC or the
  vector table out; that is what the hardware watchdog is for.
- **(b) met** (run 10). `FULL` + `STAGE90_HANDOFF_FAULT_INJECT_VA=0x80100000u`: the candidate L1
  is installed (`ttbr0 0x000b8000 → 0x0010c000`), the jump is taken, and the fault is
  *captured* — `exception: prefetch-abort lr=0x80100000` (the vector converts ARM's `LR = PC+4`
  back to the faulting PC, so that is the address the jump went to) — followed by
  `platform_reboot`. No hang, no boot loop. **This is also the first candidate-L1 install and
  jump since Stage90's original hang, and it recovered.**
- **The step up out of `HARD_SKIP` has started** (run 9): `PREFLIGHT_WATCHDOG_ONLY` completes the
  ladder, skips the candidate L1 install, arms the sampling watchdog, dumps and reboots — and the
  device returns unattended. That is the same configuration that went dark on 2026-09-16, now
  with an explanation: the loader preflight runs *before* the preflight watchdog is armed, and
  this build had no net in front of it. See experiment-93's update note.

**Status (2026-09-16).** Two mechanisms were in the tree; one had been tried on hardware and
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

Remaining in this phase: **nothing — closed 2026-09-17.** What was on this list, and how it went:

- ~~Prove the hardware watchdog first.~~ **Done** (run 3), after runs 1 and 2 found two real
  register-semantics bugs. Hangs now have a reset that does not depend on the payload's own
  state, so later runs stop costing a power press — which was the point.
- ~~Re-establish the known-good baseline.~~ **Done** (run 7): the default build arms both nets,
  completes the ladder and reboots itself.
- ~~Verify `VBAR` still points at the high-VA vectors after the candidate L1 install.~~ **Done**
  as a side effect of the baseline run, and again in run 10 under `FULL`.
- ~~Prove the dead-man~~ **Done** (run 8), built with `STAGE90_HW_WATCHDOG=0` so a success was
  attributable to it alone: it fired at its 60 s budget, dumped the interrupted PC and rebooted.
- ~~The entry-validity guard, and criterion (b) producing a logged fault.~~ **Done** (run 10).
- ~~Step the handoff mode up~~ **Done** (runs 9 and 10). `FULL` + fault injection was the first
  candidate-L1 install and jump since Stage90's original hang, and it recovered.

**One thing this phase did not establish, found while writing it up:** the Mach-O loader's copy
path is unreachable in every mode. The loader preflight runs inside the `arm_init` ladder, and
the candidate L1 is installed later in `stage90_xnu_handoff_run`, so `segments_loaded` is 0 on
all ten runs — the fail-closed check is right that the destination is unmapped at that moment,
but the consequence is that "the loader can materialize a fixture" is no longer something any
current mode does (§1's table is corrected). Fixing it means installing the candidate L1
*before* the ladder's loader step, which is Phase 2 work.

#### The unvalidated stack, audited

Six changes were waiting on one run, so
[`unvalidated-change-audit.md`](unvalidated-change-audit.md) bounds each one's risk by reading
it and names the log line that confirms or clears it — ordered as the boot executes. Its
useful conclusions: Phase 1a is **inert** in the default build (verified to emit zero Normal
descriptors), the hardware watchdog's MMIO is in the same 1 MB section as the GIC read that
has preceded the payload's MMU setup in every successful run for ninety stages, and both the
DT and F-AM1 failures would be *visible and specific* rather than silent.

**That run has now happened** (2026-09-17, run 7 of experiment-94): all five live changes are
exercised and the boot reaches `kernel_entry returned success`. It also, usefully, falsified
the DT row's optimism — a device-tree miscount *was* the first failure, and it was visible and
specific, but it surfaced four steps away in the MMU high-bootstrap selftest rather than where
the count is defined. What the audit could not bound and still cannot is the `memSize` claim
behind the conforming `boot_args`, which is inert until `STAGE90_XNU_BOOT_ARGS=1`.

#### The queued runs, in order

`STAGE90_HANDOFF_MODE=HARD_SKIP` still runs the whole `arm_init` ladder — early pmap,
`PE_init_platform`, post-PE bootstrap, `arm_vm_init` with the candidate L1 install/verify/
restore, the high-VA handler windows and the Mach-O loader. It only stops short of the
three things that can hang: the candidate-L1 *switch*, the watchdog loop and the jump. So
each of these runs buys a lot for very little risk.

```bash
cd stages/stage90

# 1. Prove the hardware watchdog FIRST. [DONE 2026-09-17 - passed on the third attempt;
#    runs 1 and 2 found two real bugs. It arms the SoC's own counter and then spins in a
#    BOUNDED loop: the watchdog should reboot the phone at ~28s, and if it does not the
#    bound reboots it at ~90s via PS_HOLD. So this run cannot leave the phone dark either
#    way, and the time it takes IS the result.]
STAGE90_EXTRA_CFLAGS='-DSTAGE90_HW_WATCHDOG_SELFTEST=1' ./build.sh
./preflight_boot_check.sh --allow-hw-watchdog-selftest

# 2. Baseline + Phase 1 exclusives baseline, one safe boot with both nets armed.
#    [DONE 2026-09-17 - green on the fourth attempt, after the run found the two bugs
#    fixed in a683c9d. The exclusive-probe numbers are in experiment-94 Part 3.]
#    Validates the repaired ladder end to end, the F-AM1 alias fix, and records what
#    LDREX/STREX do under the current strongly-ordered mapping.
STAGE90_EXTRA_CFLAGS='-DSTAGE90_EXCLUSIVE_PROBE=1' ./build.sh
./preflight_boot_check.sh

# 3. Prove the software dead-man separately (build without the hardware net so a
#    success is attributable to the dead-man alone). [DONE 2026-09-17, run 8 of
#    experiment-94: the dead-man fired at its 60s budget, dumped the interrupted PC
#    and rebooted; device returned unattended.]
STAGE90_EXTRA_CFLAGS='-DSTAGE90_DEADMAN_SELFTEST=1 -DSTAGE90_HW_WATCHDOG=0' ./build.sh
./preflight_boot_check.sh --allow-selftest
# 4. Step the handoff mode up: PREFLIGHT_WATCHDOG_ONLY [DONE 2026-09-17, run 9 of
#    experiment-94: the preflight watchdog loop armed, dumped and rebooted; the device
#    returned unattended, on the configuration that went dark on 2026-09-16], then FULL.
#
#    FULL + STAGE90_HANDOFF_FAULT_INJECT_VA=0x80100000u with
#    --allow-full --allow-fault-inject [DONE 2026-09-17, run 10]: the candidate L1 was
#    installed, the jump was taken, and the prefetch abort was logged and recovered from.
#    That was Phase 0 exit criterion (b), and Phase 0 is now closed.
```

The `fastboot` and log-capture steps are the same for all of them, and the gate prints them
too — but they are the part that, if mistyped, costs the run:

```bash
# One command does gate + boot + wait + capture + summarise. It never flashes, and it
# captures /proc/last_kmsg before touching the device again. Exit 2 means the device did
# not come back and needs a power press. The marker summary says what the counts mean, so
# a fresh failure does not have to be interpreted from scratch.
./run_and_capture.sh --allow-hw-watchdog-selftest

# Re-read a log captured earlier, or in another shell. Touches nothing.
./run_and_capture.sh --summarise /tmp/cancro-last_kmsg.txt

# or, by hand, in the same directory, with the image the gate just approved:
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

**(a), (b) and (c) are all met on hardware as of 2026-09-17** — runs 3 and 8, run 10, and run 7
of [`experiment-94`](../experiments/experiment-94-stage90-phase0-watchdog-and-baseline.md)
respectively. (b) was tested against a deliberate hole rather than the fixture header, which is
what the fixture-header version of the test would have proved anyway once the mapping analysis
in §2 showed that address is real code under the candidate L1.

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
- ~~Prove exclusive access works~~ **Done, and it never needed proving.** `LDREX`/`STREX` work on
  this device; see the F2 correction in §3 and
  [`experiment-96`](../experiments/experiment-96-stage90-phase1-exclusives-work.md). A
  `LDREX`/`STREX` increment loop is reliable today.

**Baseline measured, then corrected (2026-09-17).** `stages/stage90/exclusive_probe.c` (switch
`STAGE90_EXCLUSIVE_PROBE`, default off) measures what exclusives actually do here, on one word of
the payload's own `.bss`.

The first design had two defects that between them produced four runs of false negative, and
both are worth remembering because each is a *method* error rather than a coding slip:

- The discriminating test was T3 — "a plain store between `LDREX` and `STREX` clears the monitor,
  so `STREX` must then fail". That premise is **false on MSM8974/Krait**. The probe asserted it
  rather than testing it, and so reported `exclusives_usable=0` everywhere.
- The probe was called before `enable_identity_mmu()`, i.e. with the MMU off, and did not record
  that. With the MMU off no descriptor applies at all, so the `SO_ONLY` vs `NORMAL_NC` comparison
  — the entire point of Phase 1a — compared two builds running the same meaningless configuration.

The probe now records `sctlr`, `ttbr0` and `mmu_enabled`; re-runs the tests after the MMU is on
(phase 2) and once more on a cacheable mapping with the D-cache on (phase 3); and uses **CLREX**
as its discriminator (T5), which is the mechanism XNU uses (`clear_exclusive()` in
`osfmk/arm/atomic.h`). The corrected result, consistent across every configuration measured:

```
              MMU   memory type              T2   T3   T5   monitor_clears  exclusives_usable
phase 1       off   (no descriptor applies)   0    0    1    1               1
phase 2       on    Strongly-ordered          0    0    1    1               1
phase 2       on    Normal, Non-cacheable     0    0    1    1               1
phase 3       on    Normal, Write-Back, D$ on 0    0    1    1               1
```

T5 failing is the positive result: `LDREX` set the monitor, `CLREX` cleared it, `STREX` refused.
T3 is now reported as an observation, not a criterion.

Also found on the way: **`ACTLR` reads 0 and ignores writes** (`actlr_after=0` after writing
bit 6), so `ACTLR.SMP` — which ARMv7 requires before enabling caches for coherence — cannot be
set from non-secure PL1 here; and the payload's first ~150 log lines run with **the MMU off**,
because aboot hands over with `SCTLR.M=0` and `TTBR0=0x0f210000` (its own table, never ours).

**Phase 1a run on hardware (2026-09-17, [`experiment-95`](../experiments/experiment-95-stage90-phase1a-normal-nc.md)).**
`NORMAL_NC` now boots end to end — `kernel_entry returned success`, zero failed checks, DRAM
descriptors at `0x00011c02`/`0x00000452`, `ram_console` verified and timer IRQs delivered through
Normal-mapped memory. The whole bring-up layer runs on Normal memory for the first time. Three
descriptor literals had to be made mode-dependent first (`experiment-95` § "What changed in
response") — the same one-value-two-definitions defect as the device-tree child count.

(That log also concluded that the attribute change did not fix exclusives. It does not say
anything about exclusives at all: the probe was measuring with the MMU off. See
[`experiment-96`](../experiments/experiment-96-stage90-phase1-exclusives-work.md) and the
corrected baseline above.)

**Phase 1a implemented (2026-09-16).** `STAGE90_PMAP_ATTR_MODE`
(`SO_ONLY` default / `NORMAL_NC`) selects the memory type for DRAM mappings; MMIO stays
Strongly-ordered in both. `NORMAL_NC` is deliberately **non-cacheable** — the smallest change
that moves DRAM to Normal, needing no cache maintenance, no `ram_console` flush, and no change
to how page-table writes become visible. (Its original rationale was "makes exclusives
architecturally defined"; exclusives turned out not to need it, but it is the stepping stone to
cacheable DRAM, which XNU's entry code does need.)

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

**The I-cache is on (2026-09-17, [`experiment-97`](../experiments/experiment-97-stage90-phase1-icache.md)).**
`STAGE90_PMAP_ATTR_MODE = NORMAL_WB` + `STAGE90_CACHE_MODE = ICACHE` gives DRAM the Normal
Write-Back Write-Allocate descriptors (`0x0001140e`/`0x0000045e`, MMIO unchanged) and sets
`SCTLR.I` at MMU-enable time after an `ICIALLU` — XNU's own order. The run is green: no failed
check, `ram_console` verified, timer IRQs delivered, `SCTLR` `0x00c5487a → 0x00c5587b`. The
payload now runs with a cache enabled for the first time in ninety-seven stages.

It also found the **fourth** instance of one-value-two-definitions — a literal `0u` standing for
"the caches are off" in nine places between `mmu.c` and `xnu_pmap_bootstrap_contract.c`, so the
build failed its own pmap contract for being correctly configured. One definition now:
`STAGE90_EXPECTED_CACHE_POLICY`. The cmdline's `no-cache-change` token follows the build too.

F-AM2 is fixed in passing: the WB section descriptor is AP `0b01` (PL1-only) where the
Strongly-ordered one was full access.

**The D-cache is on, and Phase 1 is closed (2026-09-17,
[`experiment-98`](../experiments/experiment-98-stage90-phase1-dcache.md)).**
`STAGE90_CACHE_MODE = ICACHE_DCACHE` sets `SCTLR.I` and then `SCTLR.C` — the second in its own
write, after the MMU is already on, and after a whole-cache clean-and-invalidate. The run is
green: `SCTLR` `0x00c5487a → 0x00c5587f`, `ram_console` verified, timer IRQs delivered, every pmap
contract satisfied, and a complete log.

The maintenance the roadmap called for was met with one deliberate substitution and two
additions. **`ram_console` is mapped Normal-Non-cacheable even under `NORMAL_WB`** rather than
having a clean added to its write path: its PA range is disjoint from every other PA this payload
maps, so a different type violates nothing, and a crash log that is never in the cache cannot be
lost by a reboot path that failed to clean it. Page tables get an explicit clean before each of
the three places that publish one (`full_pmap`, the TTBR0 roundtrip, the exclusive probe), because
the MMU's table walk does not read the D-cache. And `platform_reboot()` cleans and invalidates the
whole D-cache first, as belt and braces rather than as the mechanism.

Not shown by this: any speedup (none was measured), the maintenance under load (the payload is
small and mostly sequential), or I-cache coherency after a code write (nothing writes code at
runtime, and the loader's header records what that path will have to do).

**Exit criteria — all five met, 2026-09-17:** identity and high-VA mappings with caches on
(experiment-98: `SCTLR.C` and `.I` set, every mapping verified under them); `ram_console` still
logging (`ram_console_verified=1`, and the log the run was read from); timer IRQ still delivered
(`irq_count 1 → 3`); a documented attribute map (`docs/reference/pmap-attribute-map.md`, three
modes, decoded rather than guessed); a passing `LDREX`/`STREX` (experiment-96 — which turned out
never to have needed the attribute work). The phase's warning that the cache work would break the
logging turned out not to apply, because the log is mapped non-cacheable by design.

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

**Run on hardware (2026-09-17, [`experiment-99`](../experiments/experiment-99-stage90-phase2-boot-args-on-hardware.md)).**
With the Phase 1 caches on, the module reports `xnu_ba_checks=10`, `xnu_ba_failures=0` and
"conforms to the XNU entry contract" against the real `__stage90_image_end`, and the payload still
ends `kernel_entry returned success`. The `memSize` claim stays source-derived and is not
something a payload run can validate; what is now hardware-exercised is the rest of the contract
holding on the real image. What remains in this phase: the `topOfKernelData` bootstrap tables, and
running 4570's own readers *on the device* rather than on the host.

**Implemented (2026-09-16).** `stages/stage90/xnu_boot_args_conformant.c`
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

`tools/check_xnu_struct_abi.py` closes the failure mode that has no symptom: `start.s` loads
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

### Phase 4 — feasibility checked, and the build is gated by Apple's own whitelist

Phase 4 says "enter public XNU `_start` / build a full `mach_kernel`". Before planning that,
one question is worth answering first: **can the open-source tarball build an ARM kernel at
all?** It cannot, and the reason is concrete.

`external/xnu-4570.1.46/makedefs/MakeInc.def:12`:

```make
SUPPORTED_ARCH_CONFIGS := X86_64 X86_64H
```

and `makedefs/MakeInc.kernel:13`:

```make
ifeq ($(filter $(CURRENT_ARCH_CONFIG),$(SUPPORTED_ARCH_CONFIGS)),)
$(error Unsupported CURRENT_ARCH_CONFIG $(CURRENT_ARCH_CONFIG))
endif
```

`CURRENT_ARCH_CONFIG` is set per build target (`MakeInc.top:246`, extracted from the build
config name), so **any ARM arch config hits that `$(error)` and stops the build.** The
platform whitelist does include `iPhoneOS` (`MakeInc.cmd:121`) — but the arch gate fires
regardless.

Verified by evaluating the expressions in isolation (the real build additionally fails on
`xcrun`, a macOS toolchain requirement, on this Linux host): the gate errors for `ARM`, and
passes when the whitelist is extended on the command line. So it *is* overridable — but
overriding it means supplying the ARM machine configs, the toolchain assumptions and the
rest of the build configuration Apple did not ship, which is the same class of gap as the
missing `SETUP/config` `.def` files.

**Consequence for the plan:** "build a full `mach_kernel`" is not a matter of running a
build. It means reconstructing a build configuration that is not in the tarball, on top of
a toolchain this project does not have. That does not make it impossible, but it is a
different and larger piece of work than the roadmap's one-line bullet implied — and it
explains why this project's public-XNU work has been a *host-only linkability proof* rather
than a build: the proof is what the shipped source actually supports.

It also sharpens what Phase 3 and Phase 4 are for. A shim plus a conforming `boot_args`
produces something XNU could consume; producing the XNU that consumes it needs a build system
that is not public. Worth deciding deliberately rather than discovering during a Phase 4
attempt.

**And the gap is wider than the arch whitelist alone.** Chasing what an override would
actually require:

- `TARGET_CONFIGS` is built from `ARCH_CONFIGS_EMBEDDED` and `DEVICEMAP_PRODUCTS_$(arch)`
  (`MakeInc.top:136-145`). **Neither is defined anywhere in the tarball** — they come from
  the internal build system. So the per-arch target lists are not merely restricted; they are
  absent.
- Build config names are parsed as `KERNEL^ARCH^MACHINE` (`MakeInc.cmd:299`, verified by
  evaluating the extractors: `RELEASE^ARM64^NONE` → kernel `RELEASE`, arch `ARM64`, machine
  `NONE`). So the *naming* convention is public and the parser works — what is missing is the
  content the names index into.
- The platform whitelist itself (`SUPPORTED_EMBEDDED_PLATFORMS`) is in `MakeInc.cmd:121`,
  so that half *is* public and editable.

**And the entry path specifically, measured (2026-09-17,
[`experiment-107`](../experiments/experiment-107-xnu-arm-entry-path-measured.md)).**
`stages/stage90/xnu_arm_entrypath_sweep.sh` measures the files the entry path needs rather than all
32 in `osfmk/arm`: `arm_init.c` — the function `_start` branches to, and therefore the exact thing
Stage90 stubs — has **seven distinct missing names**, `arm_vm_init.c` the same seven, and
`machine_routines.c` thirty.

*(A first version of this measurement said "4 errors" and "1 include". Both were wrong: clang's
default `-ferror-limit` is 20 and a missing header is a *fatal* error that ends the translation
unit, so the count was truncated and the fatal's own line counted as one of them. The script now
passes `-ferror-limit=0` and labels any count containing a fatal as a floor.)*

**AND THE REST OF osfmk IS MEASURED (2026-09-17, [`experiment-111`](../experiments/experiment-111-osfmk-measured.md)).**
`tools/sweep_xnu_osfmk.sh`: **81 of 170** files outside `osfmk/arm` parse (`kern` 41/83, `vm` 9/27,
`ipc` 2/20). Two things moved it. **MIG has three outputs per `.defs`, not one** — the first
generation run asked for `-header` only, and the kernel's sources include `X_server.h` as well;
generating those collapsed the missing-header list from **18 names to 3**. And `u_long`, the largest
single blocker at 223 occurrences, is now supplied. The remaining blockers are no longer headers but
names behind configuration this project has not chosen (`fmsg`, `mnl_msg_t`, `mach_node_t`,
`sched_group_t` — the multi-node Mach IPC machinery). Also caught: the first two runs of the sweep
counted `osfmk/i386`, `osfmk/x86_64` and `osfmk/arm64`, producing 400-odd `_STRUCT_X86_*` blockers
for a kernel that will never contain them. All three are now excluded, with the reason in the
script — a measurement that counts another architecture's failures looks like a result.

**AND THE WHOLE ARM LAYER COMPILES (2026-09-17, [`experiment-110`](../experiments/experiment-110-xnu-arm-layer-compiles.md)).**
`tools/build_xnu_arm_layer.sh`: **32 of 32** files in `osfmk/arm` compile to objects — 118,982 bytes
of text — against **3 of 32** three turns earlier. That layer contains `arm_init.c`, `arm_vm_init.c`,
`pmap.c`, `machine_routines.c`, `locks_arm.c` and `trap.c`. **Not one line of XNU's code was
changed**: the entire movement is the flag set, plus 24 MIG headers generated from Apple's own
`.defs` and eleven headers the build supplies.

What remains is a link: **443 distinct undefined symbols**, most of them outside `osfmk/arm` — the
kernel proper, `libkern`, `bsd`, and compiler runtime (`__aeabi_memcpy4`, `__aeabi_uldivmod`). So
the order is: the ARM layer compiles; XNU does not link, does not build, and does not run.

**AND THE ENTRY PATH NOW COMPILES (2026-09-17, [`experiment-109`](../experiments/experiment-109-xnu-arm-entry-path-compiles.md)).**
`arm_init.c` — the function `_start` branches to, and the exact thing Stage90's entry image stubs —
is **0 errors**, as are `arm_vm_init.c` and `machine_routines.c`. The layer as a whole went from 3
of 32 to **8 of 32**. Four values did most of it, and the largest was a single flag:
`-DXNU_KERNEL_PRIVATE=1` took `arm_init.c` from 35 errors to 10 on its own. The scheduler choice was
narrowed *by the source* rather than guessed: `struct run_queue` is defined only under
`CONFIG_SCHED_TIMESHARE_CORE` or `_PROTO`, while `_MULTIQ` wants a `kern/sched_multiq.h` the tarball
does not ship — so `MASTER.XXX` being absent is not the dead end it looked like. And `-ffreestanding`
is load-bearing: without it clang uses its hosted `<stdatomic.h>`, which defines `memory_order` as
macros rather than the `enum memory_order` XNU's ARM atomics name.

**What that does not mean:** compiling three translation units is not a kernel. They reference
thousands of symbols no object here provides, and the 24 files of `osfmk/arm` that still do not
compile include the pmap, the scheduler and the interrupt path. The blocker for replacing the
`arm_init` stub is now a *link* problem rather than a compile problem — measurable, and the next
thing to measure.

**THE MIG WALL IS DOWN (2026-09-17, [`experiment-108`](../experiments/experiment-108-mig-builds-and-generates-headers.md)).**
Apple's MIG is published in `apple-oss-distributions/bootstrap_cmds/migcom.tproj` — the real
generator, `parser.y` + `lexxer.l` + ~550 KB of C — and `tools/build_mig.sh` now builds it on this
host. `tools/gen_mach_headers.sh` then runs it over 4570's own `.defs`: **23 generated, 2 failed, 0
absent**, including `<mach/mach_host.h>` and `<mach/mach_port.h>`, which were the two blocking the
entry path. The earlier note that "MIG is not in the tarball and not in this host's package
repository" was true and led to the wrong conclusion.

With that done the entry-path error count stopped being truncated — `-ferror-limit=0` and no missing
headers — so `arm_init.c`'s distance is now a **complete 35 errors**, higher than the earlier 20
precisely because that 20 was cut short by a fatal include. What is left of the wall is *choosing
values* (`CONFIG_*`, `MASTER.XXX`) and include ordering, not obtaining tools.

**The rest of this section is the analysis that led there, kept because its method is the point.** `osfmk/vm/vm_object.h`
wants `<mach_pagemap.h>`, `<mach_debug.h>` wants `<mach/mach_host.h>` and `<mach/mach_port.h>` —
and **40 `.defs` files ship under `osfmk/mach/` while their generated headers do not**. MIG is not
in the tarball (`osfmk/mach/{mig.h,mig_errors.h,mig_log.h}` are runtime support, not the generator)
and not in this host's package repository. Three shim variants were measured to establish that:
the redirect form truncates at the fatal, the empty form loses the real `mach_debug_types.h`
chain, and the types-only form is what ships. Each shim clears one name and reveals the next
header, so the seven is a floor as well — the closure grows as you satisfy it, exactly as
`experiment-103` measured for `cpu_data_internal.h`.

~~Also located precisely: … `osfmk/conf/MASTER.XXX` is not in the tarball either (the build
generates it). The scheduler algorithm therefore has to be *chosen*, not read.~~

**CORRECTED 2026-09-17, and this was the biggest error of the session. The build configuration is
not absent — it is in the tarball.** `config/MASTER` (737 lines), `config/MASTER.arm`,
`config/MASTER.arm64`, the tool's source under `SETUP/config/` (`main.c`, `parser.y`, `lexer.l`,
`mkheaders.c`, `mkmakefile.c`) and the driver `SETUP/config/doconf` all ship.
`osfmk/conf/MASTER.XXX` is where doconf **writes its output**, not where the source lives — and the
search that concluded "absent" looked in the directory the `#error` message names, not at the
repository root.

`config/MASTER.arm` states the ARM kernel's attribute sets in Apple's own words, and
`tools/xnu_config/expand.sh KERNEL_BASE` resolves them into the option lines the kernel is built
with:

```
SCHED_BASE = [ config_sched_traditional config_sched_multiq ]
options   CONFIG_ZONE_MAP_MIN=1048576
options   CONFIG_TASK_MAX=512
options   CONFIG_IPC_TABLE_ENTRIES_STEPS=64
options   CONFIG_MAX_CLUSTERS=4
options   SERIAL_CONSOLE
options   VIDEO_CONSOLE
```

Three of those appeared in this project's own osfmk sweep as *undeclared identifiers*. They were
never missing. And `sched_group_t` — the largest single blocker, 25 occurrences — is gated on
`CONFIG_SCHED_MULTIQ`, which `SCHED_BASE` says is **on** alongside `TIMESHARE_CORE`, because the
latter is the queue core the former builds on rather than a competing algorithm.

**And it is now extracted and used** (2026-09-17,
[`experiment-112`](../experiments/experiment-112-apple-kernel-config-extracted.md)).
`tools/xnu_config/{select_master,expand,make_defines}.sh` port `doconf`'s two stages to bash and
turn Apple's attribute sets into a compiler's `-D` list — **108 option lines for `RELEASE`**.
Rebuilding the ARM layer with those instead of the hand-guessed set gives **31 of 32**, and the one
difference is precise: `monotonic_arm.c` needs `MONOTONIC`, and `MONOTONIC` is not in
`config/MASTER` in any form. It is a per-SoC decision, and the per-SoC definitions
(`ARCH_CONFIGS_EMBEDDED`, `DEVICEMAP_PRODUCTS_*`) are the piece that genuinely does not ship. So the
configuration is *nearly* complete: the catalogue, the names and the tool are all present; what is
missing is the small layer that varies by SoC, of which this layer needs exactly one value.

**And so are Apple's file lists** (2026-09-17,
[`experiment-113`](../experiments/experiment-113-arm-kernel-build-manifest.md)). `osfmk/conf/
files.arm` and its siblings in four other components list the kernel's sources with the classic BSD
`standard` / `optional <flag>` conditions. `tools/xnu_config/list_sources.py` resolves them against
the configuration — semantics taken from `SETUP/config/mkmakefile.c:416-422` rather than guessed,
where multiple flags are AND and `optional not x` inverts — and produces the object list:

```
RELEASE: 694 file(s) selected for arm
  present on disk:      652
  MIG-generated:        39
  listed but absent:    3      (hand-written ARM asm Apple did not publish)
```

**And compiled** (2026-09-17, [`experiment-114`](../experiments/experiment-114-kernel-manifest-compiled.md)):
`tools/build_xnu_arm_kernel.sh` compiles the manifest, and **172 of 569 (30%) of its C files
compile**. Where it succeeds is the finding:

| Component | Failed |
| --- | --- |
| `osfmk/arm` | **0** — the whole bring-up path |
| `iokit` | 1 of 62 |
| `pexpert` | 4 of 9 |
| `libkern` | 32 of 70 |
| `osfmk` | 77 of 230 |
| `bsd` | **288 of 293** |

**The Mach side largely works and the BSD side is almost entirely blocked**, and `bsd/net`,
`bsd/netinet` and `bsd/netinet6` are 254 of the 397 failures — the network stack, which a boot to a
first scheduler tick does not need. `<sys/sysproto.h>` was the largest single blocker and is
*generatable* (`bsd/kern/makesyscalls.sh`, wrapped by `tools/gen_bsd_headers.sh`); its 55 errors are
gone and the pass count did not move, because those files fail on other things behind it.

**And a minimal-boot configuration now exists** (2026-09-17,
[`experiment-115`](../experiments/experiment-115-minimal-boot-configuration.md)). `RELEASE` is the
full iOS kernel and 254 of its 397 failures are the network stack, which a boot to
`machine_startup` does not need. `tools/xnu_config/minimal/STAGE90_BOOT.local` declares a smaller
one — **one comment line**, using Apple's own attribute names and the `MASTER.local` mechanism
doconf documents. The manifest drops from 694 files to 526, and **191 of 401 C files compile**.

It also found `-D_CLOCK_T=1`: `bsd/sys/types.h:162` includes `_clock_t.h` *unconditionally* and
typedefs `clock_t` to `unsigned long`, while `kern_types.h:193` typedefs it to `struct clock *`.
One name, two definitions — the fourth instance of this project's recurring defect — and one flag
makes the kernel's win, taking `iokit` from 1 failing file to **0**. Also corrected: the build
script now compiles with the configuration's own 104 options rather than a handful of hand-worked
flags, which is why `experiment-114`'s numbers were low.

**Two tooling defects, found because a run hung for 45 minutes** (2026-09-17,
[`experiment-116`](../experiments/experiment-116-define-generator-and-timeout.md)).
`make_defines.sh` read the option name as `$NF`, which is wrong for the one option whose value
contains spaces — `CONFIG_NMBCLUSTERS="((1024 * 256) / MCLBYTES)"` became `-DMCLBYTES)=1`, a
*corrupted* definition of a real kernel macro rather than a missing one. And `build_xnu_arm_kernel.sh`
had no per-file timeout and only appended to its failure list, so one non-terminating file stopped
the run forever and a count read afterwards was a count of two runs. Both fixed: the generator now
takes everything after `options`, the build bounds each file at 60 s, reports a timeout as its own
outcome, and truncates every output.

Corrected: **`RELEASE` 197 of 569 compile** (was 172), **`STAGE90_BOOT` 191 of 401**. One file,
`bsd/netinet/ip_input.c`, still times out under a *combination* of `RELEASE`'s options — not the
preprocessor (`clang -E` exits normally), and no single option reproduces it. Left as an unfinished
bisect rather than implied to be resolved.

**Two shims were shadows of real files** (2026-09-17,
[`experiment-117`](../experiments/experiment-117-shadows-and-broad-paths.md)). `security/_label.h`
and `san/kasan.h` were hand-written as absent; both are in the tarball **at the tree root**, and the
search that concluded otherwise looked under `osfmk/`. Deleting them and adding `-I$XNU` raised the
minimal build from 191 to 196 and `RELEASE` from 197 to **204**. The same experiment found the
opposite lesson: putting `osfmk/libsa` on the include path to get its `uint_t` cost four files,
because that directory also holds a bootloader-context `string.h` and `sys/` that shadow the real
ones — Apple exports a *selected list* per component (`EXPORT_MI_LIST` → `EXPORT_HDRS`), and exposing
the whole source directory is a different and worse thing. Both changes are net measurement, neither
is a change to XNU's code.

**AND THE COLLISIONS ARE EXPLAINED, AND WERE A FLAG** (2026-09-17,
[`experiment-118`](../experiments/experiment-118-per-component-defines.md)). The exported-header set
was the wrong suspect: this project's own commit for `experiment-117` measured the export roots and
found them *worse* (167 against 196). The real cause is that Apple sets the `*_KERNEL_PRIVATE`
macros **per component**, in `<component>/conf/Makefile.template`, and this build set
`MACH_KERNEL_PRIVATE` globally. `MACH_KERNEL_PRIVATE` is what reaches `kern/misc_protos.h`, whose
`ffs(unsigned int)` / `fls(unsigned int)` / `copyinstr(const user_addr_t, char *, vm_size_t, …)`
collide with `bsd/libkern/libkern.h`'s `ffs(int)` / `fls(int)` /
`copyinstr(const user_addr_t, void *, size_t, …)` — so every BSD file including `<sys/systm.h>`
failed. 127 of the minimal configuration's 205 failures, from one flag in the wrong scope.

Adopting the per-component table (`tools/xnu_config/component_defines.sh`, checked against the
templates by `tools/check_component_defines.py`) takes **`STAGE90_BOOT` 196 → 288 of 401** and
**`RELEASE` 204 → 329 of 569**. `osfmk` does not move at all (59 failing before and after); the gain
is bsd 122 → 40, libkern 17 → 10, pexpert 3 → 0. `uthread_t` and `ORDINARY` are no longer in the
first-error list at all — they were downstream of `ffs`.

What remains is 113 failures spread over 36 missing generated headers (largest:
`mach/memory_object_control.h`, 10 files — the `.defs` needs `upl_size_t` from `mach_types.defs`,
which `gen_mach_headers.sh` does not feed to MIG), 18 unknown type names, 14 incomplete field types,
14 conflicting types, and small tails.

**AND THE HEADERS APPLE'S BUILD GENERATES ARE NOW GENERATED, MOSTLY** (2026-09-17,
[`experiment-119`](../experiments/experiment-119-build-generated-headers.md)). `osfmk/mach/*.defs`
was only part of the picture, and the pipeline itself was wrong in one place that mattered:

- **MIG was run without the kernel defines.** `mach_types.defs` guards the Universal Page List block
  (`upl_size_t`, `upl_t`) behind `#if KERNEL_PRIVATE`, and the preprocessor step passed no `-D` at
  all, so `memory_object_control` and `upl` produced nothing at all. Apple feeds MIG `$(DEFINES)`
  (`MakeInc.def:470`).
- **The `.defs` set was one directory wide.** `osfmk/device/iokit_rpc.c:59` includes
  `<device/device_server.h>`; `osfmk/device/device.defs` is where it comes from. The set is now every
  `.defs` under `osfmk` — 44 generated, 8 types-only, 0 failed. Two details the script had to learn:
  a types-only `.defs` has to be detected from its *preprocessed* text (`mach_notify.defs` is one
  `#include` of `notify.defs` and is a rename of it for `ipc_notify.c:68`), and `UserNotification` is
  the one directory whose server header is `<X>Server.h` rather than `<X>_server.h`
  (`osfmk/UserNotification/Makefile:80-81`).
- **The generated root was in front of the source tree.** MIG generates `mach/memory_object.h`,
  `mach/notify.h` and `mach/semaphore.h` from the `.defs` of the same names, and the tree has
  hand-written headers at those exact paths. `-I$MIG_HEADERS` was second in the include list, so the
  generated ones won; moving it after every source tree is **312 vs 307** in the minimal
  configuration. The fifth time a broad path in front of a narrow one has been the bug.

**`STAGE90_BOOT` 288 → 315 of 405, `RELEASE` 329 → 356 of 573.** The denominators move because the
manifest enumerates MIG output, so generating more of it moves files from `absent` to `tried`.

**And the fourth instance of the same idea is identified but not implemented: `OPTIONS/`.** 123 lines
across the components' `conf/files` read `OPTIONS/mach_ipc_debug optional mach_ipc_debug`, and
`SETUP/config/mkheaders.c:104-133` is the generator: it writes `#define <option> <count>` into
`<name>.h` if that file does not exist, and appends `#include <<name>.h>` to **`meta_features.h`** —
the header every component force-includes. So `mach_ipc_debug.h` (9 files, the largest single
remaining blocker), `mach_vm_debug.h`, `mach_cluster_stats.h`, `mach_ipc_test.h`, `kdebug.h`,
`vm_cpm.h` and their siblings are one-line generated files. `osfmk/ipc/ipc_hash.h:128` includes
`<mach_ipc_debug.h>` **unguarded** precisely because the include is what defines the option's macro,
with the real content behind `#if MACH_IPC_DEBUG` on the next line. `libkern/version.h` (3 files) is
the same category with a different generator (`libkern/libkern/Makefile:79-87`, from
`version.h.template` + `config/MasterVersion`, both in the tarball). **That is the next stage.**

**AND THE `OPTIONS/` HEADERS ARE GENERATED TOO, AND TEN SHIMS WENT AWAY** (2026-09-17,
[`experiment-120`](../experiments/experiment-120-options-generated-headers.md)). 124 lines of
`*/conf/files` read `OPTIONS/mach_ipc_debug optional mach_ipc_debug`, and
`SETUP/config/mkheaders.c:114-135` turns each into `#define <MACRO> <0|1>` in a **flat** object-dir
header plus an `#include` in `meta_features.h`. `tools/gen_option_headers.py` reproduces it:
**91 headers, 11 on, 80 off** for `STAGE90_BOOT`. The naming is not what it looks like — the file
takes its name from the first word after `optional`, not from the `OPTIONS/` name
(`mkmakefile.c:366-372`), and `#include <mach_ipc_debug.h>` is flat, not `mach/…`
(`main.c:206-216`).

The diagnostic shape is worth keeping: **a header included with no `#if` around it, existing
nowhere in the tree, whose content is guarded on the very next line.** `osfmk/ipc/ipc_hash.h:128`
is exactly that, and it identified nine more.

**Ten shims in `stages/stage90/shims_arm/` were deleted** — nine hard-coded a value the
configuration already states (`MACH_ASSERT 0`, `ZONE_DEBUG 0`, `CONFIG_DTRACE 0`, …), one was empty.
Both configurations' `failed.txt` lists were **byte-identical with and without them**, so they were
dead the moment the generator existed, and the statement they carried ("absent from the tarball")
had become false. `libkern/version.h` is the same idea with a different generator:
`tools/gen_libkern_version.sh` runs Apple's own `config/newvers.pl` over `version.h.template`, 7
substitutions from `config/MasterVersion`.

**`STAGE90_BOOT` 315 → 328 of 405, `RELEASE` 356 → 368 of 573.** The arm layer build is unchanged
at 32 of 32 / 118,970 bytes, with 445 undefined symbols against 443 before (the two not identified).
Recorded because it did *not* move: force-including `meta_features.h` changes no count, and is kept
anyway since a macro with the wrong value is silent while a missing header is loud.

**What is left is now genuinely absent rather than generated.** `loop.h`, `pty.h`, `compat_43.h`,
`sys/modctl.h` and `os/firehose_buffer_private.h` match no `OPTIONS/` line and are in no
component's `EXPORT_MI_LIST`. 77 failures in the minimal configuration, 205 in `RELEASE`.

**AND TWO FLAGS THE PROJECT HAD BEEN SUPPLYING DIFFERENTLY FROM APPLE** (2026-09-17,
[`experiment-121`](../experiments/experiment-121-apple-defines-and-two-pass-mig.md)). `RELEASE` is
**85 % compiled** — 496 of 585 — and `STAGE90_BOOT` 375 of 417.

**`__APPLE__` comes from the compiler, not the source.** These scripts compile with
`clang --target=armv7-none-eabi`; Apple's build uses a Darwin triple, and `__APPLE__` arrives with
it. Undefined, it sends `osfmk/prng/YarrowCoreLib/` — a vendored library with Windows types — down
its `__declspec(dllimport)` branch (`yarrow.h:91`), and off the branch at `:53` that includes
`WindowsTypesForMac.h`, where `BYTE`, `UINT`, `LONGLONG` and `LPVOID` come from. **+17 files in the
minimal configuration, +99 in `RELEASE`, no regressions.** Measured and not adopted:
`-D__MACH__=1` adds nothing (345 either way), and the full `armv7-apple-darwin` triple is worth one
more file while changing ELF→Mach-O, which is a link-step decision.

**MIG is run twice per `.defs`, and this project ran it once.** `osfmk/mach/Makefile:361-382` has
two rules — `%_user.c` with `MIGKUFLAGS = -DKERNEL_USER=1` and `%_server.c` with
`MIGKSFLAGS = -DKERNEL_SERVER=1` (`:247-248`). Asking for `-header` and `-sheader` in one
invocation generates each side with the other's `#if` blocks already preprocessed away. The loss is
concrete: `mach_types.defs:606-615` puts eight `simport` lines behind `#if KERNEL_SERVER`, and a
`simport` becomes an `#include` in the generated server header — so `ikot`/`kern/ipc_*` never
arrived, and five files failed on **34 occurrences** of `IKOT_NAMED_ENTRY`, `IKOT_TIMER` and
`ipc_kobject_type_t`, all of which have been in `osfmk/kern/ipc_kobject.h` all along. Two runs also
produce `*_user.c`, which the manifest lists and which were not being generated at all.

**Three regressions, mechanism identified, recorded as open.** `bsd/{security/audit/audit_syscalls,
uxkern/ux_exception}.c` (both configurations) and `bsd/kern/uipc_mbuf.c` (`RELEASE`) now reach
`ipc/ipc_kmsg.h` through the new `simport` includes, and `ipc_kmsg.h` uses `sync_qos_count_t` and
`ipc_kmsg_t` unconditionally while `ipc_types.h:46` defines them only under `MACH_KERNEL_PRIVATE` —
which a BSD translation unit does not define. Adding it for `bsd` reaches `sched_prim.h:574`'s
`#error`, the boundary that established the per-component rule. Whether Apple's own build avoids the
path by a different include order is not established.

**What is left:** 42 failures minimal, 89 `RELEASE`. The largest remaining cluster is MIG's
`consume_ref` types — `mem_entry_name_port_move_send_t` (12), `semaphore_consume_ref_t` (9),
`thread_act_consume_ref_t` (7) — declared as `type X = mach_port_move_send_t` and emitted into the
server headers without a definition. Next stage.

**AND THE SAME SHAPE A THIRD TIME: A TOOL WITH SIX OUTPUTS, ASKED FOR ONE** (2026-09-17,
[`experiment-122`](../experiments/experiment-122-makesyscalls-outputs.md)). `makesyscalls.sh:75`
states its own interface — `names|proto|header|table|audit|trace` — and Apple's Makefiles ask for
all six into named destinations (`bsd/sys/Makefile:216` for `header`, `bsd/conf/Makefile.template:291-305`
for the rest). `gen_bsd_headers.sh` ran only `proto`, because `sysproto.h` was the file the failure
list named. `sys/syscall.h` is where the syscall *numbers* live, so `kern_mman.c`, `sys_generic.c`
and `kern_guarded.c` failed on `SYS_mmap`, `SYS_pread` and `SYS_guarded_pwrite_np` — each in a file
that *defines* the routine whose number it is looking up. **415 `SYS_*` numbers**, and all six
outputs now generated, with the script failing if `syscall.h` comes out with none.

Two more defects came out of it, both in the *resolution* rather than the generation.
`list_sources.py` resolved every `./` entry against one generated root, but there are three
generators — MIG (`./mach/task_server.c`), makesyscalls (`./init_sysent.c`), and `config(8)`
(`./ioconf.c`, not generated here); `resolve_path` now searches a list. And
`build_xnu_arm_kernel.sh`'s `component_of()` defaulted anything outside the tree to `osfmk`, so
`out/xnu_generated/bsd/init_sysent.c` picked up `-DMACH_KERNEL_PRIVATE` and died on the
`ffs`/`fls` collision experiment-118 is about. Both roots are now mapped explicitly.

**A fourth shim deleted and measured**: `shims_arm/sys/syscall.h` declared `unix_syscall` on the
premise that the header "does not exist anywhere in the tarball". Byte-identical failure sets with
and without it — but it had been *doing harm*, since `bsd/dev/arm/systemcalls.c:82` defines
`unix_syscall` and the shim's prototype was the cause of that file's `conflicting types` error.

**`STAGE90_BOOT` 375 → 381 of 419, `RELEASE` 496 → 499 of 587.** What is left is 38 and 88
failures, small and scattered — `clock_t`/`uid_t`, `u_char`/`caddr_t`, the `_CLOCK_T` collision,
`z_off_t` in zlib, and experiment-121's three include-order regressions. No single fix is worth 30
files any more, which is the state change: the structural gaps are gone.

**AND THE LINK GAP IS MEASURED, WHICH IS THE NUMBER THAT MATTERS** (2026-09-17,
[`experiment-123`](../experiments/experiment-123-link-gap-and-force-includes.md)). Every stage so
far reported a *compile* count, which says nothing about what is missing — most of the manifest's
587 files do not matter for any given symbol. `tools/link_gap.sh` answers the other question
without a linker, by taking every symbol the compiled objects reference, subtracting every symbol
any of them defines, and separating out the compiler runtime:

```
objects:                                        539
symbols defined:                              13682
MISSING (referenced by every, defined by none): 1187
  of which compiler runtime (libgcc/compiler-rt): 9      (__aeabi_*)
  of which a source file must provide:           1178
```

**Attribution is what turns it into a work list.** At least 182 are defined by a file that currently
fails to compile — 42 in `vm_pageout.c`, 16 each in `kern/task.c` and `vm_compressor.c`, 15 in
`libkern/gen/OSAtomicOperations.c` — and that count is a floor, since it comes from definition-shaped
lines at column 0 and much of XNU puts the return type on its own line.

**And it immediately explained a failure that had been mis-described for several stages.** The
"`z_off_t` in zlib" cluster (8 files) is not about `z_off_t`:
`libkern/zlib/zutil.h:193-194` is `#if KERNEL / typedef long ptrdiff_t;` in a file Apple compiles,
so in Apple's build `ptrdiff_t` is undefined there. Two things here defined it —
`shims_arm/string.h` included `<stddef.h>` for `size_t`, and `-include stdatomic.h` reaches
`EXTERNAL_HEADERS/stddef.h` at its line 38. Both now take the compiler's own builtin instead, and
`_SIZE_T` is claimed because `stddef.h:28` and `osfmk/libsa/types.h:52` both guard `size_t` with it
and `libsa` says `unsigned long` where this target's compiler says `unsigned int`. **389 of 419.**

**`armv7-apple-darwin` measured and not adopted:** 390 vs 389, and it produces Mach-O objects that
nothing on this host can link — `/usr/lib/llvm-14/bin` has `llvm-nm`, `llvm-size` and `llvm-ar` but
no `ld64.lld`. The right target in principle, and a decision for the link step rather than this one.

**`STAGE90_BOOT` 381 → 389 of 419, `RELEASE` 499 → 507 of 587.** What is left is 30 and 80
failures, small and scattered — `clock_t`/`uid_t`, `u_char`/`caddr_t`, experiment-121's three
include-order regressions, and four genuinely absent headers (`pty.h`, `loop.h`, `compat_43.h`,
`sys/modctl.h`). `libkern/zlib` is entirely clear.

**AND THE MIG OUTPUT SET COMES FROM THE MAKEFILES NOW, WHICH REVERSES experiment-119** (2026-09-17,
[`experiment-124`](../experiments/experiment-124-mig-output-set.md)). `gen_mach_headers.sh` ran MIG
over *every* `.defs` in `osfmk`; Apple's Makefiles list the outputs explicitly, and two entries
decide the file counts. **`notify.defs` yields `notify_server.h` and nothing else** — `mach/notify.h`
is hand-written and carries `MACH_NOTIFY_NO_SENDERS`, so a generated one is a stub that hides the
real header. And **`memory_object.defs` yields `memory_object.h`**, so Apple's kernel never sees the
collision between it (user-side `mach_port_t`) and `memory_object_types.h` (kernel-side
`struct memory_object *`) — the collision that was failing **six `osfmk/vm` files**.
`tools/xnu_config/mig_outputs.py` parses the `MIG_*` blocks out of every Makefile that runs `$(MIG)`
and writes the spec; **40 bases, 0 failures**.

**And the include order flips back.** experiment-119 measured the generated root ahead of the source
tree as worse (307 vs 312) and concluded it must go last — correct for what it tested, which was an
*over-generating* root. With the faithful output set: **395 vs 384** in favour of first, which is
Apple's own order (`INCFLAGS = -I. $(INCFLAGS_GEN) …`, `MakeInc.def:466-469`).

**AND THE BOOT PATH IS SEPARATED FROM THE FAILURE LIST** (`tools/boot_closure.py`, new). It walks
the symbol graph from the `osfmk/arm` objects and reports the files in that closure which have no
object: **27 of the 74 failing files matter; the other 47 do not** — mostly `bsd/netinet6`, `bsd/nfs`
and `bsd/vfs`, code a kernel reaching a first scheduler tick never calls. The remaining work is now
"27 files, 86 symbols", a lower bound (calls through function pointers and assembly entries are
invisible to a symbol walk). The link gap re-measured at the same time: **1187 → 988** missing, of
which **907 are on the boot path**.

**`STAGE90_BOOT` 389 → 395 of 419, `RELEASE` 507 → 513 of 587.**

**AND THE FIRST REAL LINK, WHICH FOUND WHAT `nm` CANNOT** (2026-09-17,
[`experiment-125`](../experiments/experiment-125-first-link-and-option-scope.md)). Everything
reported so far comes from `nm` and from clang, and neither can see two objects defining one symbol
— both compile, and only a link fails. The first `ld -r` said:

```
bsd_net_net_stubs.o: multiple definition of `ctl_register'; bsd_kern_kern_control.o: first defined
bsd_kern_subr_xxx.o: multiple definition of `rc4_init';    bsd_crypto_rc4_rc4.o: first defined
bsd_netinet6_in6_cksum.o: multiple definition of `inet6_cksum'; bsd_kern_kpi_mbuf.o: first defined
```

`bsd/net/net_stubs.c:31` is `#if !NETWORKING` — panicking stubs for a kernel built with no
networking. So `NETWORKING` was false where it should have been true, and the reason is the sixth
instance of this project's recurring defect with a new mechanism: **the generated `OPTIONS/` headers
were one directory for both configurations.** RELEASE and STAGE90_BOOT disagree on **20 of them** —
`CRYPTO`, `NETWORKING`, `SOCKETS`, `DEVFS`, `FIFO`, `DUMMYNET`, `CONFIG_MACF` and more — and
whichever generation ran last won for both. That was `STAGE90_BOOT`, so the RELEASE build was
compiled with `NETWORKING 0`; `meta_features.h` force-includes the header, so the generated
`#define NETWORKING 0` overrode the command line's `-DNETWORKING=1` without a word.
`gen_option_headers.py` now writes `out/xnu_options/<CONFIG>/` and the build fails loudly if that
configuration's set is absent. **`RELEASE` 513 → 560 of 587, no regressions.**

`tools/link_xnu_arm.sh` exists because a compiler answers "does this parse" and `nm` answers "what
does this object want", while only a linker answers **"do these objects fit together"**. After the
fix: **560 objects, 0 duplicate definitions, exit 0, one 6 338 748-byte relocatable image**. The full
link (`-T stages/stage90/xnu_link.ld --no-undefined`) fails, correctly, on **889 symbols** — within
rounding of what the `nm`-based `link_gap.sh` independently reports, which is a useful cross-check.
`boot_closure.py` narrows it to **19 failing files on the boot path, 78 symbols**.

**What is left is 24 failures in the minimal configuration and 27 in `RELEASE` — and the two sets
are now nearly the same files**, which is itself a result: most of what separated them was this
defect.

**AND A WORKAROUND OUTLIVED THE DEFECT IT WORKED AROUND** (2026-09-17,
[`experiment-126`](../experiments/experiment-126-clockt-and-libsa-types.md)). `-D_CLOCK_T=1` was
introduced in experiment-115 with a correct diagnosis — `bsd/sys/_types/_clock_t.h` and
`osfmk/kern/kern_types.h:193` both define `clock_t`, differently — and it was **a symptom of
`MACH_KERNEL_PRIVATE` being global**, which experiment-118 removed. With per-component defines the
two definitions no longer meet and the flag resolves nothing: removing it is **560 → 564**, four
files newly passing. The comment that introduced it is preserved next to the removal, because the
general lesson is that a workaround reads as a fact about the source once the defect is gone.

**`<types.h>` means `osfmk/libsa/types.h`** — the kernel's own `u_char`, `caddr_t`, `daddr_t`.
`osfmk/device/subrs.c:138` reaches it through `<libsa/stdlib.h:63>`. Exposing that directory is what
experiment-117 measured as costing four files and it still does: its `string.h` carries the
`__builtin___*_chk` macros and takes `iokit/Kernel/IOStringFuncs.c` from passing to failing. A
*filtered* root of the three headers `<types.h>` needs, placed just before `-I$XNU/bsd/arm`, is
**565 with no regressions** — Apple's `EXPORT_HDRS` principle, third time this project has reached
for it.

**`STAGE90_BOOT` 395 → 400 of 419, `RELEASE` 560 → 565 of 587, and the link 898 → 777 undefined
symbols.** `u_char`, `caddr_t` and every `clock_t` are gone from the failure list.

**AND A THIRD GENERATOR — `config(8)`'s DEVICE HEADERS** (2026-09-17,
[`experiment-127`](../experiments/experiment-127-device-headers-and-a-source-incompatibility.md)).
`bsd/kern/bsd_init.c:875` includes `<loop.h>` with `#if NLOOP > 0` on the next lines, and three
`bsd/netinet6` files follow; the header is `config(8)` output from `pseudo-device loop`, which 4570
does not publish — `grep -c '^pseudo-device' */conf/files` is 0 in every component. So `NLOOP` is a
**choice**, and 0 is the one that agrees with the rest of the configuration: `bsd/net/if_loop.c` is
`optional loop` and `loop` is not set, so nothing would provide `loopattach()` — with `NLOOP 1` that
is an undefined symbol at link time. `tools/gen_device_headers.sh` writes it. **569 of 587.**

**And one failure that is NOT a configuration gap, the first of the whole exercise.**
`osfmk/vm/vm_object.c:355` is `*object = vm_object_template;` and `vm_object.h:174` declares one
member `const`. Assigning to a struct with a `const` member is a C constraint violation, and both
compilers on this host reject it — clang for all four `-std` modes, gcc too, not suppressible by any
flag (the diagnostic carries no `[-Wflag]`). It is specific to 4570: `xnu-upstream`, `xnu-2050.18.24`
and `apple-xnu-rel-2050` all declare that member without `const`. So Apple built it and a current
compiler does not accept it — most likely the 2016-era clang allowed it and the diagnostic was
tightened later, but that is an inference. It is left failing and recorded rather than patched, since
the project does not modify XNU's code. It is 2 of the 62 symbols the boot closure attributes.

**The link is at 699 undefined symbols**, from 1187 when it was first measured, and
`boot_closure.py` now puts only **12 of the 18 failing files on the boot path, accounting for 62
symbols.**

**AND THE MANIFEST WAS MISSING A WHOLE COMPONENT** (2026-09-17,
[`experiment-128`](../experiments/experiment-128-manifest-missing-component.md)). This is the largest
single movement since the per-component defines and it is not a compile fix. `list_sources.py` had
`DEFAULT_COMPONENTS = [osfmk bsd libkern iokit pexpert]` against Apple's own
`COMPONENT_LIST = osfmk bsd libkern iokit pexpert libsa security san` (`MakeInc.def:46`). `libsa` is
legitimately excluded — bootloader context, reached as a header — but **`security` was not**, and
`CONFIG_MACF=1` is set in RELEASE while `security/mac_*.c` are `optional config_macf` and are called
by the rest of the kernel. **About 180 of the 699 undefined symbols came from `security/` alone**
(`mac_vfs.c` 81, `mac_process.c` 26, `mac_base.c` 15, …), and no amount of fixing compile errors
would have touched them: the files were never compiled.

It only became visible once the link was real. Every earlier measurement asked "do these files
compile", and the answer for a file that is not in the list is a shrug. **The check that catches it
is the one this stage performed**: after a link, attribute every remaining undefined symbol to a
source file and ask whether that file was compiled at all.

`san` was included, measured and excluded on purpose: its five files are `standard` in
`san/conf/files` but they are KASAN machinery (`#error KASAN undefined`, `<kasan.h>` not published),
and including them adds 5 failing files and contributes **0** of the link's undefined symbols.

**`STAGE90_BOOT` 400 → 401 of 420, `RELEASE` 569 → 591 of 609, and the link 699 → 499 undefined
symbols** (660 → 460 on the boot path). The boot closure is unchanged at 12 files / 60 symbols,
because those files were already attributing theirs.

**AND THE BIGGEST BOOT-PATH FILE WAS ONE MISSING TYPEDEF** (2026-09-17,
[`experiment-129`](../experiments/experiment-129-caddr-t.md)). `osfmk/vm/vm_compressor.c` was 17 of
the boot path's 60 symbols, on `unknown type name 'caddr_t'`. `caddr_t` is defined once in the tree
(`bsd/sys/_types/_caddr_t.h:30`) and that file reaches no header including it — verified by
preprocessing, where `osfmk/device/subrs.c`'s closure contains the typedef and `vm_compressor.c`'s
does not (it reaches `osfmk/libsa/types.h`, through `<libsa/stdlib.h>`; `vm_compressor.c` reaches
nothing). The narrow header is force-included, the same treatment `u_int` has had since the ARM
layer first compiled. **`RELEASE` 591 → 592, no regressions; the link 499 → 454; the boot closure
60 → 42 symbols over 11 files.** How *Apple's* build reached it is left open — the plausible answer
is the BSD `<sys/types.h>` chain, but forcing that in would reintroduce the `clock_t` collision
experiment-118 fixed, so it is not the mechanism to copy.

**What is left on the boot path is 11 files and 42 symbols, and both of the largest are identified:**
`libkern/gen/OSAtomicOperations.c` (15) defines `enum { false = 0, true = 1 }` and something in its
closure now defines them first, and `bsd/dev/arm/conf.c` (7) needs `pty.h` — a header Apple's
config(8) generates for a `pseudo-device pty` the tarball does not publish, the same shape as
`loop.h`, which `tools/gen_device_headers.sh` already handles.

**AND TWO INVESTIGATIONS THAT PRODUCED NEGATIVE RESULTS, WHICH ARE RESULTS** (2026-09-17,
[`experiment-130`](../experiments/experiment-130-two-negative-results.md)). Both of the boot path's
largest remaining items need something outside the tarball, and neither is a value to choose — which
is what every fix since experiment-119 has been.

`libkern/gen/OSAtomicOperations.c` (15 of the 42 symbols) fails because `false`/`true` are macros by
the time its `enum { false = 0, true = 1 }` is reached. The chain is traced: `kern/debug.h` →
`mach/vm_param.h:79` → `libkern/os/overflow.h:45` → `EXTERNAL_HEADERS/stdbool.h:36`. Both includes are
unconditional, both files are `standard`, and Apple compiles this file — so Apple reached the same
line without the macros and **why is not established**. Left open with the chain written down, since
a guess is exactly what does not belong in a build script.

`bsd/dev/arm/conf.c` (7) needs `<pty.h>`, and the `loop.h` treatment was tried and **measured to
fail**: with `NPTY 0`, `conf.c:189` fails on `ptsselect`, which **exists nowhere in the tree** — the
`#else` branch defines `ptcselect` but not `ptsselect`. Apple's `NPTY 0` path does not compile in
4570 at all. So `NPTY` must be non-zero and `bsd/kern/tty_pty.c` must be compiled with it — and
**`pty` is a device, not an option**, so with no device tables published (experiment-127)
`optional pty` can never match. Doing this properly means giving `list_sources.py` a device table to
read, which is also what would make `NLOOP` a fact rather than a choice.

**What remains on the boot path is 11 files / 42 symbols: 2 that need facts from outside the
tarball, 8 smaller ones, and `vm_object.c`'s `const` member, which is not fixable here at all.**

**AND THE BUILD DEFINED `MONOTONIC` WHILE THE MANIFEST EXCLUDED ITS ONLY IMPLEMENTATION**
(2026-09-17, [`experiment-131`](../experiments/experiment-131-device-table.md)).
`osfmk/conf/files:296` is `osfmk/kern/kern_monotonic.c optional monotonic`, and
`build_xnu_arm_kernel.sh` defines `-DMONOTONIC=1` **by hand** — it is the per-SoC value
experiment-112 identified as genuinely absent. `list_sources.py` matched `optional` against
`config/MASTER` only, so every *user* of `MONOTONIC` was compiled with it on while the only
*implementation* was excluded. Ten undefined symbols at link time, and the seventh instance of the
recurring defect with a new mechanism: **two halves of the build reading two sources of truth for
one condition, with nothing comparing them.**

**And the reason is now written down**: a `*/conf/files` condition has three origins — an option
(MASTER), an `OPTIONS/` line (a generated `0`/`1` header), and a `device`/`pseudo-device`
declaration. **4570 publishes no device lines at all**, so the third is simply absent. Of the 139
distinct conditions, 89 are not in `RELEASE`: 60 are `OPTIONS/` (handled and correctly off) and **29
have nothing**, including `loop`, `pty`, `ptmx`, `monotonic` and eleven per-SoC `config_*` names.

`tools/xnu_config/device_table.py` is where those choices live, and **both** the manifest and the
compile flags read it. The build runs it as a gate that compares the table against the script's own
`-D` flags — reading the *value*, since `-DXPR_DEBUG=0` mentions the name and means off — and stops
on a disagreement. Perturbation-tested. `pty`/`ptmx` are deliberately left out: `NPTY 0` was measured
not to compile, so they need a real device table and `--unknown` keeps reporting them.

**`STAGE90_BOOT` 402 → 405 of 423, `RELEASE` 592 → 595 of 612, link 454 → 444.** The boot-path list
is unchanged at 11 files / 42 symbols — `kern_monotonic.c` was never in it, because it was never
*attempting* to compile.

**AND THE pty DEVICE, WHICH NEEDED FOUR THINGS TO AGREE** (2026-09-17,
[`experiment-132`](../experiments/experiment-132-pty-device-and-a-named-unknown.md)). `NPTY 0` does
not compile (experiment-130), so the device has to be on — and "on" means the table, the generated
header, the `-D` flag and the manifest together: `device_table.py` says `pty: 1`, `pty.h` says
`NPTY 1`, the build defines `-DNPTY=1`, and `list_sources.py` picks up the three files behind it.
`NPTY 1` rather than larger because `tty_pty.c:89-92` is `#if NPTY == 1 / #define NPTY 32` with a
`#warning`. **`conf.c` compiles**, and it is the file that *defines* `bdevsw`, `cdevsw`, `chrtoblk`,
`nblkdev` — the device-switch tables the whole BSD device layer needs.

**Two defects in the tool that exists to prevent defects**, both found by running it against a case
that should pass: it read the *name* and not the value (`-DXPR_DEBUG=0` mentions `xpr_debug` and
means off), and it compared a device's name to its macro (`pty` vs `NPTY`) — the same
one-value-two-definitions shape one level up.

**And one missing header narrowed to one value.** `os/firehose_buffer_private.h` is not published,
but everything its three users take from it is, under `libkern/firehose/` (`libkern/firehose/Makefile:37-42`
exports all four). A forwarding shim changes `file not found` into
`use of undeclared identifier 'FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT'` — **one constant, used to size
real allocations, with no evidence in the tarball for its value.** Left failing and named: a guess
there would under- or over-allocate a kernel buffer.

**`STAGE90_BOOT` 405 → 406 of 426, `RELEASE` 595 → 599 of 615, boot path 42 → 33 symbols — and the
link 444 → 449, five symbols worse.** The trade is the same change: `conf.c` provides nine
device-switch tables and references fourteen log entry points that live in the two files §3 is
about. Kept because their `NPTY 0` path is broken — any kernel from this source has the device on —
and because the fourteen are already the queue. **The link count is a count of what is missing, not
of what is right**; this traded five countable misses for a correct configuration and a smaller real
work list.

**AND THE MAP HALF OF THE PHASE 3 SHIM NOW RUNS ON THE DEVICE** (2026-09-17,
[`experiment-134`](../experiments/experiment-134-pe-arm-init-interrupts-replacement.md)). The shim
already registered `tbd_ops` and drove the timer (experiment-104); what it had never done is the
*other* half of `pe_arm_init_interrupts` — the map step and the board-class dispatch. Both are now
implemented and exercised on hardware, **13 checks, 0 failures**, on the image built with
`-DSTAGE90_XNU_MSM8974_SHIM=1`.

The log carries the replacement's justification as numbers rather than a paragraph:

```
msm8974_map_soc_phys=0xf9000000          <- step 1 reproduced from /arm-io ranges[1]
msm8974_map_pic_base=0xf9000000          <- computed directly
msm8974_map_timer_base=0xf9020000
msm8974_map_apple_pic_base=0xf2000000    <- what Apple's soc_phys + reg[0] produces
msm8974_map_apple_timer_base=0xf2020000
msm8974_map_dispatch_would_return=0      <- spec section 1, evaluated on the device
```

Neither Apple formula lands where the hardware is; the two lines above each are where it is. That is
the same gap experiment-100 measured through XNU's own device-tree code, from the other side.

**What it does not establish:** it is not *called by XNU*. The payload calls it, as it calls the
`tbd_ops` registration, because XNU is not running. What is established is that the replacement is
correct and executable — the precondition for wiring it in. The remaining device-side piece is the
third thing `pe_arm_init_interrupts` does that is still not replaced: `tbd_fiq_handler`, which the
shim deliberately leaves NULL, because FIQ is the live path on this build and the payload has only
ever driven IRQ.

**AND THERE IS NOW AN XNU IMAGE** (2026-09-17,
[`experiment-135`](../experiments/experiment-135-first-xnu-image.md)). `tools/measure_link.sh` links
a **final** ELF against a script that places the kernel where `start.s` says it lives (`0x80000000`,
`physBase` 0, sections 1 MB-aligned for the section descriptors at `start.s:195-207`), stubbing every
symbol the build cannot provide as a weak function so the linker can do the part of its job that has
nothing to do with missing code.

| | text | data | bss | entry |
| --- | --- | --- | --- | --- |
| `RELEASE` | 4 179 211 B | 99 032 B | 299 644 B | `_start` @ `0x803a7074` |
| `STAGE90_BOOT` | 2 096 167 B | 52 984 B | 222 776 B | `_start` @ `0x801cd074` |

Three things it measured that `ld -r` cannot:

1. **Zero linker diagnostics other than undefined references, and `readelf -r` reports "There are no
   relocations in this file."** No branch out of range, no unrepresentable relocation, no section
   that will not place.
2. **XNU's real entry sequence is in the image and reads `boot_args` where the contract says** —
   `ldr r8, [r0, #8]` / `ldr r9, [r0, #4]` / `ldr sl, [r0, #12]`, the `virtBase`/`physBase`/`memSize`
   offsets `docs/reference/xnu-handoff-contract.md` derives and `tools/check_xnu_struct_abi.py`
   guards. `arm_init`, `arm_vm_init`, `machine_startup`, `kernel_bootstrap` and `rtclock_init` are
   all present as **real functions, not stubs**.
3. **It fits**: `RELEASE` spans 4.38 MB against the 93 MB `memSize` the conforming `boot_args`
   declares, with `.data` exactly 1 MB-aligned.

**It is not bootable and has not been put on the device.** 443 symbols are stubs that return 0 —
including `PE_putc` and `kprintf`, so a boot attempt would be a **silent hang with no log**, the
exact failure mode Phase 0 exists to eliminate. The distance to a boot attempt is therefore not
"make the link succeed" (it does) but **which of the 443 stubs must become real first**; the console
path is the one to do first, because without it every later step is unobservable.

**AND THE SIXTH GENERATOR WAS IN THE TARBALL TOO** (2026-09-17,
[`experiment-136`](../experiments/experiment-136-assym-and-a-correction.md)).
`stages/stage90/xnu_arm_boot/assym.s` — 48 lines, 17 defines — says in its own header that the real
one "is absent from the OSS tarball" and is "the single largest piece of the build configuration that
osfmk/arm's assembly needs". **Both halves are wrong**: `osfmk/arm/genassym.c` is the generator, and
`osfmk/conf/Makefile.template:184-189` is the rule that compiles it to assembly and scrapes the
`offsetof()` values out with sed. `tools/gen_assym.sh` reproduces that pipeline unmodified and
produces **266 defines**. It assembles three more of the manifest's `.s` files, including
**`locore.s`** — the exception vectors, `BootCpuData` and `CpuDataEntries`, which `_start` needs four
instructions in. The measurement link's stubs fall **443 → 427**.

The error it fixes reads like an assembler bug and is not: `locore.s:92`'s `error: register expected`
at `ldr r0, [r4, ASSIST_RESET_HANDLER]` was an **undefined name**, not bad syntax.

**And a correction to experiment-135, which claimed `PE_putc` and `kprintf` are among the stubs.**
They are not: `PE_putc` is a BSS *function pointer* (`pe_gen.c:107`) and `kprintf`, `vprintf`,
`cnputc`, `PE_init_printf` are all defined. The console path is present as code — sixth instance of
"a measurement can be the thing that is wrong", and the shape matters: **the claim was about a list,
and the list was in the tree.** The next step is narrower than it looked: `cnputc` comes from
Apple's ring-buffer `serial_console.c`, so where the bytes go depends on what drains it, and
`PE_init_printf` has to be *reached* for `PE_putc` to be non-NULL at all.

The generators-concluded-absent list is now six long, and every entry was in the tarball.

**AND THE WORK ORDER IS COMPUTED, NOT GUESSED** (2026-09-17,
[`experiment-137`](../experiments/experiment-137-work-order.md)). `tools/stub_reach.py` walks the
call graph in the measurement image, stops at each stub, and ranks what is left by distance from the
entry:

```
functions in the image:   8063
reachable from arm_init without hitting a stub:  1846
stubs the boot path reaches:  136 of 427

  1 edge   __aeabi_memcpy4   fiq_context_init   get_mmu_control   set_mmu_control
  2 edges  IODTGetDefault    bcopy   memcpy   flush_mmu_tlb   ml_get_timebase
           set_mmu_ttb   set_mmu_ttb_alternate
```

**`arm_init` reaches a stub on its first call** — one edge in, not after a filesystem or a
scheduler. And the stubs are not spread across 427: **`machine_routines_asm.s` defines 35 and
`data.s` defines 6**, and those two files are the deepest on the path. `data.s` alone carries
`BootCpuData`, `CpuDataEntries`, `intstack_top` and `fiqstack_top`.

**Both are blocked on one question, and it is not a value to choose.** `data.s:41` is
`.section __DATA, __data` — Mach-O section syntax, so GNU as answers `expected string in directive`.
`machine_routines_asm.s:696` is `.macro COPYIO_BODY` declared with no parameters and invoked with a
positional one referred to as `$0`; tested here, **both clang's assembler and `arm-none-eabi-as`
reject that form** (`Wrong number of arguments` / `too many positional arguments`), and declaring the
parameter does not rescue it. So it is the assembler dialect, and fixing it means changing XNU's
source or transforming its input — a decision, not a step.

Both come back to the choice experiment-123 measured and did not take: `--target=armv7-apple-darwin`,
which produces Mach-O (what `data.s` is written for) and which **nothing on this host can link** —
`/usr/lib/llvm-14/bin` has `llvm-nm`, `llvm-size` and `llvm-ar` but no `ld64.lld`. **41 of the 427
stubs, including the three nearest `arm_init`, are behind one question: Mach-O or ELF.** Worth
deciding deliberately rather than discovering.

**AND THE ASSEMBLY CONVENTION WAS A THIRD THING STANDING ON THE SAME QUESTION** (2026-09-17,
[`experiment-138`](../experiments/experiment-138-underscore-convention-and-work-list.md)).
`asm.h:88-98` gives two conventions behind one flag — `EXT(x) = _##x` for Darwin, `x` for ELF — and
this build sets `__NO_UNDERSCORES__` because the toolchain is ELF. **But ten of the manifest's
assembly files do not use `EXT()`**, they write `_bcopy:` literally, so they assembled *cleanly* and
defined the **wrong names**. The failure appears at link time as `undefined reference to 'bcopy'`
from files that plainly have an implementation. `tools/assemble_arm_layer.sh` renames `_x` to `x`
**when `x` is otherwise unresolved** — the condition that keeps it safe, since `start.s`'s `_start`
is not undefined and stripping it unconditionally lost the entry point. 22 symbols across 9 objects;
XNU's source untouched.

**And the work list is complete.** `tools/stub_blockers.py` attributes every boot-path stub:

| count | category |
| --- | --- |
| **88** | a file that fails to compile |
| **23** | assembly the build never attempts (`machine_routines_asm.s`, `data.s`) |
| **10** | C++ never attempted |
| 4 | compiler runtime (`__aeabi_*`) |
| 4 | no source in the tree |

and the 88 are **four files for 76 of them**: `vm_object.c` (27, the `const` member — not fixable
here), `uipc_mbuf.c` (24), `OSAtomicOperations.c` (15), `kern_event.c` (10).

**Two defects in this stage's own tools, same shape again**: `stub_blockers.py` first classified
`.s`/`.cpp` as "compiled" because it asked `failed.txt` and the build *skips* those extensions (30
symbols misreported), and its definition regex missed names at column 0 (85 misreported as "no
source"). Both fixed; the test is now "does an object exist", which is what the build answers.

**Image: 416 stubs, 130 on the boot path.** The Mach-O-vs-ELF question is untouched and still owns
the 23 deepest.

**AND XNU HAS TWO HEADERS NAMED `kern/ast.h`** (2026-09-17,
[`experiment-139`](../experiments/experiment-139-two-ast-headers.md)). `osfmk/kern/ast.h:63` and
`bsd/kern/ast.h:34` **guard themselves with the same `_KERN_AST_H_`** and are not variations of each
other — `AST_KEVENT_REDRIVE_THREADREQ` is in the BSD one only. With `osfmk` ahead of `bsd`,
`bsd/kern/kern_event.c:101`'s `<kern/ast.h>` finds the Mach header, takes the guard, and the BSD one
is skipped: `use of undeclared identifier` for a macro in the file the line above asked for.

Apple's order is per component and the file's own comes **first** (`MakeInc.def:463-469`), which is
what experiment-117 recorded a flat include list cannot reproduce — and this is the first case where
it *matters*. The two component roots now move per file, after the generated roots. **That position
is not free:** putting them ahead of MIG's output re-breaks the six `osfmk/vm` files
experiment-124 fixed — measured, and the first two attempts at this change did exactly that, one
appending the roots instead of prepending (no effect) and one putting them before everything (594 of
615, six regressions).

**`RELEASE` 599 → 600 of 615, `STAGE90_BOOT` 406 → 407 of 426, image 416 → 377 stubs, boot path
130 → 120.**

**AND THREE MORE NAMES, EACH ANSWERED NARROWLY** (2026-09-17,
[`experiment-140`](../experiments/experiment-140-three-missing-names.md)). `bsd/sys/kauth.h:113`
uses `uid_t` and `:118` `gid_t` and includes nothing that defines them; the definition arrives
through `sys/types.h`, which in `kern_ktrace.c`'s closure comes at line 128 against kauth.h's 107.
The answer is `-include sys/types.h` **for BSD files only** — the restriction being the substance,
since that header is what collides with `kern_types.h` over `clock_t`, and a BSD file never reaches
that definition (it is behind `MACH_KERNEL_PRIVATE`, experiment-118). `osfmk/kern/btlog.c:641`'s
`u_char` is the same shape and gets the same answer (`sys/_types/_u_char.h`, which defines `u_char`
and nothing else). **`RELEASE` 600 → 602, `STAGE90_BOOT` 407 → 409, image 377 → 362 stubs, boot path
120 → 113.** Left alone and named: `vnode_pager.c`'s `vnode_trim` is a *conflict*, not an absence,
and needs its two definitions compared rather than a header supplied.

**AND `size_t` IS THE FOURTH THING POINTING AT THE TRIPLE** (2026-09-17,
[`experiment-141`](../experiments/experiment-141-vnode-trim-is-the-triple.md)). `vm_protos.h:224`
declares `vnode_trim(..., unsigned long len)` and `vnode_pager.c:211` defines it with `size_t`, and
on this target `size_t` is `unsigned int` — reduced to a minimal case, clang calls that a hard error.
XNU's own `bsd/arm/_types.h:67-71` is `typedef __SIZE_TYPE__ __darwin_size_t` when `__SIZE_TYPE__`
is defined and `unsigned long` otherwise, so **Apple's build and this one each agree with their own
compiler and XNU's two headers disagree only under ELF**.

**And the command line cannot change it**: `-D__SIZE_TYPE__=long` is overridden — clang applies its
builtin *after* the command line, confirmed with `__builtin_types_compatible_p`, and adding the flag
to the whole build changes nothing (602 either way, no regressions, no new passes). So `vnode_trim`
is the Mach-O triple or a source edit, and this project does not edit XNU's source.

That is the **fourth** independent finding pointing at the same choice: the triple (119/123), the
Mach-O directives and macro dialect in `data.s`/`machine_routines_asm.s` (137), the underscore
convention (138), and now `size_t` (141).

**Also**: `osfmk/kperf/kperfbsd.c` is an osfmk file that includes BSD headers, so it sees both
`clock_t` definitions. `-D_CLOCK_T=1` is now given to **osfmk files only** — the same restriction and
reason as the BSD-only `-include sys/types.h` — which removes that conflict and moves the file on to
`ffs`/`fls`, i.e. experiment-118's defect from the other side, in a file that genuinely straddles the
Mach/BSD boundary. One file; no flag set satisfies both views.

**No count moved: `RELEASE` 602 of 615, boot path 113 stubs.**

**AND THE CHOICE IS NOW MEASURED, NOT ARGUED** (2026-09-17,
[`experiment-142`](../experiments/experiment-142-darwin-assembles.md)). Four findings had been
pointing at the target triple and every one was an argument; this is the demonstration. Same source,
same `assym.s`, same flags, only `--target` differs:

| file | `armv7-none-eabi` | `armv7-apple-darwin` |
| --- | --- | --- |
| `data.s` — `.section __DATA, __data` | fail | **OK** |
| `machine_routines_asm.s` — positional `$0` macro | fail | **OK** |
| `WKdmData_new.s`, `lz4_decode_armv7NEON.s` | fail | **OK** |
| the other 13 present | OK | OK |

**13 of 17 → 17 of 17.** And this corrects experiment-137, which tested the macro form against
clang's EABI assembler and `arm-none-eabi-as`, found both reject it, and concluded "no assembler on
this host does". **One does — the one the target selects.** The earlier test varied the flags and not
the target, which is the test that would have caught it.

It also retroactively explains experiment-138: `bcopy.s`'s literal `_bcopy:` is the **right** name
here, because `asm.h`'s `EXT(x) = _##x` is the convention; `-D__NO_UNDERSCORES__` is what makes it
wrong. Under this target the de-underscoring step would be unnecessary rather than merely harmless.

**What it does not settle**: the linker. Nothing here can link Mach-O — `/usr/lib/llvm-14/bin` has
`llvm-nm`, `llvm-size` and `llvm-ar` but no `ld64.lld`, and `apt-cache policy lld` offers a version
from this host's own repositories. So the question is not whether the toolchain exists but whether to
switch, which changes what every measurement so far was taken against. **`lld` was not installed: it
is a host toolchain change and the choice is the user's.** The measurement above needed no install —
clang's Mach-O *assembler* is already present.

**AND THE LARGEST REMAINING GROUP IS BOUNDED BY THE INCLUDE MODEL, NOT BY A MISSING NAME**
(2026-09-17, [`experiment-144`](../experiments/experiment-144-sync-qos-and-the-export-lists.md)).
The four `sync_qos_count_t` failures looked like the last three stages' work and are not. The chain is
`bsd/kern/uipc_mbuf.c` → IOKit headers → `osfmk/mach/mach_interface.h` → the MIG-generated
`mach/exc_server.h` → its `simport <kern/ipc_kobject.h>` → `osfmk/ipc/ipc_kmsg.h:111`, whose
`sync_qos_count_t` and `ipc_kmsg_t` live in `ipc_types.h` **under `MACH_KERNEL_PRIVATE`**.

Both obvious fixes are measured failures. **Forcing `MACH_KERNEL_PRIVATE` for BSD files** re-creates
exactly the defect experiment-118 fixed — it reaches `misc_protos.h`'s `ffs(unsigned int)` against
`bsd/libkern/libkern.h`'s `ffs(int)`; the error simply moves to `ffs`. **Supplying the two typedefs
narrowly** clears it and reveals `ipc_table_index_t`, then the next — *the closure grows as you
satisfy it*, as experiment-103 measured for `cpu_data_internal.h`.

**What the export lists say is the answer**: `osfmk/kern/Makefile`'s `EXPORT_FILES` does **not**
contain `ipc_kobject.h`, and `osfmk/ipc/Makefile` exports `ipc_types.h` but **not** `ipc_kmsg.h`. No
component exports the two headers this build reaches, so **no BSD file reaches them in Apple's build**
— it happens here because `-I$XNU/osfmk` puts the whole source tree on the include path, which is what
experiment-117 measured the cost of from the other direction.

**Third instance of one limitation**, and the three together name it: *a flat include list cannot
express "this component sees that header's public half and not its private half."* The other two are
`kern/ast.h`'s shared include guard (139) and `size_t` (141).

**State unchanged: `RELEASE` 602 of 615, boot path 113 stubs.**

**AND THE `simport` QUESTION IS ANSWERED — BY APPLE'S OWN TWO RULES** (2026-09-17,
[`experiment-145`](../experiments/experiment-145-the-simport-answer.md)). experiment-144 recorded the
hypothesis and said it was the first thing to check. It is confirmed, in `osfmk/mach/Makefile`:

```make
MIG_USHDRS : %_server.h : %.defs      $(MIG) $(MIGFLAGS) ... -sheader $@          # :231-238, EXPORTED
MIG_KSHDRS : %_server.h : %.defs      $(MIG) $(MIGFLAGS) $(MIGKSFLAGS) -sheader $*_server.h
                                                                                 # :372-380, NOT exported
```

`MIGKSFLAGS = -DMACH_KERNEL_PRIVATE -DKERNEL_SERVER=1` (`:247`) and the `simport` lines are behind
`#if KERNEL_SERVER` — so **the two rules produce different files under the same name**, and
`EXPORT_MI_GEN_LIST = ${MIGINCLUDES}` exports only the first. osfmk reads the build dir
(`INCFLAGS_LOCAL`, `INCFLAGS_GEN`) and gets the variant **with** the simports; every other component
reads export roots only (`INCFLAGS_IMPORT`) and gets the one **without** — which is what keeps a BSD
file out of `osfmk/ipc/ipc_kmsg.h`.

Implemented as both variants (`mach_headers/` and `mach_headers/kserver/`) with the `kserver` root
first for osfmk files only, the same per-file mechanism experiments 139/140 built. **The generated
`_server.c` moves with the KERNEL_SERVER header**, because it includes its own header with quotes
(`#include "mach_vm_server.h"`), which resolves next to the file — a `.c` in the export root picks up
the wrong header. **`RELEASE` 602 → 606 of 615, `STAGE90_BOOT` 409 → 412, image 362 → 277 stubs, boot
path 113 → 89, no regressions.**

**That change exposed one more instance of the project's oldest defect, caught by the linker**: the
first run after it failed on `multiple definition of iokit_server_routine` between the old and new
objects, because the build cleared `*.log` and never `*.o`. It now clears both.

**This is the first of the four component-dependent findings to be resolved by reproducing Apple
rather than by working around it** — 139 (`ast.h`), 140 (`sys/types.h`), 141 (`size_t`) and this one
all had the same shape, and this one had an answer in the rules.

**AND A CASE DIFFERENCE WAS THE WHOLE DEFECT** (2026-09-17,
[`experiment-146`](../experiments/experiment-146-arm-case-and-the-last-three.md)).
`bsd/kern/kern_sysctl.c:2772` is `#if defined(__ARM__)`; the build defined only the compiler's
lowercase `__arm__`, which the rest of the ARM tree uses. So the `#else` was taken, the 64-bit
`SYSCTL_QUAD` form was applied to 32-bit values, and the error was
`'_sysctl__vm_global_no_user_wire_amount_size_check' declared as an array with a negative size` —
**a message that names neither the macro nor the file that uses it.** One define; `RELEASE` 606 →
607 of 615, `STAGE90_BOOT` 412 → 413 of 426. Ninth instance of the one-value-two-definitions class,
and its cleanest small case.

**And two of the remaining eight cannot be fixed by any flag**, which is worth stating as a
category rather than as a backlog:

- **`subr_prof.c:160-163` is malformed source** — `STATIC` is defined nowhere except in two other
  `.c` files as a file-local macro, and the next line is a function definition nested inside the
  first one's body. **Identical in `xnu-upstream`**, so it is a long-standing malformation in the OSS
  drop. The second file in this project that no flag can fix, after `vm_object.c`'s `const` member.
- `if_bridge.c`'s `DLT_EN10MB` is diagnosed and pending: the macro is unguarded in a header the file
  includes, so the include is resolving elsewhere; it needs the same reading as the two above.

**The eight left are: 2 needing the toolchain decision, 2 unfixable by any flag, 1 needing a value
the tarball does not supply, 1 diagnosed, and 2 more in the same categories.** That is the honest
shape of what remains on the host side.

**AND THE LAST OF THE EIGHT BELONGS TO A CATEGORY OF THREE** (2026-09-17,
[`experiment-148`](../experiments/experiment-148-nbpfilter-and-the-last-category.md)).
`bsd/net/if_bridge.c:134` is `#if NBPFILTER > 0 / #include <net/bpf.h> / #endif`, and `:1419` uses
`DLT_EN10MB` — **outside that guard**. `NBPFILTER` is not an option: `net_osdep.h:212-215` records it
in the source as "number of bpf pseudo devices: others: bpfilter.h, NBPFILTER", i.e. **a count like
`NLOOP` and `NPTY`**. And `RELEASE` sets `if_bridge` while not setting `bpfilter`, so the include is
skipped and the unguarded use fails. `gen_device_headers.sh` now writes `bpfilter.h` with
`NBPFILTER 0` and records what 0 costs; **the file cannot be made to compile by a header value**,
because turning the count on requires the option and its dependencies.

**Three files now fail this way** — a source path valid only with an option built, in a file that
builds either way: `conf.c` (`NPTY 0`'s branch is missing `ptsselect`, experiment-130), `if_bridge.c`
(this), `if_loop.c` (`optional loop`, experiment-127). **All three are answerable only by a
configuration decision**, not a header, a flag or an include order.

**The final eight, no two alike**: `vm_object.c` and `subr_prof.c` (**not fixable here**);
`vnode_pager.c` and `kperfbsd.c` (the toolchain / include-model decisions); `OSAtomicOperations.c`
(needs Apple's header order, unknown); the firehose pair (**a value with no evidence in the
tarball**); `if_bridge.c` (a configuration decision). **That is 607 of 615, and it is the end of
host-side compile work as a source of movement.**

**AND THE TOOLCHAIN CHOICE NOW HAS NUMBERS ON BOTH SIDES** (2026-09-17,
[`experiment-149`](../experiments/experiment-149-macho-path-measured.md)). `tools/build_xnu_arm_macho.sh`
builds the `armv7-apple-darwin` path as far as it goes **without installing anything**, so the
decision is priced rather than argued. Nothing was installed; the ELF build is untouched and the two
outputs do not read each other.

| | ELF | Mach-O |
| --- | --- | --- |
| manifest `.s` that assemble | 13 of 17 | **17 of 17** |
| manifest `.c` that compile | **607 of 615** | 602 of 615 |
| boot-path stubs the assembly closes | — | **23 of 89, and they are the 23 nearest `arm_init`** |

`BootCpuData`, `CpuDataEntries`, `intstack_top`, `fiqstack_top`, `get_mmu_control`,
`set_mmu_control`, `fiq_context_init`, `ml_get_timebase` and 15 more now exist as Mach-O objects on
this host, verified with `llvm-nm` — symbols the ELF build **cannot produce at all**.

**The cost is measured too, and it is not the triple.** The C side started at 252 and reached **601**
as the ELF path's mechanisms were ported; the six-file gap is what remains of them. **Every mechanism
is target-independent.** Only `--link` waits for anything. Only `--link` waits for anything: there is no `ld64.lld` on this host, and the
script refuses with that reason rather than producing something half-done.

**And the script reproduced two of this project's own defects before it worked** — `-DMACH_KERNEL_PRIVATE` global (271 files on `ffs`, defect 118) and `-D_CLOCK_T` global (`clock_t`, defect 126) — taking it 252 → 544 → 601 as each moved into the row it belongs to. — `-DMACH_KERNEL_PRIVATE` in the global
defines, 271 files failing on `ffs`. One move to the per-component set took it 252 → 544. The fix
existed, was written down, and a new file did not inherit it: a build configuration living in two
scripts will drift.

**AND THE ONE THING THAT ARGUED AGAINST IT IS ONE FLAG.** `bsd/net/dlil.c:1419`'s
`IF_DATA_REQUIRE_ALIGNED_64(ifi_ipackets)` — **an assertion in XNU's own source** — fails under a
plain `armv7-apple-darwin`, because clang 14's Darwin ARM ABI aligns `long long` to 4 and XNU asserts
8. Measured: `armv7-none-eabi` gives 8, `armv7-apple-darwin` gives 4, and **`armv7-apple-darwin
-mabi=aapcs` gives 8** — AAPCS is what Apple's toolchain used and clang 14 has to be told. With that
flag `dlil.c` compiles and the Mach-O C count is **602 of 615**. So option A is *"the Darwin target
with AAPCS, plus a Mach-O linker"* — two nameable things, no unknowns.

**B and C do not advance the goal.** The boot path's nearest 23 stubs are blocked on the triple, and
A - done properly - is the only option that moves them.

**AND THE TOOLCHAIN QUESTION IS CLOSED BY MEASUREMENT, WITH THE OPPOSITE ANSWER TO THE ONE THE
ANALYSIS POINTED AT** (2026-09-17,
[`experiment-150`](../experiments/experiment-150-lld-cannot-link-armv7-and-the-dialect-translation.md)).
Three turns of findings said the Mach-O target was the way to the boot path's 23 deepest stubs.
Installing the linker made it checkable:

```
$ ld64.lld -arch armv7  ... -> error: unhandled relocation type
$ ld64.lld -arch arm64  ... -> Mach-O 64-bit arm64 executable
```

**LLVM's `ld64.lld` links arm64 Mach-O and does not link armv7 Mach-O**, in 14 and in 15. So the
Darwin target can *assemble* XNU (experiment-142) and **cannot link here** — option A was not a choice
between two viable paths but a path that stops at the linker. Each of the three findings before it was
right about the *cause* and wrong about the *remedy*.

**So the four files were translated instead**, and the constructs are four — verified against the EABI
assembler one at a time and applied by `tools/translate_arm_asm.py`, which is a parser over `.macro`
blocks rather than a per-file patch: any parameterless `.macro` whose body uses `$N` gets the
parameters it is evidently called with. **A dialect translation, not a source change** — the tree is
never written to, and it sits in the same category as the `objcopy --redefine-sym` step that exists
because `asm.h`'s `EXT(x)` is `_##x`.

```
manifest .s that assemble for ELF   13/17 -> 17/17
stub symbols in the image            282 -> 240
boot-path stubs                       89 ->  66
"assembly the build never attempts"   23 ->   0
```

**And the de-underscore step had to be made deterministic**: it keyed on the linker's undefined list,
so the assembler depended on the *previous* link run — on a clean tree 2 of 22 symbols were renamed
and `bcopy` stayed a stub, and the identical command run again renamed 22. **A build step whose result
depends on how many times it has been run.** The rule is now the flag's own (rename `_x` to `x` except
`_start`), with no list and no ordering.

`BootCpuData`, `CpuDataEntries`, `get_mmu_control`, `set_mmu_control`, `fiq_context_init` and
`ml_get_timebase` are **defined in the ELF objects now** — the same eight the Mach-O path produced, and
the same ones a boot reaches first.

**And that closes the decision.** A's only remaining advantage is `vnode_trim`'s `size_t`, one file,
against a toolchain that cannot link here and thirty stages of mechanisms to port. **Option B now
reaches everything A reached except one file.**

**AND AN ELEVENTH INSTANCE OF THE TWO-DEFINITIONS CLASS — IN A MACRO** (2026-09-17,
[`experiment-151`](../experiments/experiment-151-force-includes-and-stdbool.md)).
`libkern/gen/OSAtomicOperations.c:33` is `enum { false = 0, true = 1 }` and fails because
`EXTERNAL_HEADERS/stdbool.h:36-37` has already defined those as macros. Traced through the
preprocessed output: `mach/vm_param.h:79` (`#ifdef KERNEL`) → `libkern/os/overflow.h:45` →
`stdbool.h`, reached from `mach/thread_policy.h` → `thread_info.h` → `clock_types.h` → `vm_region.h` →
`dyld_kernel.h` — **a header this build force-includes for every file.** Apple's build force-includes
nothing; this project's ten-header set stands in for what `MakeInc.*` supplies, and its cost had never
been measured.

**The scope was chosen by measurement**: a `stdbool.h` defining `bool` but not the two macros fixes
this file and **breaks 78 others** (530 of 615), because 78 files write `true`/`false` and need them.
Applied to **this file only**: 608 of 615, no regressions. **Not a global setting and not a flag — a
per-file one**, with the mechanism `COMP_FIRST` already uses.

**It buys the 15 boot-path stubs `OSAtomicOperations.c` provides** — `OSAddAtomic`,
`OSCompareAndSwap`, `OSIncrementAtomic`, the primitives the scheduler and locks sit on:

```
RELEASE        607 -> 608 of 615      STAGE90_BOOT 413 -> 414 of 426
image stubs    240 -> 224             boot path     66 ->  51
```

**And every one of the seven remaining `RELEASE` failures is now a stated limitation rather than an
unfinished item** — two need a source edit this project does not make, one the target triple, two a
value with no evidence in the tarball, one a configuration decision, one a file that straddles Mach
and BSD. The boot path's 51 stubs are the same: 10 C++ (a runtime not built here), 4 `__aeabi_*` from
libgcc, the rest behind those seven files. *(Superseded the next day: the C++ block was built and is
75 of 83, the boot path is 46, and the "10 C++ never attempted" row is 0 — experiment-154, below.)*

**AND THE C++ BLOCK IS AT 75 OF 83, AND THE "ONE DEAD LINE" WAS NEVER IN THE TREE** (2026-09-18,
[`experiment-154`](../experiments/experiment-154-cpp-entered-the-build.md)). This entry replaces one
written the day before, which said all 83 `.cpp` files failed on `osfmk/kern/misc_protos.h:254` and
that "no flag can fix it". **Both halves were wrong, for the same reason: the number came from a
hand-written compile loop that passed `-DMACH_KERNEL_PRIVATE` and `-DMACH_KERNEL` for every file.**

Those two are *component* defines (`xnu_config/component_defines.sh`), and no `libkern` or `iokit`
file gets either. `kern_types.h:190-192` reaches `misc_protos.h` only under `#ifdef
MACH_KERNEL_PRIVATE`, and `IOTypes.h:148`'s `#ifndef MACH_KERNEL` block is where `io_object_t`,
`io_connect_t`, `io_service_t` and the `<device/device_types.h>` include all live. So the "dead line"
was reached by a flag this project invented, and the fix it was said to need — a one-line edit to
Apple's source — would have bought nothing. Reproduced with the real build, one command:

```
$ XNU_KERNEL_EXTRA_DEFINES=-DMACH_KERNEL_PRIVATE=1 ./tools/build_xnu_arm_kernel.sh --dir libkern
  C++ compile: 0 of 22     all 22: osfmk/kern/misc_protos.h:254  'kmod_info_t'
$ XNU_KERNEL_EXTRA_DEFINES=-DMACH_KERNEL=1 ./tools/build_xnu_arm_kernel.sh --dir iokit
  C++ compile: 51 of 61    io_buf_ptr_t 12, io_connect_t 8, io_service_t 4, io_iterator_t 2
```

**And the 83 `.cpp` files are now compiled by the build itself** — they were skipped by
`build_xnu_arm_kernel.sh` with the comment that they "need libkern's C++ runtime, which is a separate
and larger problem than this measures". That is true of the *link* and false of the *compile*: a
`.cpp` in `libkern` is a `libkern` translation unit and needs the same per-component table, the same
generated roots, the same include order and the same shims, with `clang++` in place of `clang`. The
script counts them separately now, and `XNU_KERNEL_EXTRA_DEFINES` exists so a controlled comparison is
one command rather than a private flag list.

| | before | after |
| --- | --- | --- |
| `RELEASE`, C | 608 of 615 | 608 of 615 |
| `RELEASE`, C++ | **not attempted** | **75 of 83** |
| `STAGE90_BOOT`, C++ | not attempted | **75 of 83** |
| boot-path stubs (`RELEASE`) | 51 | **46** |
| C-side undefined symbols in the image | 224 | **209** |

**And putting the C++ objects into the image turned up three more of the same defect in the tools**:
the shim's declarations had C++ linkage (`.cpp` asked for `_Z5bzeropvj` while the kernel defines
`bzero` — 14 undefined symbols whose names are C signatures; fixed with `extern "C"`);
`measure_link.sh` read the linker's diagnostics **demangled** and then tried to assemble stubs named
with the demangled spellings, so it silently stubbed none of the C++ symbols and reported "the linker
refused even with every undefined symbol stubbed" under a list of undefined references (fixed with
`--no-demangle`); and `stub_blockers.py` looked mangled names up as text, so 19 stubs landed in
"no source in the tree", which means *write a driver* — a demangling lookup puts them behind
`IOService.cpp` and `IOUserClient.cpp` where they belong.

The gap was five declarations in `shims_arm/string.h` — `bcopy`, `bzero`, `bcmp`, `strlcpy`,
`strlcat`, plus `strchr`. They are not missing from the tree (`osfmk/libsa/string.h:72,73,93,94,95`
declares them); they were missing from this build, because `libkern/c++/*.cpp` includes `<string.h>`
and `EXTERNAL_HEADERS/` ships no `string.h` at all, so `<string.h>` is the shim. Worth **29 files**
(46 → 75 of 83), and on `libkern` alone 7 → 19 of 22.

The eight that remain: three on `vm_deallocate`/`mach_vm_deallocate` — which is a **missing `-DKERNEL`
in the MIG input**, not a source problem (`vm_map.defs:132` is `#if !KERNEL && !LIBSYSCALL_INTERFACE`
around the routine, and `grep -c deallocate out/mach_headers/mach/vm_map.h` is **0**) — plus
`thread_policy_set`, two out-of-line `enqueue` signature disagreements, `kext_request`'s language
linkage, and `operator new[]`'s `size_t` (the target triple, same as `vnode_pager.c`). The MIG one is
the next stage.

**One thing from the retracted entry survives**: the shim's `NULL` was `((void *)0)`, which is not a
null pointer constant in C++, so every `return NULL;` in a `.cpp` was an error; it is `0` under
`#ifdef __cplusplus` now. The *cause* of the C++ failures came from the private flag list; that defect
came from a `.cpp` the flag list could not stop from being compiled. A measurement can be an artifact
and still have something real inside it.

**`vm_object.c` COMPILES, AND 58 OF THE 189 CLOSE WITH IT** (2026-09-18,
[`experiment-160`](../experiments/experiment-160-vm-object-compiles.md)). experiment-158 put one file
at the top of the work order — `osfmk/vm/vm_object.c`, 58 of the 189 undefined, referenced by
`vm_map.o`, `vm_pageout.o`, `memory_object.o` and `bsd_vm.o`. experiment-127 had already examined that
file and closed it: `*object = vm_object_template;` at `:355` assigns a whole struct whose
`vm_object.h:174` member is `const`, C11 6.3.2.1p1 makes that a constraint violation, and "no flag,
include order or macro on this host can make legal". **Both halves of that are still true** — the flag
battery was re-run wider (`-fms-extensions`, `-fms-compatibility`, `-fno-strict-aliasing`, every
`-std` from gnu89 to gnu17, `arm-none-eabi-gcc`) and none of it helps, because the diagnostic is an
**error with no `[-W...]` group**. What experiment-127 did not have was the denominator: it measured
`boot_closure.py`'s boot-path attribution, where the file is worth 2 symbols of 62, and wrote "a fair
measure of how much it matters". Against a whole-kernel link it is worth 58 of 189.

What does reach it is a macro named `const` with an empty body — `-Dconst=`. `nm --defined-only` on
the object gives 152 symbols and exactly 58 of them are in the 189, which confirms experiment-158's
attribution symbol by symbol. Two negative controls decided the shape, and both were run:

- **Scoped to the header it fails.** A force-include that neutralizes `const` only while
  `vm/vm_object.h` is read leaves the rest of the unit const-qualified, and a declaration reached
  inside that window then disagrees with the same declaration reached outside it:
  `stages/stage90/shims/kern/debug.h:6` and `osfmk/kern/debug.h:423` both declare `Debugger`, one
  `const char *` and one `char *` — "conflicting types for 'Debugger'". Placed *first*, so that
  everything would be inside the window, it fails earlier on `osfmk/kern/sched.h:214`'s `u_int`. So
  the flag has to precede the first token, and it is an ordinary `-D`, not a header.
- **Applied to the directory it costs two files.** `osfmk/vm/lz4.h:68` is `static const size_t
  lz4_encode_scratch_size = lz4_hash_table_size;`, an initializer that *needs* `const` to be a
  constant expression, and dropping it turns `vm_compressor_algorithms.c:52`'s array into a
  variable-length one. `--dir vm`: 27 of 29 without the flag, 26 with it directory-wide, **28 with it
  for `vm_object.c` alone**.

| | before | after |
| --- | --- | --- |
| `RELEASE`, C | 608 of 615 | **609 of 615** |
| undefined after a whole-kernel link | 189 | **132** |
| boot-path stubs from `arm_init` | 42 of 189 | **15 of 132** |
| reachable from `arm_init` without a stub | 2054 | **2119** |

Both link routes agree on the after state (132 by `nm -u` and 132 by `measure_link.sh`), and the
before state was re-measured the same way by moving the one new object out and re-linking, rather than
being read off the previous experiment. 58 symbols closed and **one opened** —
`vnode_pager_issue_reprioritize_io`, which `vm_object.c` references under `CONFIG_IOSCHED` and which
`bsd/vm/vnode_pager.c` defines: a file entering the link brings its references with it.

**And the next file is a different kind of problem.** `bsd/vm/vnode_pager.c` is the last failure in the
directory, and preprocessing it shows the return types agree (`u_int32_t`, `uint32_t` and `__uint32_t`
all `unsigned int`) while the **third parameter** does not: `unsigned long` in `vm_protos.h:224`
against `size_t` in the definition. `size_t` is `unsigned int` here because `bsd/arm/_types.h:67-71`
takes `__SIZE_TYPE__`, which clang defines per target — `long unsigned int` for `armv7-apple-ios`,
`unsigned int` for `armv7-none-eabi`, which is this project's triple because experiment-150 closed the
Mach-O path. So the remaining work has a component that is not a file at all but the **target's ABI**,
and the C++ list already names it ("`operator new[]`'s `size_t`, the target triple, same as
`vnode_pager.c`"). It is one command to measure:

```bash
XNU_KERNEL_EXTRA_DEFINES='-D__SIZE_TYPE__=long unsigned int' ./tools/build_xnu_arm_kernel.sh
```

**THE TARGET ABI: AN ELF TRIPLE THAT CARRIES DARWIN'S TYPE WIDTHS** (2026-09-18,
[`experiment-161`](../experiments/experiment-161-the-target-abi.md)). experiment-160 left one file in
`osfmk/vm` failing, and its failure turned out not to be about that file. `vm_protos.h:224` declares
`vnode_trim(struct vnode *, int64_t, unsigned long)` and `vnode_pager.c:211` defines
`vnode_trim(struct vnode *, off_t, size_t)`. Preprocessing shows the return types agree — `u_int32_t`,
`uint32_t` and `__uint32_t` are all `unsigned int` — and the **third parameter** does not, because
`size_t` here is `unsigned int`. `bsd/arm/_types.h:67-71` takes `__SIZE_TYPE__`, and clang defines that
**per target**: `long unsigned int` for `armv7-apple-ios`, `unsigned int` for `armv7-none-eabi`. The
source is not at fault; the compiler's idea of the target is. The project had already recorded this
from the other end — "`operator new[]`'s `size_t` (the target triple, same as `vnode_pager.c`)".

`-D__SIZE_TYPE__='long unsigned int'` fixes the C half (610 of 615) and **destroys the C++ half**: 80
of 83 becomes **3 of 83**, every file failing on `OSMetaClass.h:922`'s `operator new(size_t)` —
because clang checks that parameter against the *target's* built-in size type and not against the
macro. So the target moved instead. Four macros differ between the triples
(`__SIZE_TYPE__`, `__UINTPTR_TYPE__`, `__INTPTR_TYPE__`, `__WCHAR_TYPE__`); `armv7-apple-ios` carries
all four and is Mach-O, which experiment-150 closed. Of the ELF triples with those widths,
`armv7-unknown-netbsd-eabi` is chosen over `armv7-unknown-openbsd` because the macro delta is smaller
(OpenBSD additionally claims `unix`, `__PIC__`/`__PIE__`, `__SSP_STRONG__` — none read anywhere in the
tree, and `-fno-pic` beats its PIE default byte for byte, so this is a preference and not a
measurement). The one branch in the tree that reads either name is `bsd/netinet/ip_compat.h:122`, and
it selects the same `typedef u_int32_t u_32_t` as the path it replaces.

**One value, four places.** The triple was spelled out in `build_xnu_arm_kernel.sh` (C *and* C++),
`build_xnu_arm_layer.sh`, `sweep_xnu_osfmk.sh` and `gen_assym.sh`. It now lives in
`tools/xnu_config/arm_target.sh`, beside `component_defines.sh` and `make_defines.sh` — the same kind
of thing, a value the build configuration would have supplied. It honours `XNU_ARM_TARGET`, so the
before case is `XNU_ARM_TARGET=armv7-none-eabi ./tools/build_xnu_arm_kernel.sh`.

| | before | after |
| --- | --- | --- |
| `RELEASE`, C | 609 of 615 | **610 of 615** |
| `RELEASE`, C++ | 80 of 83 | **82 of 83** |
| `RELEASE`, undefined | 132 | **97** |
| `RELEASE`, boot-path stubs from `arm_init` | 15 of 132 | **12 of 97** |
| `STAGE90_BOOT`, C | 415 of 426 | **416 of 426** |
| `STAGE90_BOOT`, C++ | 80 of 83 | **82 of 83** |
| `STAGE90_BOOT`, undefined | 273 | **238** |

40 symbols closed and 5 opened, and both lists are what they should be: the 40 are `vnode_pager.c`'s
9, `libkern/c++/OSKext.cpp`'s 31 and `OSRuntime.cpp`'s 3 plus `__cxa_pure_virtual`, and the 5 are
everything `libkern_c++_OSKext.o` references. Three costs, all measured: `__NetBSD__` becomes defined,
`__PTRDIFF_TYPE__` becomes `long int` where Darwin has `int`, and the EH model changes from ARM EHABI
to DWARF, so **no `.ARM.exidx` sections are emitted** (the old build emitted 113 orphan ones) — nothing
in XNU, in the measurement link or in the entry image reads them, and that is where the 46 KB of
`.text` went. Verified unchanged: `build_xnu_arm_layer.sh` at 32 of 32 and 445 undefined,
`gen_assym.sh`'s `assym.s` byte-identical at 266 defines.

**THE FIREHOSE SEAM IS A COMPONENT, NOT A VALUE** (2026-09-18,
[`experiment-162`](../experiments/experiment-162-the-firehose-seam-is-a-component.md)). The largest
remaining block was 24 symbols from `bsd/kern/subr_log.c` plus 6 from `libkern/os/log.c` — the
`/dev/log` and `/dev/oslog` device entry points, `log_putc`, `msgbufp`, `oslog_init`,
`_os_log_internal` — both `standard` in their `conf/files`, both held shut by one undeclared
identifier. Four earlier experiments (132, 148, 151, 153) recorded it as *a value with no evidence in
the tarball*. That is true of the tarball and false of Apple's published sources: `os/firehose_buffer_private.h`
is a **libdispatch** file (`apple-oss-distributions/libdispatch`), and its `#ifdef KERNEL` block
declares the four functions no file in the tarball defines, with the signatures `log.c:426`,
`log.c:462`, `subr_log.c:874` and `subr_log.c:813` call, argument for argument. **The interface is
published; only the implementation is not.**

The value itself is published twice over: `libdispatch-913.30.4`'s copy has
`#define FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT 16`, and the next generation (from
`libdispatch-1008.200.78`, `OS_FIREHOSE_SPI_VERSION 20180226`) turned it into a runtime variable while
keeping 16 as both `MIN` and `DEFAULT`. So the shim header the project already had — the one whose
own comment called its contents *derived* — is a real file, and its `#ifdef KERNEL` half is what the
two files were missing.

| | before | after |
| --- | --- | --- |
| `RELEASE`, C | 610 of 615 | **612 of 615** |
| `RELEASE`, C++ | 82 of 83 | 82 of 83 |
| `RELEASE`, undefined | 97 | **72** |
| `RELEASE`, boot-path stubs from `arm_init` | 12 of 97 | **14 of 72** |
| `STAGE90_BOOT`, C | 416 of 426 | **418 of 426** |
| `STAGE90_BOOT`, undefined | 238 | **229** |
| `STAGE90_BOOT`, boot-path stubs | 24 | **27** |

30 closed and 5 opened, and the 5 are the four `__firehose_*` functions plus `OSKextKextForAddress`,
which `log.c:59` declares. **`STAGE90_BOOT`'s boot-path stub count goes up**, 24 to 27, and that is
the measurement to keep: a whole-kernel link leaves fewer undefined symbols while a *boot* now reaches
the code that entered the image, and `kprintf` reaches `_os_log_to_log_internal` for the first time.
The three new ones are `__firehose_buffer_create` at distance 4 (`oslog_init <- kernel_bootstrap`) and
the reserve/flush pair at 5. They are safe in the measurement image — every stub returns 0, `reserve`'s
0 is `NULL` and `log.c:427-447` already has a `NULL` branch that falls back to `firehose_boot_chunk`,
`flush` is only reached on a non-`NULL` reservation, `merge_updates` is only called from the
`/dev/oslog` ioctl, and `create`'s 0 lands in a global `__firehose_allocate` tests before use and that
has no caller in the tree — which is what makes the next stage a **port** rather than a repair:
`libdispatch/src/firehose/firehose_buffer.c`, 1188 lines at `libdispatch-913.30.4`, with a
`#ifdef KERNEL` half. Verified unchanged: the ARM layer at 32 of 32 and 445 undefined, and both link
routes agreeing (72 and 72; 229 and 229).

**THE NEAREST BLOCKER WAS NOT XNU'S** (2026-09-18,
[`experiment-163`](../experiments/experiment-163-the-nearest-blocker-was-not-xnu-s.md)). On the image
experiment-162 left, `stub_reach.py --from arm_init` put `__aeabi_memcpy4` at **distance 1** — the
closest missing symbol in the whole image, called by `arm_init`'s first aggregate copy. Five of the
fourteen boot-path stubs were `__aeabi_*`, and `stub_blockers.py` had been filing all five under its
own "compiler runtime, not source" heading since the first ELF link without anything acting on it.

They are the price of the ELF path: `armv7-apple-ios` lowers an aggregate copy to `bl memcpy` and an
EABI target lowers the same source to `bl __aeabi_memcpy4` — `grep -rn "__aeabi"` over the whole
tarball is empty because Apple's target is not EABI, and the Mach-O target was closed by
experiment-150. So the cost belongs to experiment-150's decision and not to experiment-161's table
of three.

The four memory intrinsics are now `stages/stage90/xnu_aeabi_runtime.c` — twelve tail calls, **108
bytes of `.text`**, with `__aeabi_memset`'s EABI `(dst, n, c)` argument order written as specified
rather than as it looks. The five arithmetic helpers are **`libgcc.a`**, linked rather than copied:
the ARM-state multilib is chosen deliberately (`thumb/v7ve+simd/softfp` matches the build's
`-mfpu=neon-vfpv4` and is **Thumb**, where the whole image is ARM), and the FP-ABI check that makes
that safe is measured by disassembly — the `__aeabi_*` helpers take their `double` arguments in
**r0:r1** under both `soft` and `softfp`, so a `softfp` caller and a `soft` callee agree.

| | before | after |
| --- | --- | --- |
| `RELEASE`, undefined | 72 | **63** |
| `RELEASE`, compiler runtime among them | 9 | **0** |
| `RELEASE`, boot-path stubs from `arm_init` | 14 of 72 | **9 of 63** |
| `RELEASE`, `.text` | 4908480 | 4911088 |
| `STAGE90_BOOT`, undefined | 229 | **220** |
| `STAGE90_BOOT`, boot-path stubs | 27 of 229 | **23 of 220** |
| `STAGE90_BOOT`, `stub_blockers.py`'s compiler-runtime row | 4 | **0** |

9 closed and 0 opened in both, and both link routes still agree (63/63 and 220/220). Two measured
costs: 40 symbols enter the image for the 9 asked for (the closure is soft-float double arithmetic
the ARM-state libgcc builds `_fixunsdfdi.o` in terms of — +2608 bytes), and ld warns twice that
`_fixunsdfdi.o`/`_udivmoddi4.o` use variable-size enums, which is inert because neither member's
interface is an enum.

**This stage also corrected a figure of its own**: experiment-162's `RELEASE` boot-path row said
7 of 72, and 7 was the number of rows a `tail -20` showed. `stub_reach.py` prints the count in a
header and up to `--list` (15) rows nearest the entry **first**, so truncating from the top hides
the smallest distances and leaves the count unread. The true figure is **14**, re-measured by
rebuilding that exact image, and experiment-162's table now says so. Eighth instance of the class
`docs/status/roadmap.md` keeps meeting — the tool was right and the reading was not.

**THE WHOLE OF `task.c` WAS BEHIND ONE `#ifdef`** (2026-09-18,
[`experiment-164`](../experiments/experiment-164-an-off-option-has-two-spellings.md)). Twelve of the
twenty-three boot-path stubs were `osfmk/kern/task.c`, and that file failed to compile for one
error: `task_collect_crash_info`'s `crash_label` argument is guarded by `#if CONFIG_MACF` in
`osfmk/kern/task.h:635` and by `#ifdef CONFIG_MACF` in `osfmk/kern/task.c:1600`. Apple's
configurations all set `CONFIG_MACF=1`, where the two spellings agree — and upstream `main` still has
the `#ifdef` a decade later — so this is latent in Apple's own tree, and the project's minimal
configuration is the one that turns MACF off and finds it.

`mkheaders.c` writes `#define <MACRO> 0` for every option a configuration does not select, which is
faithful and is reproduced faithfully here, but `#if X` reads an undefined `X` as 0 while `#ifdef X`
does not. With the option off there is no value that satisfies both spellings, so no flag can fix it:
the prototype and the definition differ by one argument either way. The fix is one `#undef` line
appended to the generated `meta_features.h` — and it has to be **global**, because
`osfmk/kern/task.h:241` guards `struct task`'s own `crash_label` field with a third spelling of the
same option, so a per-file `-D` would give one translation unit a differently-shaped `struct task`
than the rest of the kernel.

The obvious generalisation was implemented first and measured, and it is a trap: undefining *every*
off option also undefines `NFSCLIENT`, and `bsd/sys/mount_internal.h:243` guards the **definition**
of `MNTK_TYPENAME_OVERRIDE` with `#ifdef NFSCLIENT` while six uses of it in
`bsd/vfs/vfs_syscalls.c` are unguarded — so Apple's tree compiles with NFS off only because
`#define NFSCLIENT 0` makes that `#ifdef` true. Two macros, two opposite dependencies on the same
spelling. A count that came out the same either way (419 of 426) hid a file swapped for a file; the
file list is what showed it. `GPROF` is the next candidate on the same evidence and is deliberately
not taken: it is off in `RELEASE` too, so its cost would land in four files there and has not been
measured.

`STAGE90_BOOT`: undefined **220 → 114**, boot-path stubs from `arm_init` **23 → 11**, the
"file that fails to compile" category **14 → 2**. 106 symbols closed, 0 opened, all from task.c —
the task interface entire, and `task_init` itself, which was the boot path's second-nearest stub at
distance 3. `RELEASE` is provably unchanged: the regenerated option headers `diff -r` empty, since
the line is only written for a macro that is off and CONFIG_MACF is on there. No line of XNU's source
was changed, no configuration option was turned on, and no compiler flag was added — the whole change
is one `#undef` in a generated header, which is the cheapest thing this project has done since it
started counting.

The largest remaining category is now **"a file not in the manifest"**: `libkern/crypto/corecrypto_md5.c`
(three stubs at distance 7), `san/memintrinsics.h`'s `__nosan_bzero` (7) and
`osfmk/chud/chud_xnu.h`'s `chudxnu_thread_get_callstack64_kperf` (9). The nearest thing a boot now
hits is `bsd_scale_setup` at distance 3, from `bsd/dev/unix_startup.c`, and its error is a different
class: `bsd/netinet/in_pcb.h` reached without `<netinet/in.h>`. The firehose port, which experiments
162 and 163 both ranked first, is now behind two smaller things.

**TWO FILES, ONE LINE EACH: THE C++ BLOCK IS COMPLETE** (2026-09-18,
[`experiment-165`](../experiments/experiment-165-two-files-one-line-each.md)). The last of the 83
`.cpp` was `libkern/OSKextLib.cpp`, with three errors and one cause. `kext_request` is a `friend` of
`OSKext` (`OSKext.h:189`) and is *defined* inside `OSKextLib.cpp:39`'s `extern "C" {` block; with no
earlier declaration clang reads the friend as C++ linkage and the definition as C, and the two
follow-on errors about `OSKext`'s private statics are consequences — a `friend` declaration grants
access to *that function*, so a `kext_request` clang does not believe is the friend has no access
either. A function's linkage is fixed by its first declaration, so a shim that declares Apple's own
signature `extern "C"`, spelled out in full, makes the two spellings meet at compile time instead of
diverging silently. Seven symbols closed, and **the C++ block is now 83 of 83 in both
configurations**.

The second file was `bsd/dev/unix_startup.c`, and its cause is latent upstream: in 4570
`bsd/netinet/in_pcb.h` is not self-contained — `struct in_addr`, `struct route`, `struct sockaddr_in`
at `:114,176,185,295` and no `<netinet/in.h>` — because Apple's configurations all set `IPSEC=1`,
and `in_pcb.h:84`'s `#if IPSEC` then pulls `<netinet6/ipsec.h>` → `<net/if.h>` → `<net/if_var.h>` →
`<net/route.h>` → `<net/radix.h>` → `<net/if_llatbl.h>` → `<netinet/in.h>`. Upstream `main` fixed it
in the header (`in_pcb.h:74-75` now opens with `<netinet/in.h>` and `<sys/socketvar.h>`), which is
not an edit this project makes; `-include net/route.h` for that one file is. The three other files
that reach `in_pcb.h` compile because their own closures arrive at `net/route.h` anyway, which is
what says the header is the missing piece and not a symptom.

`STAGE90_BOOT`: undefined **114 → 102**, boot-path stubs **11 → 9**, C **419 → 420 of 426**, and
thirteen symbols closed against one opened — `bsd_exec_setup`, and its direction is the point:
`unix_startup.c` now compiles further, defines more, and one of the functions it contains calls into
`bsd/kern/bsd_init.c`, which still does not compile. `RELEASE` undefined **63 → 56**, boot path
**9 → 8**. **The boot path no longer reaches any stub whose blocker is a compile failure**:
`stub_blockers.py --min` now prints two categories and no third (5 "not in the manifest", 4 "no
source in the tree"), so the nearest remaining blocker is four edges in and is a missing component
rather than a broken file.

That `__nosan_bzero` row is itself misattributed, and the correction is the next thing to act on: the
symbol is not missing from the tarball and the header is not really "not in the manifest". Nothing
declares it in `osfmk/kern/zalloc.c`'s translation unit at all, and its one definition in the tree —
`static inline`, `san/memintrinsics.h:40` — is reached through `osfmk/libsa/string.h:97-99`'s
`#ifdef PRIVATE`, which in Apple's build *is* the kernel's `<string.h>`. Here `<string.h>` resolves to
`stages/stage90/shims_arm/string.h`, our own replacement, which declares the same functions and
dropped that block. The fix belongs in the shim, not in the manifest. Four of the six remaining
`STAGE90_BOOT` failures are one cause of the same local kind — the config(8)-generated device headers
exist only for `RELEASE` (`out/xnu_device/RELEASE/{bpfilter,loop,ptmx,pty}.h`), so `conf.c:111`'s and
`tty_ptmx.c:67`/`tty_pty.c:67`'s `#include <pty.h>` falls through to the **host's**
`/usr/include/pty.h` and dies in glibc, and `bsd_init.c:875` does the same with `loop.h`. The two
failures both configurations share are `subr_prof.c` (`STATIC`, line 160) and `kperfbsd.c`
(`ffs`/`fls`/`copyinstr`, the `MACH_KERNEL_PRIVATE`-per-component set); `bsd_init.c` has a second,
`STAGE90_BOOT`-only error that is the experiment-164 shape again
(`bsd/netinet/mptcp_var.h:465: no member named 't_mptcb' in 'struct tcpcb'`).

**XNU'S REAL `arm_init` RAN ON THE DEVICE** (2026-09-18,
[`experiment-159`](../experiments/experiment-159-the-real-arm-init-ran.md)). The entry image no
longer stubs `arm_init`: it links XNU's own `osfmk_arm_arm_init.o`, plus XNU's own `data.o`
(`osfmk/arm/data.s` — the real `intstack`, `fiqstack`, `CpuDataEntries`, `BootCpuData`, `RTClockData`,
48 KB of it) and `bcopy.o`/`bzero.o`, and generates a self-naming stub for everything they still need.
On the device the jump went through, and the image's own line says what happened:

```
Stage84 Mach-O/XNU loader preflight ok
stage90 xnu_entry: jumping to XNU's _start
real XNU entry: real arm_init reached a symbol this image does not provide
real XNU entry stub_hit=cpu_data_init
```

So the real function ran its prologue — including the 320-byte `const_boot_args = *args` copy, which
clang lowers to `bl __aeabi_memcpy4` and which now resolves to XNU's own `memcpy` in `bcopy.s` (the
`stub_hit` line is `cpu_data_init`, not `__aeabi_memcpy4`, which is what says the copy completed) —
and then called `cpu_data_init()` in `osfmk/arm/cpu.c`, which this image does not carry. **The next
thing to link is `cpu.c`'s object; the thing to decide is how large the image can be.**

Four defects came out of making it run, all of them the same shape — a stand-in that was not what it
stood for. Storage stubs were a flat 64 bytes (real sizes: `EntropyData` 68, `UNDReply_subsystem` 68,
and `BootCpuData` classified as a *function* because the map that decides that only looked in
`out/xnu_kernel_obj/`, not `out/xnu_asm_obj/`); `gPhysBase`/`gVirtBase`/`gPhysSize` were `const
uint32_t` while `arm_vm_init.c:80` declares `unsigned long` and *assigns* them at `:351-353`; and
`intstack_top` was a *variable* holding a stack address when `start.s:310`'s `LOAD_ADDR` and
`arm_init.c:226`'s `& intstack_top` both mean the symbol's own address *is* the stack top — so the
old image's `sp` was the address of a 4-byte variable and the stack descended into image data. The
generator now sizes every storage stub from the object that defines it and fails the build when the
size is not knowable.

And one defect that was not in the image at all: the payload's dry-run pmap mapped a **hardcoded 1 MB**
high-VA window while the probe it checks lives in the payload's own `.bss`. The entry image growing by
0x14000 moved that probe from 0x000EC0B4 to 0x001000B4 — past the window's end — and the preflight
failed with `HIGH_VA_DATA` on the *first* device run of this stage, before the jump. The window is now
derived from `__stage90_image_end`, which is the value `boot_args.c` already uses for
`topOfKernelData`. The payload will keep growing; the check had to stop assuming it would not.

**THE CLOSURE OF `arm_init` IS THE WHOLE KERNEL, AND THE MANIFEST OMITS 126 `optional` SOURCES**
(2026-09-18, [`experiment-158`](../experiments/experiment-158-the-closure-is-the-whole-kernel.md)).
`tools/entry_closure.py` was written to answer "what does the compile graph have to grow by next":
link, read the undefined set, add whatever object defines those symbols, repeat. It does not
converge. The chain is real — `arm_init` → `cpu_init` → scheduler/thread/task → IPC →
`bsd/kern/kern_proc.c` → vnode → devfs → tty line disciplines — and at 400 objects it was still 405
symbols short. **A static kernel is one connected component through its data, so no entry point has a
smaller closure than the kernel.** The staging question changes with it: a kernel is linked whole, and
what matters is the *count* of symbols a whole-kernel link leaves.

That count is **189**, for all 703 objects this project builds, every one of them referenced by an
object in the pool. They split three ways:

- **118 (62%)** name a symbol that appears in one of the **ten sources that do not compile**, and 58
  of those are `osfmk/vm/vm_object.c` alone (referenced by `vm_map.o`, `vm_pageout.o`,
  `memory_object.o`, `bsd_vm.o` — the biggest names in the kernel).
- **14** are compiler runtime and ABI: `__aeabi_*` (9), `__cxa_atexit`, `__cxa_pure_virtual`,
  `__dso_handle`, `__nosan_bzero`, `__nosan_strncpy`. Not XNU's to define; Apple links `libcc_kext`.
- The rest are sources **the manifest does not contain**, and that is the finding that matters more
  than the ten files. `bsd/net/bpf.c` and `bsd/net/ether_if_module.c` are in Apple's `bsd/conf/files`
  as `optional bpfilter` / `optional ether` and are absent from `out/xnu_arm_manifest.txt`
  (`grep -c` = 0 for both). Counting every `optional`-tagged `.c`/`.cpp` in the five `conf/files`
  this project reads: **301 are in the manifest and 126 are not** — all 18 `libkern/kxld/*` (the
  kext-linking machinery behind `OSKext.cpp`'s compile failure), ten `bsd/net/*.c` including
  `if_loop.c` and `if_vlan.c`, all of `bsd/security/audit/*`, all of `bsd/dev/dtrace/*`, all of
  `bsd/nfs/*`, `osfmk/kdp/kdp.c` (why `kdp_init` is undefined), `osfmk/kern/xpr.c`. So a
  configuration question — which options this kernel is — is silently deciding which files exist, and
  the refcounts of the 189 (`bpf_*` referenced by `iptap.o`/`pktap.o`, `ether_*` by `if_fake.o`,
  `lo_ifp` by whoever wants a loopback) are all downstream of it.

**AND ONE FORCE-INCLUDE WAS REWRITING DECLARATIONS** (2026-09-18,
[`experiment-157`](../experiments/experiment-157-the-force-include-was-rewriting-declarations.md)).
`-include kern/queue.h` was in both build scripts' force-include lists, for `mpqueue_head_t`. What it
also did is pull two **function-like macros** into every translation unit —
`osfmk/kern/queue.h:224-225` is `#define enqueue(queue,elt) enqueue_tail(queue, elt)` and the same for
`dequeue`. `iokit/IOKit/IODataQueue.h:131` declares `virtual Boolean enqueue(void *data, UInt32
dataSize);`, and a function-like macro fires on `name(`, so the declaration became `enqueue_tail(...)`
while the definition at `IODataQueue.cpp:157` kept its own name — the file `#undef`s the macro at
`:47`, four lines after the include that needed it. Two IOKit files failed with "out-of-line
definition of 'enqueue' does not match any declaration", a message that names neither the macro nor
the header.

Removing the line: C++ **78 → 80 of 83** in both configurations, C unchanged at 608/615 and 414/426,
objects 686 → 688, and **all 686 shared objects byte-identical**, with the undefined lists identical
(`RELEASE` 189, `STAGE90_BOOT` 330) and the boot path unmoved at 42 / 50. The `mpqueue_head_t`
justification was obsolete — `cpu_data_internal.h` reaches it another way. The layer script had the
same line for the same reason: **32 of 32, all 32 objects byte-identical, 445-symbol undefined list
identical**, so it is gone from both and the two scripts still describe one configuration.

**Third time a force-include has cost more than it bought** (a `stdatomic.h` that reached `ptrdiff_t`
and broke eight zlib files, and a `stdbool.h` shim that fixed one file and broke 78 — both
experiment-151). The shape is the same each time: a force-include is a global edit to every
translation unit, and the damage surfaces somewhere else entirely. Each entry in that list now carries
a **measurement** rather than a reason it might help — three of them had a reason and no measurement.

**The C++ block is at 80 of 83, three files left, no two alike**: `OSKext.cpp` on
`kxld_create_context`'s signature, `OSKextLib.cpp` on `kext_request`'s language linkage, and
`OSRuntime.cpp` on `operator new[]`'s `size_t` (the target triple). The seven C failures are unchanged
and both configurations fail the same three.

**AND THE MIG RUN WAS MISSING ONE WORD FROM APPLE'S DEFINE LIST** (2026-09-18,
[`experiment-156`](../experiments/experiment-156-the-mig-run-was-missing-kernel.md)).
`gen_mach_headers.sh` preprocesses each `.defs` before MIG, with a hand-written flag list — four
copies of it, in fact, three of which agreed with each other and none with Apple's `$(DEFINES)`
(`makedefs/MakeInc.def:78-80`), which is the first thing in `MIGFLAGS` (`:470`). **`-DKERNEL` was
not in any of them.**

It is load-bearing in the `.defs` language. `osfmk/mach/vm_map.defs:75-79` is

```
#if !KERNEL && !LIBSYSCALL_INTERFACE
#define PREFIX(NAME) CONCAT(_kernelrpc_, NAME)
#else
#define PREFIX(NAME) NAME
#endif
```

and `:116,132,153` wrap `vm_allocate`, `vm_deallocate` and `vm_protect` in the same `#if … skip;
#else routine PREFIX(…); #endif`. So the kernel's own `<mach/vm_map.h>` was generated with the
**userspace** branch throughout: the three routines were skipped, and every `PREFIX(...)` routine
carried the `_kernelrpc_` name — the one libsystem calls it by, not the one the kernel defines it
under. `grep -c deallocate out/mach_headers/mach/vm_map.h` was **0**; it is 7.

| | before | after |
| --- | --- | --- |
| generated MIG files that differ | — | **32 of 260** |
| `RELEASE` / `STAGE90_BOOT`, C++ | 75 of 83 | **78 of 83** (both) |
| undefined symbols, `RELEASE` | 448 | **189** |
| undefined symbols, `STAGE90_BOOT` | 586 | **330** |
| boot-path stubs, `RELEASE` / `STAGE90_BOOT` | 46 / 53 | **42 / 50** |

Three C++ files closed, and they were three different problems that had been counted as one:
`IOUserClient.cpp` and `IOMemoryDescriptor.cpp` on `vm_deallocate`/`mach_vm_deallocate` — **which have
no other declaration anywhere in the tree** — and `IOService.cpp` on `thread_policy_set`, whose
prototype at `osfmk/mach/thread_policy.h:54-66` is **inside a `/* … */` block**, which is why clang's
suggestion was the next identifier down. All three are defined by the kernel
(`osfmk/vm/vm_user.c:336`, `osfmk/kern/thread_policy.c:272`); only the declaration was missing.

**Most of the 259-symbol drop is not the define.** The three files now compile, so their 829 defined
symbols enter the image and **250 of them were undefined before**; 13 are the `PREFIX` change (12
`_kernelrpc_*` plus `_host_page_size`); 4 are new references the new objects bring. 448 − 250 − 13 +
4 = 189. And the C side is **unchanged at 608 of 615** — those 13 names were being stubbed silently
in a C build that reported no problem at all, because a stub is not a diagnostic.

The four hand-copied lists are now one array, `DEFS_DEFINES=(-DKERNEL=1 …)`. `-DAPPLE` is out (no
`.defs` references it outside license comments), `-D__MACHO__=1` and `-Dvolatile=__volatile` are
referenced by no `.defs`, and `-DMACH_KERNEL_PRIVATE` stays exactly where it was because
experiment-144's export/`kserver` split depends on the arrangement — re-checked, not assumed
(`exc_server.h` and `vm_map_server.h` still carry `ipc_kobject` in `kserver/` only). Eleventh
instance of the one-value-two-definitions class. Nothing in XNU's source changed, both configurations
build to exit 0, and the payload is byte-identical.

**AND THE ASSEMBLY TRANSLATOR HAD BEEN WRITING INTO APPLE'S TREE** (2026-09-18,
[`experiment-155`](../experiments/experiment-155-the-translator-wrote-into-the-tree.md)). Found
because `stages/stage90/build.sh` stopped at its own gate:

```
stage90_xnu_compile_graph_no_external_mutation=0x00000000
stage90_xnu_compile_graph_failure_mask=0x80000000
```

`external/xnu-4570.1.46` is a git repo of its own, and four files in it were modified — the four
`experiment-150` is about. The mechanism: `assemble_arm_layer.sh` mirrored the tree into its output
directory with `ln -sfn "$XNU/$d" "$OUT/translated/$d"`, so `$OUT/translated/osfmk` **was**
`$XNU/osfmk`, and `open($OUT/translated/osfmk/arm/data.s, "w")` was `open($XNU/osfmk/arm/data.s,
"w")`. **Every translated copy was a write into Apple's source**, on every run, while the tool's own
comment said "the tree is never written to".

Reverting and re-running is what made it undeniable: the script reverted the tree and un-reverted it
in one command, with the same sha256. The fix builds the mirror the other way round — **directories
real, files symlinks into the tree**, and only the directories above a file that actually needs
translating stop being symlinks — and translates to a scratch file first, so nothing this script
opens for writing passes through a symlink.

**And the in-tree edits were pure leakage**: from a pristine tree, **17 of 17** assemble and all
**17 objects are byte-identical** to the ones built from the edited tree, the tree is clean after the
run, and `build.sh` is **exit 0** with all three `*_no_external_mutation` markers `1`. So
`experiment-150`'s 17-of-17 was measured against a mutated tree and now stands re-measured against
the original. **The rule "XNU's source is never modified" now holds for the assembly path as
written, not only as intended.**

**AND THE SHIMS WERE AUDITED, AND CAME BACK CLEAN** (2026-09-17,
[`experiment-153`](../experiments/experiment-153-shim-audit.md)). Sixteen of the project's 26 shims
have the same path as a real header in the tree — this project's most-repeated defect class, four
times over five turns — so they were checked against the tree, and **every one is inert in the kernel
build by construction**: `-I$SHIMS` and `-I$SHIMS_ARM` are **last** in the include order
(`build_xnu_arm_kernel.sh:269-271`), so a real header always wins. Verified directly
(`#include <kern/kern_types.h>` reaches `osfmk/kern/kern_types.h`, not the shim) and by measurement:
removing the directory costs **125 files** (608 → 490).

So it is a **load-bearing fallback**, and the shadowing is the price of being one — the opposite
arrangement from the four earlier cases, where a shim was **first** on the path and hid a real header.
**The rule that separates them**: a shim may shadow a real header if and only if it is placed where it
can only be reached when the real one is not. Four failures came from breaking that rule; this
directory obeys it. The ten `shims_arm` headers that shadow nothing are the ones the build needs, and
they are build-generated, this project's own configuration, or genuinely unpublished.

Also recorded so it is not mistaken for a regression: `xnu_object_subset_compile.sh` reports
`failure_mask=0x80000010` and did so at `26f14c5` too — checked, not chased.

**This is the first audit in this project that came back clean**, which is what makes the other four
credible.

**This is the right denominator, and it replaces the earlier one.** "32 of 32 compile" was every
`.c` in `osfmk/arm`; a real kernel builds what the file lists say. So the honest question is how many
of **694** compile, and that measurement is now one command away.

See [`tools/xnu_config/README.md`](../../tools/xnu_config/README.md). This is the third time in this
project that "not available" meant "looked in one directory": MIG was published elsewhere, the
generated mach headers were a build step away, and this was at `config/` all along. The remedy each
time is to search the tree for the *shape* of the thing rather than trusting the path an error
message names.

**The gap is now a list of twenty symbols, not an adjective (2026-09-17,
[`experiment-103`](../experiments/experiment-103-xnu-entry-point-assembles.md)).**
`stages/stage90/xnu_arm_assemble.sh` assembles `osfmk/arm/start.s` — XNU's real `_start`,
unmodified — with clang 14. The undefined list is: `_arm_init` (+ its two secondary-CPU siblings),
eight `fleh_*` handlers and `ExceptionVectorsTable` (all in `locore.s`, same treatment), and nine
XNU data symbols that `globals_asm.h` already enumerates as `LOAD_ADDR_GEN_DEF`s. `assym.s`, the
largest single missing piece, is supplied for `start.s`; `locore.s` needs 28 more constants, all
`offsetof()`s into `cpu_data_t`/`arm_saved_state`, so the whole entry path reduces to one question:
can `osfmk/arm/cpu_data_internal.h` be made to compile? Two flags were the difference between
"header problem" and "one symbol list": **`-DASSEMBLER=1`** (without it `asm.h`'s entire `LOAD_ADDR`
machinery is preprocessed away) and **`-Dfmrx=vmrs -Dfmxr=vmsr`** (Apple's assembler dialect).

**And it corrects Phase 2.** `start.s` *builds its own* bootstrap page tables at `topOfKernelData`
— it invalidates the TTEs, sets a V=P section, maps the kernel with 1 MB sections from
`physBase`/`virtBase`/`memSize`, spills to an L2 table when `memSize` is unaligned, then sets
TTBR0/TTBR1 and enables the MMU. So the Phase 2 bullet *"populate a `topOfKernelData` region
containing the bootstrap page tables XNU will adopt"* has the direction wrong: XNU adopts nothing
and writes its own. What it needs is a writable, 16 KB-aligned region with room — which the
conforming `boot_args` already provides and `experiment-99` verified on the device.

**One correction, 2026-09-17: the *processor* configuration is in the source.** Chasing this
further, `osfmk/arm/proc_reg.h:73` is `#if defined (ARMA7)` → `__ARM_ARCH__ 7`, `__ARM_VMSA__ 7`.
That is the 32-bit ARMv7 machine configuration, it is the only 32-bit branch in the chain, and
without a processor macro the chain reaches `#else / #error processor not supported` at `:161` -
which is what every ARM file was dying on. `-DARMA7` removes it. clang, now installed, is also
what the atomic layer requires (`EXTERNAL_HEADERS/stdatomic.h:24` is `#error unsupported compiler`
unless `__clang__`). Neither changes the file count: **3 of 32, before and after, with either
compiler.** The next distinct error is `decl_simple_lock_data`, defined in
`osfmk/arm/simple_lock.h`, which **nothing in the tree includes** - a header the build arranges to
be present rather than a value to choose.

So the honest summary is: **the parser, the platform list and the processor selection are public;
the per-arch target definitions and the force-included header set are not.** Reconstructing them is a real possibility — the
convention is visible — but it is building the build system, not configuring it, and it would
sit on a toolchain (`xcrun`) this host does not have either.

That is worth writing down before Phase 4 is attempted, because the failure mode otherwise is
spending days fighting `$(error Unsupported CURRENT_ARCH_CONFIG ARM)` and concluding the
source is unusable, when the source is fine and the *configuration* is what is missing.

### And the configuration may not be the real obstruction — the headers are

The above concludes "build the build system". Trying to compile one ARM source by hand says
something more useful, and it is a **better** answer than the one above.

`osfmk/arm/arm_init.c`, compiled directly with the project's toolchain and the project's own
shim-header directory (the same `-Ishims` approach `xnu_object_subset_compile.sh` already
uses for its five objects):

1. Fails at `#include <debug.h>` — a `PRIVATE_DATAFILE` the build exports. The project's
   `shims/kern/debug.h` already covers it.
2. With the shim subdirectories on the path, gets further and fails at
   `#include <mach_ldebug.h>`.
3. **`mach_ldebug.h` does not exist anywhere in the tarball.** Nothing matches `*ldebug*`.
   It is included by `osfmk/kern/thread.h:104`, `locks.c`, `simple_lock.h`, `genassym.c`,
   `locks_arm.c` and `arm_init.c` — and 20 of the 32 `.c` files in `osfmk/arm` include
   `thread.h`. So it is reached by most of the ARM tree, and it was never published.

That reframes the work, and more favourably than the previous section suggests. The project
does not have to reconstruct Apple's build system — it already has a working mechanism for
compiling XNU sources outside it, in `stages/stage90/shims/` (16 headers, enough for the five
pexpert objects). Extending that to the ARM tree is **iterative and mechanical**: point the
compiler at the next source, be told the next missing header, write it, repeat. The compiler
walks the include graph for you.

What is unknown is the *count* — how many headers like `mach_ldebug.h` are missing, and how
many of them are declarations-only shims versus things that need real behaviour. `mach_ldebug.h`
looks like the former (it exists to hold `pal_mlock`/`pal_munlock`-style debug hooks). But the
honest statement is that no one has counted, and the count is the thing that decides whether
Phase 4 is a weeks-scale or months-scale piece of work.

**Correction — the estimate below was wrong, and doing the work is what showed it.** An
initial pass concluded the ARM layer's gap was 8 missing headers: bounded, enumerable, the
same order of magnitude as the 16 shims already written. Writing them disproves that. With
four stubs in `stages/stage90/shims_arm/` plus `-DKERNEL=1` plus the `iokit` path on the
include line, the sweep goes from **2 of 32 to 3 of 32** — and the remaining errors stop being
*missing files* and become **undefined build-configuration symbols** (`AST_NONE`,
`INTSTACK_SIZE`, `gPhysBase`, `decl_simple_lock_data`). The clearest is
`EXTERNAL_HEADERS/stdatomic.h:24`:

```c
#ifndef __clang__
#error unsupported compiler
#endif
```

XNU's atomic layer is clang-only; this project's ARM toolchain is `arm-none-eabi-gcc`. So the
accurate conclusion — reaching the same place the build-system investigation did, from the
other direction — is that **the source tree is complete and the build configuration is
absent**. The first level of the header gap was real and is now written and verified; it is
not a meaningful fraction of the way to compiling.

The measurement below stands as the record of the first level. Its "bounded and enumerable"
claim was over-optimistic and should not be planned from.

**The first-level measurement, for the record:**


Sweeping every `osfmk/arm/*.c` with `-fsyntax-only`, using the project's toolchain and its own
existing `shims/` include set:

| | |
| --- | --- |
| `.c` files in `osfmk/arm` | 32 |
| compile clean with the existing 16 shims | **2** |
| distinct missing headers blocking the other 30 | **8** |

The eight, and what each actually needs:

| Header | Blocking | Kind |
| --- | --- | --- |
| `sys/_symbol_aliasing.h` | 17 files | build-generated; **empty stub is correct** |
| `sys/_posix_availability.h` | 16 files | build-generated; minimal stub |
| `sys/_pthread/_pthread_types.h` | 16 files | **needs real content** — the `__darwin_pthread_*` types it names are also absent from `bsd/sys/_types.h`, and the whole `bsd/sys/_pthread/` directory is missing from the tarball |
| `mach_assert.h` | 5 files | absent entirely |
| `mach_ldebug.h` | 3 files | absent entirely |
| `mach_kdp.h` | 3 files | absent entirely |
| `mach/vm_page_size.h` | 2 files | exists, but at `libsyscall/mach/mach/vm_page_size.h` — a path shim |
| `mach_debug.h` | 1 file | exists, but at `osfmk/mach_debug/mach_debug.h` — a path shim |

**Two of the eight were verified by iterating, not by inspection.** Adding an empty
`_symbol_aliasing.h` moved the sweep's dominant blocker to `_posix_availability.h` (16 files);
adding that moved it to the `_pthread_types` chain. So the two build-generated headers are
genuinely empty-stub-able, and the third is genuinely not — its names resolve to types that
are also missing, so it needs the real definitions rather than a placeholder.

**What the number means.** Eight headers, of which two are empty stubs, two are path shims,
one needs type definitions, and three need writing (`mach_assert`, `mach_ldebug`, `mach_kdp`
— chiefly assertion and debug macros). That is a bounded, enumerable list, not an open-ended
one, and it is the same order of magnitude as the 16 shims already written for five pexpert
objects.

The extrapolation is the honest caveat: this is the **ARM layer only**. A full `mach_kernel`
would need the whole `bsd/` and `libkern/` dependency closure, which nobody has swept. So
Phase 4's cost is not "8 headers" — it is "8 headers for the first layer, times an unknown
number of layers." But the first layer is now measured rather than guessed, and the method
(compile, read the next missing header, add, repeat) is mechanical.

### Phase 3 — the shim runs on hardware

**`STAGE90_XNU_MSM8974_SHIM=1` now passes on the device (2026-09-17,
[`experiment-101`](../experiments/experiment-101-phase3-shim-first-hardware-run.md)):**
`checks=8`, `failures=0`. It registers its `tbd_ops` through a mirror of `ml_init_timebase`'s
guard and verifies the installation by reading the ops back, checks the EOI pair
(`GICC_EOIR` at `0xf9002010`, interrupt `0x13` — the CNTP PPI), confirms `CNTFRQ` reads 19200000,
roundtrips the decrementer through the registered callback, and leaves the timer disarmed so the
payload's own timer code keeps owning arming.

The first run failed all eight on a single missing assignment: `r->int_address`/`r->int_value`
were declared, logged and checked but never written, while the mechanism underneath was correct
all along. Sixth instance of this project's recurring two-places-one-write defect.

**It now has a caller, and drives hardware (2026-09-17, [`experiment-104`](../experiments/experiment-104-shim-drives-the-timer.md)).**
`prepare`/`commit`/`disarm` run for real: a 10 ms interval goes through the registered
`tbd_set_decrementer`, the interrupt comes back through the payload's GIC handler
(`arm_irq_before=1 → arm_irq_after=2`), the measured elapsed time is 9961 µs against the 10000 µs
asked for — 0.4% — and the timer is disarmed on every path. That run also found a real bug: the
interval guard in `prepare` bounded `interval_us` by `0xffffffff / CNTFRQ` (223 µs) instead of
bounding the intermediate `interval_us * 96`, so it rejected every realistic interval and the demo
produced no output at all. Found by running it; the code reads like a bound either way.

What is still not true: XNU does not call it. The shim registers its own ops through a mirror of
`ml_init_timebase`'s guard, because XNU is not running. Substituting it for
`pe_arm_init_interrupts` is the step that needs XNU.

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
- **XNU requires a platform FIQ handler on this build — and that may be the wrong path for
  this SoC.** The FIQ vector slot (`locore.s:147`) branches to `r9` — the shim's
  `tbd_fiq_handler` — because `__ARM_TIME__` is used in the ARM tree and defined in none of
  them. XNU's own `fleh_fiq_generic` shows the handler's contract, including that the EOI is
  literally a write of `int_value` to `int_address`.
- **But the FIQ coupling is structural, and MSM8974 probably cannot do it.** The
  non-`__ARM_TIME__` path keeps the decrementer in a register banked to FIQ mode
  (`machine_routines_asm.s:1029` switches mode to touch `r8`), which is *why* it needs a FIQ
  handler. And the vendor's own header says of FIQ on this family: *"You have to be running in
  secure mode to use FIQ"* (`msm_watchdog.h:28`), with the cancro device tree not enabling
  kernel FIQ at all.
- **XNU already contains the alternative.** Defining `__ARM_TIME__` moves the timer to a
  complete IRQ path in the tree (`Lexc_decirq_vector` → `fleh_decirq`, both real), and its
  decrementer callbacks use the architectural timer instead of a FIQ-banked register. That is
  the shape of a path built for platforms with a real hardware timer — the situation here —
  and it is the candidate resolution for Phase 3's largest risk. The guarded sites are short
  and tractable (vector, vector-table slot, `fiq_context_init`, two accessors,
  `ml_get_timebase`, `user_timebase_active`), with nothing obviously half-finished. But
  confirming it compiles is **not** the cheap host-side check this document first called it:
  `__ARM_TIME__` lives in files this project does not compile (the public-XNU graph is five
  `pexpert` objects), so testing it means building XNU's ARM kernel — gated to Phase 4 — and
  the build config that selects it (`SETUP/config` `.def` files) is not in the OSS tarball
  anyway. One thing *can* be read off, and sharpens the picture: `__ARM_TIME_TIMEBASE_ONLY__`
  is defined inside `#if defined(ARMA7)`, and `ARMA7` comes from the same closed Apple
  board-class set that §1 says Phase 3 must replace — so the timebase question and the shim
  are coupled through one macro.
- **And XNU's stated reason for using CNTV instead of CNTP is measurably false here.** Its
  own comment (`machine_routines_asm.s:924`) says *"for our current platforms, the interrupt
  generated by the physical timer is not hooked up to anything, and is therefore dropped on
  the floor"*. On MSM8974 the payload armed CNTP and its interrupt arrived on intr 19 (§3.2) —
  the physical timer IS wired. So the CNTV choice is a workaround for an Apple wiring
  decision this device does not share, which opens a third option neither source discusses:
  take the IRQ vector from `__ARM_TIME__` but point the shim's decrementer callbacks at CNTP,
  reusing the measured intr 19 and the payload's validated timer code. Reading-derived
  hypothesis, not a tested design — see spec §6.2.1.
- **The timebase tension resolved, and favourably.** `fleh_fiq_generic` maintains a software
  timebase (TBL incremented per tick) while `__ARM_TIME_TIMEBASE_ONLY__` makes
  `ml_get_timebase` read the real `CNTPCT`. Reading `rtclock.c` settles it: everything that
  wants a timestamp goes through `ml_get_timebase`, so **the software TBL is dead code on this
  build** and the generic handler is usable as-is. Consequence: the timer need *not* tick
  periodically for the timebase to be correct, which removes a constraint that would otherwise
  have shaped the shim's timer code. The decrementer callbacks still matter — they are how the
  handler re-arms — but `fleh_fiq_generic` itself does not need reimplementing.

**Phase 3 has started.** [`stages/stage90/xnu_msm8974_shim.c`](../../stages/stage90/xnu_msm8974_shim.c)
implements the interface the spec defines — the `tbd_ops` mirror (asserted in-payload and
checked against XNU's header by `tools/check_xnu_struct_abi.py`), the `&BootCpuData`
registration guard reproduced *and verified by reading the registration back*, CNTP-based
decrementer callbacks rather than CNTV (§6.2.1), the EOI pairing, and the CNTFRQ check.
Behind `STAGE90_XNU_MSM8974_SHIM`, default off.

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
