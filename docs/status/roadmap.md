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

**What that does not mean:** `arm_init` is a Stage-owned stub, so everything after it in a real
kernel — `arm_vm_init`, `machine_startup`, the scheduler — does not exist here. **XNU's entry point
runs; XNU does not run.** The next thing needed is the symbol *after* `arm_init`, which is a much
larger body of code than the entry path was.

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
