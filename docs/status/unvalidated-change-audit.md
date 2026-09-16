# Risk Audit of the Unvalidated Stage90 Changes

Six changes were written on 2026-09-16 without any of them reaching hardware, because the
device was left hung by the `PREFLIGHT_WATCHDOG_ONLY` run earlier that day. When several
changes are tested in one boot, a failure is hard to attribute — so this document exists to
bound each change's risk *by reading it*, and to say which log line confirms or clears it.
It is a review, not a substitute for the run.

The items are ordered by when they execute in the default build, so the document reads as the
boot does.

## 0a. Which changes the default build actually exercises — 5 live, 3 inert

"Six changes have never reached hardware" has been said repeatedly in this project's notes,
and it overstates what one run has to prove. Counting against the actual compiled switches
(`out/stage90/stage90-build-config.txt`, which `build.sh` writes from the preprocessor):

Each row below is verified against the built image, not against the header — the point of
the table is what the *binary* does, and a switch that is off in the header but on in the
build would make the table a lie. The right-hand column of evidence is what was checked:

| Change | Switch | In a default run |
| --- | --- | --- |
| Handoff harness repair (mode enum, genuine ladder, entry guard) | `STAGE90_HANDOFF_MODE` | **live** |
| Software dead-man | `STAGE90_DEADMAN_ENABLE=1u` | **live** |
| MSM8974 hardware watchdog | `STAGE90_HW_WATCHDOG=ARMED` | **live** |
| F-AM1 (RAM-console alias move) | none — always compiled in | **live** |
| Device-tree fixes (`/arm-io`, `state`, `device_type`) | none — always compiled in | **live** |
| Phase 1a (Normal/Non-cacheable attributes) | `STAGE90_PMAP_ATTR_MODE=SO_ONLY` | **inert** |
| Conforming `boot_args` | `STAGE90_XNU_BOOT_ARGS=0u` | **inert** |
| Fault injection | `STAGE90_HANDOFF_FAULT_INJECT_VA=0u` | **inert** |

So a default `HARD_SKIP` run exercises **five** deltas, not eight — and three of the pending
changes provably cannot affect its outcome. Each of those three was checked in the built
image rather than trusted:

| Inert change | How it was confirmed off |
| --- | --- |
| Phase 1a (`SO_ONLY`) | `tools/count_descriptors.py --diff` against a `NORMAL_NC` build: the NC values `0x1c02`/`0x0452` go 0 → 30 and 0 → 1, so the switch genuinely rewrites 30–31 sites, and `SO_ONLY` contains none of them |
| Conforming `boot_args` | `objdump --disassemble=kernel_entry` contains 0 calls to `xnu_boot_args_prepare` with the switch off, 1 with it on |
| Fault injection | the image contains 0 occurrences of the `FAULT INJECTION` log strings by default, 2 when enabled |

That is the difference between "the header says off" and "this build does nothing". That is worth knowing before the run,
because it means a failure has five candidate causes rather than eight, and a *success*
says nothing at all about the three inert ones — they still need their own runs later.

The practical consequence: if the goal is to shrink what one run must prove, the lever is
not more analysis, it is turning an inert change into a live one only when its own run is
scheduled. `STAGE90_PMAP_ATTR_MODE=SO_ONLY` as the default is doing exactly that job.

## 0. What the default build actually is

`STAGE90_HANDOFF_MODE = HARD_SKIP`, `STAGE90_ENTRY_LADDER_LEVEL = FULL`,
`STAGE90_PMAP_ATTR_MODE = SO_ONLY`, `STAGE90_HW_WATCHDOG = ARMED`, and the three
opt-in switches (`EXCLUSIVE_PROBE`, `XNU_BOOT_ARGS`, both SELFTESTs) **off**. Verified from
the build's own config dump, not from the header.

`HARD_SKIP` still runs the entire `arm_init` ladder — early pmap, `PE_init_platform`,
post-PE bootstrap, `arm_vm_init` with the candidate-L1 install/verify/restore, the high-VA
handler windows, the Mach-O loader — and stops short of only the three things that can hang:
the candidate-L1 *switch*, the watchdog loop, and the jump. So a green run validates most of
the stack; it just does not test the handoff itself.

