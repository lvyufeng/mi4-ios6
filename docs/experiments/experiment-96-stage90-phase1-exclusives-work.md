# Experiment 96 — Stage90 Phase 1: LDREX/STREX work on this device, and the probe said otherwise for four runs

Date: 2026-09-17
Commit under test: `54e0163`, plus the probe changes described below
Build switches: `STAGE90_EXCLUSIVE_PROBE = 1`, run once with
`STAGE90_PMAP_ATTR_MODE = SO_ONLY` (default) and once with `NORMAL_NC`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`

| # | Build | Result |
| --- | --- | --- |
| 1 | two-phase probe (phase 2 added), `SO_ONLY` | phase 1 and 2 both `monitor_tracks=0` |
| 2 | same, `NORMAL_NC` | phase 2 still 0, with the descriptor verified `0x00011c02` |
| 3 | + phase 3 (cacheable mapping, D-cache on, `ACTLR.SMP`) | still 0; **`ACTLR` reads 0 and ignores writes** |
| 4 | + T5 (`CLREX`) and T6 (store to another address) | **`clrex_strex_status=1` — the monitor works** |
| 5 | same build, `SO_ONLY` | identical: works in all three phases |

Raw captures: `/tmp/kmsg-probe2-so.txt`, `/tmp/kmsg-probe2-nc.txt`, `/tmp/kmsg-probe3-nc.txt`,
`/tmp/kmsg-probe3b-nc.txt`, `/tmp/kmsg-probe4-nc.txt`, `/tmp/kmsg-probe5-so.txt`.

## The answer

```
              MMU   memory type at the probe word            T2   T3   T5   monitor_clears
phase 1       off   (none - no descriptor applies)           0    0    1    1
phase 2       on    Strongly-ordered                         0    0    1    1
phase 2       on    Normal, Non-cacheable                    0    0    1    1
phase 3       on    Normal, Write-Back, D-cache on           0    0    1    1
```

`T2` = an undisrupted `LDREX`/`STREX` pair. `T3` = a plain store to the reserved address
between `LDREX` and `STREX`. `T5` = `CLREX` between them. STREX status 0 means success, 1 means
failure, so **T5 failing is the positive result**: `LDREX` set the monitor, `CLREX` cleared it,
and `STREX` correctly refused to store.

**The exclusive monitor is implemented and working on MSM8974/Krait, in every configuration
measured — including Strongly-ordered memory, and including with the MMU switched off.**

Phase 1's "working `LDREX`/`STREX`" exit criterion is therefore **met**, and it was met before
any of the memory-attribute work was done.

## Two separate defects in the probe produced four runs of false negative

**1. The probe ran with the MMU off, and did not record it.** `stage90_exclusive_probe_run()`
was called from `kernel_entry` before `mmu_identity_selftest()`, which is the function that
calls `enable_identity_mmu()`. The MMU is disabled on entry to the payload — aboot hands over
with `SCTLR.M = 0`, `TTBR0 = 0x0f210000` (aboot's own table, never ours) — so the probe ran with
no translation at all, on physical addresses. With the MMU off, ARMv7 treats **every** access as
Strongly-ordered and non-shareable, so no page-table descriptor is consulted and the attribute
mode under test could not change the result.

That is exactly what the two identical measurements meant: `SO_ONLY` and `NORMAL_NC` produced
digit-for-digit the same numbers because they were the same configuration. The probe's own
comment claimed it ran "under whatever mapping is already live"; there was no mapping.

The probe now records `sctlr`, `ttbr0` and `mmu_enabled` in its result, which is what makes the
state visible in the log instead of inferable from the call order. Two illustrative values from
run 1: `exclusive_probe_sctlr=0x00c5487a` (bit 0 clear) and
`exclusive_probe_ttbr0=0x0f210000` (not our `stage90_l1_table`).

**2. T3's discriminator encodes a premise that is false on this part.** T3 was the whole reason
the probe existed: "a plain store to the reserved address clears the exclusive monitor, so a
correct monitor must then make `STREX` fail." On Krait it does not. Runs 1–3 show
`disrupted_strex_status = 0` in every configuration, and run 4 adds T6 — a plain store to a
**different** address between `LDREX` and `STREX` — which also leaves the monitor set. What
clears it here is `CLREX`.

This is the failure mode the probe was built to avoid, one level up. Its docstring says a naive
implementation returning success from `STREX` unconditionally is indistinguishable from a
working one unless T3 is checked. True — but T3 is only a valid check if its premise holds, and
the probe was asserting the premise rather than testing it. The corrected criterion is
`monitor_clears` (T2 succeeds and T5 fails), and it is the mechanism XNU itself uses:
`osfmk/arm/atomic.h`'s `clear_exclusive()` is `__builtin_arm_clrex`, and
`osfmk/arm/locks.h`'s `wait_for_event()` is the same instruction.

## What the digression found on the way

- **`ACTLR` reads 0 and ignores writes.** `exclusive_probe_dcache_actlr_before=0x00000000`,
  `actlr_raised=1` after writing bit 6, and `actlr_after=0x00000000`. On ARMv7 `ACTLR.SMP` must
  be set before the caches are enabled for them to be coherent, and this payload has never
  written ACTLR — so whatever aboot leaves is what runs, and here that is zero and un-writable
  from non-secure PL1. This is now known rather than assumed, and it is a fact to carry into
  Phase 1's cache work rather than a blocker for the exclusive monitor (which works regardless).
- **The payload runs with the MMU off for its first ~150 log lines** — the device tree build,
  `pexpert` discovery, `boot_args`, the GIC snapshot and the dead-man arming all execute untranslated.
  True of every stage so far; it had simply never been measured.
- **Blast radius of phase 3 was bounded and it restored everything**: one spare 1 MB DRAM section
  (PA `0x00200000`) mapped Normal-WBWA after verifying the identity table had no entry for it and
  that a write/read-back round-trip took, then `SCTLR.C` set, then both cleared and the entry put
  back — `sctlr_restored=0x00c5487b`, `actlr_restored=0x00000000`, `l1_entry_restored=0x00000000`.
  Nothing else in the payload is cacheable, so no page-table maintenance and no `ram_console`
  clean were needed, which is why the D-cache could be tested before Phase 1's cache work.

## What this changes

- **Roadmap finding F2 is corrected.** It said "ARMv7 `LDREX`/`STREX` are architecturally only
  defined on Normal memory. On Strongly-Ordered/Device memory the exclusive monitor is
  unpredictable. The current pmap therefore cannot support a single lock, spinlock or atomic."
  Measurably false on this device: the monitor tracks on Strongly-ordered memory and with the
  MMU off. Locks are not blocked by the memory type here.
- **Phase 1's exclusives criterion is met**, so what remains in Phase 1 is the caches — which
  are still needed, for a different reason than F2 gave: XNU's `start.s` enables the I-cache and
  assumes cacheable Normal memory for kernel text and data, and any DMA-capable driver needs
  correct Normal/Device distinctions. The attribute work keeps its value as that prerequisite;
  it was never needed for atomics.
- **`experiment-95`'s Phase 1a conclusion is wrong and is corrected there**: it reported "the
  memory type alone is not why exclusives misbehave", on the strength of an identical-result
  comparison between two builds that were both measuring nothing. The corrected statement is
  that the comparison was meaningless, and that exclusives in fact work in both modes.
