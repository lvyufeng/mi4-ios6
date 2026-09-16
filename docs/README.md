# Documentation index

`docs/` is split by purpose:

| Directory | Contents |
| --- | --- |
| `reference/` | Platform research, boot tooling, device findings, safety/recovery rules, source baselines. Stable background material. |
| `experiments/` | One bring-up log per experiment (`experiment-01` … `experiment-92`), in chronological order. |
| `status/` | Current progression summary and next-step analysis. |
| `history/` | Superseded design documents and the per-stage milestone log moved out of the root `README.md`. |

## Reference — `reference/`

- `local-device-findings.md` — detailed local observations, partition map, backup status, and parsed boot/recovery image fields.
- `recovery-and-rollback.md` — required recovery checklist and rollback procedure before any persistent write.
- `boot-tooling.md` — local boot image tooling plan and no-op round-trip results.
- `cancro-platform.md` — Xiaomi Mi 4 / MSM8974 platform source pointers and bootloader notes.
- `darwin-xnu-research.md` — open Darwin/XNU research notes and milestone framing.
- `source-baseline.md` — external source checkout baseline for cancro kernel/device tree and public XNU references.
- `upstream-xnu-analysis.md` — selected iOS 6-era public XNU tag analysis, Stage42 gap review, and revised Stage43 loader direction.
- `no-teardown-debugging.md` — USB-only debugging channels (`/proc/last_kmsg`, `/dev/kmsg`, ramoops) that avoid soldering a UART.
- `msm8974-xnu-porting-map.md` — concrete MSM8974 ↔ XNU platform interface and work-package map.
- `ios-613-oss-baseline.md` — notes on Apple OSS `distribution-iOS@ios-613` and public XNU baseline implications.
- `pmap-attribute-map.md` — every mapping the payload creates, its ARMv7 short-descriptor encoding (decoded with `tools/decode_armv7_descriptor.py`), and the Phase 1 target attributes. Read this before blaming a fault on memory attributes.
- `xnu-handoff-contract.md` — what public ARM XNU's `_start` actually requires of `boot_args`, read off `osfmk/arm/start.s`, where the payload does not yet provide it, and how the conforming object resolves each gap. Phase 2's reference.

## Host tools — `tools/`

- `check_xnu_struct_abi.py` — compares our `boot_args` layout against `pexpert/pexpert/arm/boot.h` for ARM ILP32, field by field. `start.s` loads four of those fields by hand at fixed offsets, so a drift is silent: XNU uses the wrong word as the physical base of memory rather than failing to build. `build.sh` runs it.
- `decode_armv7_descriptor.py` — decodes a short-descriptor page-table entry; field positions from Apple's own `osfmk/arm/proc_reg.h`, not from memory.
- `xnu_dt_requirements.py` — scans XNU's ARM sources for the device-tree lookups they make and checks our Apple-format DT against them, classified by what XNU does when each is missing. Also validates each node header's declared property count.
- `host_dt_check.sh` — compiles XNU's own `pexpert/gen/device_tree.c` for the host and walks our device tree with it, checking the property *values* the source scan cannot; also runs `host_dt_selftest_probe` against the payload's own selftest. Extracts the shipping builder verbatim rather than copying it.
- `host_dt_selftest_probe.c` — corrupts one device-tree node-header field at a time and confirms the payload's own selftest catches each, which is the claim that makes a device-tree edit safe without a hardware run.
- `check_storage_refs.py` — fails if a payload can address an MSM8974 storage controller, by `movt` high halves and literal-pool words. Closes a hole the symbol tripwire could not see: with a deliberate storage store added to the payload, the symbol check found nothing.
- `count_descriptors.py` — counts ARMv7 page-descriptor constants in a built payload. Its `--diff` mode compares two builds that differ by one switch, which is the sound way to check that a memory-attribute switch does what it claims; absolute counting is unreliable because a descriptor is often built in pieces.
- `stage-archive.sh` — list/restore the archived stage0–84 snapshots.

## Status — `status/`

- `phase3-msm8974-shim-spec.md` — what an MSM8974 replacement for XNU's ARM platform bring-up must provide: the interface to satisfy, the hardware values it must encode (with evidence), and what cannot be settled from the host.
- `unvalidated-change-audit.md` — bounds the risk of each change made on 2026-09-16 that has not
  reached hardware, in boot order, and names the log line that confirms or clears each. Read
  alongside the roadmap before the next run.
- `roadmap.md` — **the current plan.** Re-planned after Stage90: what has actually been
  proven, the three findings that invalidated the previous plan (the handoff target is not
  code; every mapping is strongly-ordered with caches off; no public iOS 6-era ARM XNU
  exists), the Phase 0–5 technical roadmap with hardware exit criteria, and the open
  strategic decision.
- `method-c-progression-summary.md` — the Method-C staged XNU bring-up progression, level by
  level. **Historical:** written at Stage83, uses the pre-reorganization flat `stageN/`
  paths, and its "public XNU `_start` executes" entries refer to Stage-owned stubs with XNU
  names (see `../status/roadmap.md` §2). Read it as a record of the stages, not as current
  status.
- `stage82-next-step-analysis.md` — Stage82 next-step analysis (written in Chinese).

## History — `history/`

- `milestones.md` — the per-stage "Confirmed milestones" log, moved out of the root `README.md`.
- `stage0-payload-plan.md` — plan for the first non-Linux ARMv7 payload executed via `fastboot boot`.
- `stage1-boot-wrapper-plan.md` — Stage1 boot-wrapper design for XNU-style `boot_args` and Apple-DT handoff.
- `stage82-pmap-design.md` — Stage82 full kernel virtual address space pmap design.
- `stage84-sgi-irq-timer-planning.md` — Stage84+ SGI/IRQ timer boundary planning.

## Experiments — `experiments/`

See `experiments/README.md` for the full cross-reference table (experiment number ↔
stage number ↔ title). Note the offset: experiment NN corresponds to stage NN-3.

Path commands written inside the experiment logs are a record of what was actually
run at the time and have deliberately not been rewritten. Stages 00-84 are no longer
in the working tree — see `../stages/README.md` for the retention policy and
`tools/stage-archive.sh` to restore one.