## 1. Hardware watchdog arming — the one change that runs before everything

**What it does:** writes four registers at `0xf9017000` at the top of `stage90_main`, before
the DT build, before `boot_args`, before the MMU is enabled by the payload.

**The risk:** it is MMIO, and it now runs earlier than any other MMIO the payload performs.
If that address were unmapped or the wrong base, the access would fault.

**Why it is bounded:** the address is in the same 1 MB section as the GIC —
`0xf9000000`, `0xf9017000` and `0xf9020000` all fall in section `0xf9000000` — and
`gic_readonly_snapshot()` reads the GIC well before the MMU selftest, at `kernel_entry` line
46 versus `mmu_identity_selftest()` at line 100. That has worked in every successful run for
ninety stages, so MMIO in this section before the payload's own MMU setup is established
practice, not a new frontier. The specific address is from the cancro device tree
(`msm8974.dtsi`, `qcom,wdt@f9017000`), and the DT lives in the same tree that correctly names
the GIC, timer and PS_HOLD addresses the payload already uses.

**Residual risk:** the *register semantics* are unverified on this device — see "What this
audit cannot bound" below.

**The timeout is safe, and that was checked rather than asserted.** A watchdog that fired
during a *healthy* run would reboot every run and look exactly like the hang this change
exists to prevent — so the margin matters as much as the mechanism. Bounding the payload
from its own code, with a pessimistic 500 ns per uncached Strongly-Ordered byte and every
buffer byte touched twice:

