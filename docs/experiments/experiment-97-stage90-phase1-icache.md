# Experiment 97 — Stage90 Phase 1: the I-cache on, and the six places that asserted caches are off

Date: 2026-09-17
Commit under test: `7374605`, plus the changes described below
Build switches: `STAGE90_PMAP_ATTR_MODE = STAGE90_PMAP_ATTR_MODE_NORMAL_WB`,
`STAGE90_CACHE_MODE = STAGE90_CACHE_MODE_ICACHE`; `STAGE90_HANDOFF_MODE = HARD_SKIP`; both nets armed
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`

| # | Build | Result |
| --- | --- | --- |
| 1 | `NORMAL_WB` + `ICACHE`, first attempt | `kernel_entry returned failure` — the pmap bootstrap contract compared the workspace cache policy against the literal `0u` |
| 2 | after consolidating the cache-policy expectation into one definition | **`kernel_entry returned success`** |

Captures: `/tmp/kmsg-icache1.txt`, `/tmp/kmsg-icache2.txt`.

## What was being tested

The first step of Phase 1's cache work, and the one XNU's own entry code takes first: XNU's
`start.s` enables the I-cache within its first few instructions. Two switches, deliberately
separate:

- `STAGE90_PMAP_ATTR_MODE = NORMAL_WB` moves DRAM to the Normal Write-Back Write-Allocate
  descriptors `0x0001140e` (section) and `0x0000045e` (page). MMIO stays Strongly-ordered.
- `STAGE90_CACHE_MODE = ICACHE` sets `SCTLR.I` at MMU-enable time, after an `ICIALLU`.

A cacheable descriptor with the caches off means nothing, and a cache with an uncacheable
descriptor caches nothing, so the two are complementary rather than alternatives; a build that
sets `ICACHE` without `NORMAL_WB` is refused at compile time, and the gate refuses one without
`--allow-icache` and `--allow-attr-normal-wb`.

**Why the I-cache first, and why it is a bounded step.** Nothing in this payload writes code at
runtime, so no I-cache maintenance is needed anywhere. The one path that would is the Mach-O
loader materializing segments, and it currently refuses to copy at all (its destination is
unmapped at loader time — experiment-94), so the cache cannot go stale through it; when that path
becomes reachable it must issue an invalidate for the region it wrote, and the header says so.

The D-cache is deliberately **not** available yet. It needs maintenance in three places this
payload does not have it — `ram_console`'s write path, the reboot path, and every page-table write
followed by a TTBR switch — and enabling it without those produces a device whose log is stale
rather than wrong, which is the worst failure mode this project has. That is the next step, and
it is a separate one.

## Run 1: six places asserted "the caches are off", and the failure named none of them

Run 1 failed in the loader preflight with the familiar cascade — every pmap dry-run contract
reporting "source failed", then `pexpert`, then IOKit — rooted in
`xnu_pmap_bootstrap_contract`'s `failure_mask = 0x20` (`FAIL_WORKSPACE`). The check that failed:

```c
if (contract->workspace_allocation_tag == ... &&
    contract->workspace_mmu_enabled == 1u &&
    contract->workspace_cache_policy == 0u) {     /* <- the literal */
```

`workspace_cache_policy` is the live value read from `SCTLR`, so it is 1 the moment a cache is on.
The stated claim was "the workspace runs with caches disabled"; it should have been "the workspace
runs with the cache policy this build configured".

The same literal-0 assumption was in **eight** places in `mmu.c` — the VM plan's, VM state's and
pmap workspace's policy checks, the pmap bootstrap snapshot's two, and the three
`mmu high bootstrap selftest` comparisons. They had already been made mode-aware in this same
change by a single `STAGE90_VM_PLAN_CACHES_EXPECTED`, which is why the failure surfaced in the
contract module rather than in the MMU ladder; the contract was the one left behind.

All of them now use `STAGE90_EXPECTED_CACHE_POLICY`, defined once in `stage90.h` next to
`STAGE90_CACHE_MODE`. This is the **fourth** instance of one value with two definitions in this
project — the device-tree root child count, the pmap descriptor literals, and now the cache
policy — and the same remedy keeps being the right one: one definition, derived from the switch.

One related claim was made honest at the same time: the kernel command line carried the token
`no-cache-change`, which a run with the I-cache on would have been advertising falsely. It is now
`${CACHE_TOKEN}`, read from the preprocessor the same way the build config is, so it is
`no-cache-change` or `icache-enabled` according to what was compiled.

## Run 2: green

```
mmu_entry_low          =0x0001140e      Normal WBWA, SHAREABLE, AP=01 (PL1 RW, PL0 no access)
mmu_entry_gic          =0xf9010c02      MMIO unchanged - Strongly-ordered
mmu_sctlr_before       =0x00c5487a      M=0 C=0 I=0
mmu_sctlr_after        =0x00c5587b      M=1 C=0 I=1
ttbr_rt_cache_bits_*   =0x00001000      the I bit, unchanged across the TTBR0 roundtrip
ttbr_rt_caches_changed =0x00000000      the ladder itself changes nothing during its run
high_plan_cache_policy =0x00000001
kernel_entry returned success           and no line containing "failed" anywhere in the log
```

with `ram_console_verified=1` and the high-VA IRQ handler still delivering timer interrupts
(`irq_count 1 → 3`). The device returned to Android unattended.

So the payload now runs with a cache enabled — the first time in ninety-seven stages — and every
check that used to prove the caches were off now proves they are on in the configured way. The
attribute map's one-consistency rule holds throughout: DRAM is cacheable everywhere it is mapped,
MMIO is not, and the two PA sets are disjoint.

**Two claims this run does not make.** It does not show a speedup — no timing comparison was
made, and the point was correctness of configuration, not performance. And it does not exercise
code modification, which is the one thing that would need I-cache maintenance.

## What changed in response

- `stage90.h`: `STAGE90_PMAP_ATTR_MODE_NORMAL_WB` with its two descriptors;
  `STAGE90_CACHE_MODE` (`NONE` / `ICACHE`) with a compile-time requirement that `ICACHE` implies
  `NORMAL_WB`; and `STAGE90_EXPECTED_CACHE_POLICY` as the single definition the checks share.
- `mmu.c`: `invalidate_icache_all()`; `enable_identity_mmu()` sets `SCTLR.I` after an `ICIALLU`
  when configured; the eight cache-policy comparisons use the shared definition.
- `xnu_pmap_bootstrap_contract.c`: the workspace policy check uses the shared definition.
- `build.sh`: the cmdline's cache token follows the build; `STAGE90_CACHE_MODE` joins the config
  dump so the gate can see it.
- `preflight_boot_check.sh`: `--allow-attr-normal-wb` and `--allow-icache`, a "caches" section,
  and a consistency check that refuses `ICACHE` with an uncacheable attribute mode.

## What is outstanding in Phase 1

- **The D-cache**, with the three pieces of maintenance it makes mandatory: a clean in
  `ram_console`'s write path, a clean-and-invalidate of the log region before any reboot, and a
  clean of every page-table write that a later TTBR switch depends on.
- **F-AM2 is now fixed in passing** — the WB section descriptor is AP `0b01`, PL1-only, where the
  Strongly-ordered one was full access. Nothing in this payload runs at PL0, which is why it is
  safe to take with this step; the page descriptor already had PL1-only access.
