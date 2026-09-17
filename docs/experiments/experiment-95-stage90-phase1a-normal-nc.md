# Experiment 95 — Stage90 Phase 1a: Normal, non-cacheable DRAM

Date: 2026-09-17 (runs 03:23–03:29 UTC)
Commit under test: `3c5dfca`, plus three local fixes made during this experiment
Build switches: `STAGE90_PMAP_ATTR_MODE = STAGE90_PMAP_ATTR_MODE_NORMAL_NC`,
`STAGE90_EXCLUSIVE_PROBE = 1`; `STAGE90_HANDOFF_MODE = HARD_SKIP`; both recovery nets armed
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`

| # | Build | Result |
| --- | --- | --- |
| 1 | `NORMAL_NC` + probe, as committed | `kernel_entry returned failure` — the pmap bootstrap contract compared the real descriptor against the Strongly-ordered literal |
| 2 | after making the bootstrap + table-dryrun constants mode-dependent | `kernel_entry returned failure` — two more literals of the same kind |
| 3 | after all four | **`kernel_entry returned success`** |

Raw captures: `/tmp/kmsg-normalnc{,2,3}.txt`.

## What was being tested

Phase 1a from `docs/status/roadmap.md` §5 — the smallest change that makes ARMv7 exclusives
architecturally defined. `STAGE90_PMAP_ATTR_MODE = NORMAL_NC` moves every **DRAM** mapping from
the Strongly-ordered descriptor `0x00010c02`/`0x00000012` to the Normal-Non-cacheable
`0x00011c02`/`0x00000452`, and leaves **MMIO** Strongly-ordered in both modes. It is deliberately
non-cacheable: with `SCTLR.C` and `SCTLR.I` both still clear it needs no cache maintenance and
no change to how page-table writes become visible, so a failure here is attributable to the
memory type alone.

The question it was built to answer is the one `exclusive_probe.c` has been asking since
experiment-94: the probe measured `monitor_tracks=0` and `exclusives_usable=0` under
Strongly-ordered DRAM, and Phase 1a is the test of whether the memory type is why.

## Runs 1 and 2: the attribute switch worked; the contracts did not

Run 1 failed in the loader preflight, and the shape of the failure is the interesting part.
Every real descriptor had moved:

```
high_plan_section_descriptor            =0x00011c02     (was 0x00010c02)
high_vmstate_pmap_section_descriptor    =0x00011c02
high_pmapws_l1_section_descriptor       =0x00011c02
stage90_xnu_pmap_workspace_l1_section_descriptor=0x00011c02
```

and the pmap bootstrap contract failed with `failure_mask=0x20` (`FAIL_WORKSPACE`) because of
one line:

```c
contract->workspace_l1_section_descriptor == STAGE90_XNU_PMAP_BOOTSTRAP_SECTION_DESC_SO
```

`STAGE90_XNU_PMAP_BOOTSTRAP_SECTION_DESC_SO` was the literal `0x00010c02u`, and the value it was
compared against comes from the live snapshot. One value, two definitions — and the second one
did not follow `STAGE90_PMAP_ATTR_MODE`. Everything else failed downstream of it: the table,
page, attribute, multi-window and transition dry-run contracts each report "source failed", and
the pexpert and IOKit contracts cascade off those, so a one-line defect in the bootstrap contract
took out the whole loader preflight.

Run 2 fixed that and failed on two more literals of exactly the same kind
(`xnu_pmap_table_dryrun_contract.c`'s constants check, and the attribute dry-run's check that
the table dry-run's section word is what it expects). Run 3 fixed those and passed.

This is the third time this project has been bitten by one value with two definitions — the
device-tree root child count (`19u` in two places, experiment-94), and now three descriptor
literals. The fix in every case is the same and is now applied here: the descriptor constants are
**aliases of `STAGE90_PMAP_DESC_SECTION_DRAM`**, so the mode has exactly one definition and a
future mode cannot leave a checker behind.

## Run 3: the payload boots end to end on Normal memory

```
kernel_entry returned success
```
with **no** line containing "failed" anywhere in the capture (3904 lines, every one carrying a `MI4IOS6_STAGE90` marker). On the way:

- Every DRAM descriptor is `0x00011c02` and the table dry-run's simulated descriptors match.
- `stage90_xnu_arm_vm_init_full_pmap_ram_console_verified=0x00000001` — `ram_console` is
  reachable and correct through the Normal-NC mapping, so the log this run was read from is
  itself the evidence that logging still works.
- `high_va_irq_handler_irq_count 1 → 3`, `timer_irq_count 1 → 3`, `irq_delivered=1`, and VBAR
  restored to `0x000080a0` — timer interrupts are delivered through a Normal-NC-mapped payload.
- The high-VA data-abort and undef handler probes both fired and were handled.
- The Mach-O loader still refuses all six segments (`segments_unmapped=6`), which is the
  documented state from experiment-94 and not a regression: the candidate L1 is not installed in
  `HARD_SKIP`, so the destination really is unmapped at loader time.

That is a milestone in its own right: the whole bring-up layer — vectors, GIC, timer, exception
handlers, page tables, the device tree — runs on Normal memory for the first time. Every one of
the previous ninety stages ran on Strongly-ordered memory.

## The Phase 1a question, answered: no

The exclusive probe reports, on hardware, under Normal-Non-cacheable DRAM:

```
exclusive_probe_ldrex_reads_word        =1
exclusive_probe_undisrupted_strex_status=0x00000000   success
exclusive_probe_disrupted_strex_status  =0x00000000   success - and T3 says it must fail
exclusive_probe_success_count           =1000  of 1000
exclusive_probe_fail_count              =0
exclusive_probe_monitor_tracks          =0
exclusive_probe_exclusives_usable       =0
```

**Identical to the Strongly-ordered measurement, digit for digit.** So the working hypothesis
behind Phase 1a — that exclusives misbehave because the memory is Strongly-ordered — is
falsified, at least in the form "Strongly-ordered is the reason". Changing the memory type to
architecturally-legal Normal memory did not make the monitor track.

`undisrupted_strex_status=0` and `disrupted_strex_status=0` together are the specific finding:
`STREX` succeeds whether or not a plain store intervened, which is what an implementation with
no local monitor at all looks like. `exclusives_usable=0` is therefore a correct reading, not a
probe artefact — T3 exists precisely to distinguish "STREX works" from "STREX always reports
success".

What this does **not** yet establish is *why*. Two candidates remain, and they need different
tests:

1. **The monitor needs cacheable memory, not merely Normal memory.** Some ARMv7
   implementations only implement the local monitor for cacheable accesses. That is testable by
   going to Normal **write-back** — the next step anyway — and re-running the probe unchanged.
2. **`ACTLR.SMP` is clear.** On ARMv7 the SMP bit governs whether the implementation takes part
   in coherency and whether the exclusive monitor is the multiprocessor one. The payload has
   never written `ACTLR`, so whatever aboot left there is what runs. This is cheap to read and
   report, and it is worth reading *before* the next run rather than guessing.

Both are Phase 1 work, and neither is a reason to keep `NORMAL_NC` or to drop it: it is now a
known-good, hardware-verified platform for the cache steps, which is exactly what it was for.

## What changed in response

- `stage90.h`: `STAGE90_XNU_PMAP_BOOTSTRAP_SECTION_DESC_SO` and
  `STAGE90_XNU_PMAP_TABLE_DRYRUN_DESC_SECTION_SO` are replaced by `..._DESC_DRAM` aliases of
  `STAGE90_PMAP_DESC_SECTION_DRAM`; the two code literals that compared against them
  (`xnu_pmap_table_dryrun_contract.c`, `xnu_pmap_attr_dryrun_contract.c`) now use the alias.
- `docs/reference/pmap-attribute-map.md` gains the Normal-NC descriptor rows and the
  one-definition rule.

## What is still outstanding in Phase 1

- Cacheable Normal memory: I-cache, then D-cache with the `ram_console` clean the D-cache makes
  mandatory.
- A passing `LDREX`/`STREX`, which the roadmap makes a Phase 1 exit criterion and which this run
  shows is **not** delivered by the attribute change alone.
