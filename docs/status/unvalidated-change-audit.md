# Risk Audit of the Unvalidated Stage90 Changes

Six changes were written on 2026-09-16 without any of them reaching hardware, because the
device was left hung by the `PREFLIGHT_WATCHDOG_ONLY` run earlier that day. When several
changes are tested in one boot, a failure is hard to attribute — so this document exists to
bound each change's risk *by reading it*, and to say which log line confirms or clears it.
It is a review, not a substitute for the run.

The items are ordered by when they execute in the default build, so the document reads as the
boot does.

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

## 6. What this audit cannot bound

- **The watchdog's register semantics.** The readback and liveness checks confirm the
  registers are *where the device tree says* and that a counter is *running in the expected
  window*, but not that a bite resets the SoC. That is what the
  `STAGE90_HW_WATCHDOG_SELFTEST=1` run is for, and it should come first for exactly this
  reason.
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