| Contribution | Bound |
| --- | --- |
| `delay_us` calls (4 sites: 1+5+2+1 ms) | 9 ms |
| Bounded selftest waits, worst case (SGI 20 ms + timer 50 ms) | 70 ms |
| Logging — 275 KB at byte granularity (the largest recorded Stage log; `runtime.c`'s `memcpy`/`memset` are byte loops, so byte granularity is exact, not conservative) | 138 ms |
| Buffer zeroing — 16 KB + 128 KB L1/L2 tables, 32 KB device tree, 64 KB Mach-O arena | 262 ms |
| **Pessimistic total** | **479 ms** |

Against a 30 s bark, that is a **63x margin**. The measured end-to-end times in the early
experiments ("~25 seconds to Android") are consistent with this and are *not* payload time:
they include Android booting afterwards.

**Confirms it:** `hw_watchdog_enabled=0x00000001` and `hw_watchdog_counter_running=0x00000001`
in `last_kmsg`. If those are 0, the net is not armed and the run has no hardware recovery.

## 2. Device tree: the `/arm-io` node and `state` properties

**What changed:** one new root child (`/arm-io`), one new property on each of the four cpu
nodes (`state = "running"`), and the root's declared child count 19 → 20.

**The risk that matters — a wrong `nChildren`:** it corrupts the walk silently, because the
walker reads the count it is given. This is the hazard of editing this builder at all.

**Why it is bounded:** `skip_node` must land exactly at the buffer end, and
`find_child_by_name` walks exactly `nChildren` children, so `apple_dt_selftest_and_log`
catches a mismatch *and runs on hardware* — it is not a host-only check. The count was also
verified independently of the checker: 21 level-1 `apple_dt_node_begin` calls minus the root
itself = 20 children, matching the declared value.

**The other risk — a wrong `nProperties`:** also counted by
`tools/xnu_dt_requirements.py`, which is wired into `build.sh` and negative-tested
(perturbing the cpu node's count 7 → 6, or `/arm-io`'s 4 → 3, each makes it fail).

**Confirms it:** `apple_dt selftest ok`, `apple_dt_root_children=0x00000014`, and
`find /arm-io ... ok` in `last_kmsg`.

## 3. Software dead-man

**What it does:** arms the GIC + timer sampling watchdog at the end of `kernel_entry`'s GIC
validation, 60 s, and re-arms after the handoff.

**The risk:** it enables the GIC distributor and CPU interface if they are not already on, and
leaves IRQ delivery unmasked from that point on. A bug here could produce unexpected
interrupts during the ladder.

**Why it is bounded:** `gic_sgi_selftest()` asserts `GICD_CTLR & 1` and `GICC_CTLR & 1` and
returns 0 otherwise — and it *passes on hardware* — so aboot already leaves both enabled and
the repair path is normally not taken. The priority mask is deliberately not touched, so the
set of delivered IRQs is not widened. The 60 s budget is far longer than the ~1 s payload, and
every normal exit already ends in `platform_reboot()`.

**Confirms it:** `deadman: armed (dump + PS_HOLD reboot if the payload stops making progress)`
early in the log, and — the important one — its *absence* of a spurious firing: a
`pc_sample_watchdog_fired=1` on a run that otherwise completed normally would mean the budget
is too tight.

## 4. `SO_ONLY` — i.e. nothing changed

Phase 1a is `STAGE90_PMAP_ATTR_MODE = SO_ONLY` by default, and that is verified to emit
**zero** Normal descriptors: the built image contains 0 occurrences of the Normal-NC section
descriptor `0x1c02` and 0 of the small-page `0x452`, against 55 and 16 of the
Strongly-ordered forms. The only difference from the pre-Phase-1a binary is the added
classifier in `ttbr_section_desc_for_pa()`, which returns the same descriptor for DRAM.

**So Phase 1a is not a risk in this run at all** — it is inert unless the switch is set.

## 5. F-AM1 — the RAM-console alias move

**What changed:** `full_pmap`'s `STAGE90_RAM_CONSOLE_ALIAS_BASE` `0xc0100000` → `0xc0300000`,
so it stops shadowing the second megabyte of the image alias.

**The risk:** the alias verification reads `RAM_CONSOLE_SIG` through the new VA and compares
it against the identity mapping. A wrong address fails that check.

**Why it is bounded:** the failure is *visible and specific* — a `full_pmap` failure with
`FAIL_RAM_CONSOLE` set — rather than a silent mis-translation. The change is confirmed
present in the built image (the constant loaded at the verification site is `0xc0300000`), and
the identity table is untouched, so nothing that works today depends on the old address.

**Confirms it:** `stage90_xnu_arm_vm_init_full_pmap_status=0x90000001` with no
`FAIL_RAM_CONSOLE` bit, and `..._ram_console_verified=0x00000001`.

## 5a. The recovery paths have 9x stack headroom — measured, not estimated

Every path that must work for a hang to be *recoverable* runs on a 1 KB banked stack
(`start.S`: `stage90_irq_stack`, `stage90_abt_stack` each `.space 1024`). An overrun there
would corrupt the stack rather than fail cleanly, and the symptom would be a corrupt-but-
plausible hang — the exact failure class this audit exists to shrink. So it was worth
measuring rather than assuming.

`arm-none-eabi-gcc -fstack-usage` on the actual sources, at the build's own `-O2`:

| Path | Stack | Of 1 KB |
| --- | --- | --- |
| IRQ → sample dump (logging) | 76 B | 7.4% |
| IRQ → reboot → PS_HOLD + bite | 96 B | 9.4% |
| **IRQ → reboot → bite → log** | **112 B** | **10.9%** |
| prefetch abort → log + reboot → bite | 88 B | 8.6% |
| data abort → handler → log | 64 B | 6.2% |
| undefined instruction → handler | 16 B | 1.6% |

**Worst case 112 B, a 9x margin.** The frames are small because `ram_console`'s logging is
byte-at-a-time with no formatting machinery — the thing that makes it slow is also what
makes it stack-cheap.

Two properties that make this bound hold rather than merely hold on average: the handlers
run with interrupts masked (ARM sets I on exception entry, and the abort path additionally
does `cpsid if`), so there is **no nesting**; and the payload is freestanding, so no library
call can consume stack unaccounted for here.


## 7. The window before any net exists, and why it is bounded

The audit covers what happens *after* the recovery nets are armed. The complementary
question matters just as much for the safety constraint: **can the payload spin forever
before either net exists?** If it could, no watchdog would help, because there would be
nothing to arm it from.

The window from `_start` to the watchdog arm is short and fully enumerable
(`start.S` then the first statements of `stage90_main`):

1. `cpsid if`, six banked-stack setups — straight-line.
2. `.bss` zeroing — a loop with a **static** bound.
3. `VBAR` install — straight-line.
4. `log_init()`, one `log_puts`, four `log_kv32` — loops bounded by string length and
   `size < max`.
5. `stage90_hw_watchdog_arm()` — the net appears.

**Every loop in that window has a compile-time or pointer-comparison bound. There is no
`for (;;)`, no unbounded wait and no hardware poll before the arm.** So the payload cannot
*spin* before it has a way out. The one unbounded loop in the payload is the recovery spin
itself, reached only after both nets are armed.

Two honest qualifications, since the point of this section is not to overclaim:

- **A fault in step 2 is not covered.** `VBAR` is installed in step 3, so the dominant part
  of the window — 155,867 word-stores covering 623 KB of `.bss` — runs with whatever vector
  table aboot left behind. A fault there would jump into stale vectors with no defined
  behaviour. It is bounded (~31–156 ms depending on uncached store cost) and it is
  straight-line code with no data dependency, so a fault is not *expected*; but the claim
  "nothing in this window can hang" holds for a spin, not for a fault.
- **The same is true of every stage that has ever run**, so it is not a regression — the
  earliest stages had the same ordering. It is recorded because it is the residual gap in
  the safety story, and because it is why the ordering (install `VBAR` as early as possible)
  should not be changed casually.


## 8. A defect found by reading the recovery handler: the sampling budget was shared

The recovery nets depend on `stage90_irq_c_handler`, and reading it against its own
documentation turned up a real bug — of exactly the kind this audit exists to catch, because
it would have produced *early, unexplained reboots* rather than a clean failure.

`stage90_irq_sample_count` served two purposes: the sample ring index, and the budget that
decides when the watchdog fires. It is incremented **for every interrupt**, before the
per-source dispatch:

```c
    stage90_irq_sample_ring[sample_idx] = interrupted_pc;
    stage90_irq_sample_count = stage90_irq_sample_count + 1u;   /* every interrupt */
    ...
    } else if (intid == GIC_TIMER_PPI0_ID || intid == GIC_TIMER_PPI1_ID) {
            if (stage90_irq_sample_count < stage90_irq_sample_max) { re-arm } else { FIRE }
```

So any interrupt from any source consumed sampling budget, and the next timer tick could
then fire the watchdog **early**:

| User | Budget | Exposed to |
| --- | --- | --- |
| Handoff sampling watchdog (`FULL` / `PREFLIGHT`) | 16 samples x 500 us = **8 ms** | **16 unrelated interrupts** would end sampling and reboot *before the target was sampled at all* - which would look like "the jump hung instantly" |
| Software dead-man (default) | 600 x 100 ms = 60 s | 600 unrelated interrupts in the window |

Recording the ring for every interrupt is *useful* - it shows the PC at any interrupt and is
worth keeping. The bug is only that the **budget** was the same variable. Fixed by splitting
them: `stage90_irq_sample_budget_used` increments only in the timer branch while in sample
mode, and is what the fire/re-arm decision uses. It is now logged and reported as
`pc_sample_budget_used` / `pc_sample_count`, so a run shows both numbers and the difference
between them is visible rather than hidden.

Worth noting how this was found: not by a test, and not by the device - by reading the
handler against the comment above it, which says "this is how we observe where XNU is
executing when the timer/watchdog fires". A non-timer interrupt is not that.


## 9. HARD_SKIP's status computation — checked, because it is subtle

The default mode is the one the first run will use, and its result is produced by a
different code path from the others. Reading between two changes that touch this — the
mode-dependent required mask, and the ladder split — the interaction is worth writing down,
since it is the kind of thing a later edit breaks silently.

The handoff's status test is:

```c
if (r->satisfied_mask == r->required_mask && r->failure_mask == 0u) { OK } else { FAIL }
```

and `required_mask` is now mode-dependent, because the candidate-L1 bits are unattainable in
`PREFLIGHT_WATCHDOG_ONLY`:

| Mode | Required mask |
| --- | --- |
| `FULL` | `0x7f` — `SAT_COMMON` (loader, entry, boot_args, ready) plus `SAT_L1_HANDOFF` (full pmap, candidate L1, stage target) |
| everything else | `0x47` — `SAT_COMMON` only |

`HARD_SKIP` takes this path:

```c
        r->satisfied_mask = r->required_mask;
        r->status = STAGE90_STATUS_OK;
        r->checksum = stage90_xnu_handoff_checksum(r);
        stage90_xnu_handoff_log(r);
        return 0;
```

**So `HARD_SKIP` sets `satisfied = required` by assignment, in whichever mode-dependent form
that mask takes, and returns OK directly — it never reaches the shared test.** Two
consequences worth being explicit about, because both are easy to break later:

1. Its result cannot fail on a mask mismatch *whatever* `required_mask` is. An edit that
   changed `SAT_COMMON`'s contents would change what the other modes require, and would not
   make `HARD_SKIP` fail — it would just make `HARD_SKIP` claim satisfaction of bits it never
   evaluated. That is a real hazard of the assignment form, and the reason the L1 bits are
   kept out of the non-`FULL` mask rather than merely left unset.
2. `HARD_SKIP` still reports through `stage90_xnu_handoff_log`, so `last_kmsg` will show
   `handoff_mode`, `required_mask` and `satisfied_mask` equal and `status=0x90000001`. A run
   that shows those unequal in `HARD_SKIP` means the assignment was lost — which would be a
   compiler/codegen problem, not a hardware one.

Verified against the definitions: `SAT_COMMON` = `0x47`, `SAT_L1_HANDOFF` = `0x38`,
`FULL` = `0x7f`, non-`FULL` = `0x47`.

## 6. What this audit cannot bound

- **The watchdog's register semantics.** The readback and liveness checks confirm the
  registers are *where the device tree says* and that a counter is *running in the expected
  window*, but not that a bite resets the SoC. That is what the
  `STAGE90_HW_WATCHDOG_SELFTEST=1` run is for, and it should come first for exactly this
  reason.
- **And the bite is mediated by the secure world, not pure hardware.** The vendor binding
  (`Documentation/devicetree/bindings/arm/msm/msm_watchdog.txt`) says the bite "is an
  interrupt in the secure mode, which leads to a reset of the SOC via the secure watchdog".
  So the accurate claim is "does not depend on the *payload's* GIC/timer/IRQ state", not
  "does not depend on any software". The secure world is running (aboot loaded it, and
  Android's own panic path relies on exactly this), so this is a dependency already
  satisfied rather than a risk — but it is a dependency, and the earlier wording overstated
  the independence.
- **Whether `physBase = 0` is right — now sourced, not argued.** The cancro kernel is built
  with `CONFIG_PHYS_OFFSET=0x00000000`, so RAM really does start at PA 0. `memSize` moved from
  a 2 MB placeholder to 93 MB, the span from PA 0 to the first block the device tree removes
  (`0x5d00000`); see the contract doc. The conforming `boot_args` is still off by default, so
  none of it affects this run.
- **The `reg` offset-vs-absolute question.** Recorded in
  [`xnu-handoff-contract.md`](xnu-handoff-contract.md), deliberately unresolved, and
  harmless until XNU's own platform code runs.

## Why `HARD_SKIP` first, and what each outcome means

The stack is large enough that the first run's job is *attribution*, not progress.

| Outcome | Reading |
| --- | --- |
| Green, both recovery nets armed, DT selftest ok | The ladder, the DT fixes and F-AM1 all stand. Next: `STAGE90_HW_WATCHDOG_SELFTEST=1`. |
| Green but `hw_watchdog_enabled=0` | Registers are not where the device tree says. The clock-fallback and watchdog questions both need revisiting before anything else. |
| Fails at the DT selftest | The `arm-io`/`state` edit, and only that — the selftest names the missing child. |
| Fails at `full_pmap` with `FAIL_RAM_CONSOLE` | F-AM1, and only that. |
| Hangs despite `HARD_SKIP` | The candidate-L1 *install/verify/restore* path, which `HARD_SKIP` does run — the one part of the ladder previously untested at this commit. |
