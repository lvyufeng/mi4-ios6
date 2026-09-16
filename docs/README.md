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
- `xnu-handoff-contract.md` — what public ARM XNU's `_start` actually requires of `boot_args`, read off `osfmk/arm/start.s`, and where the payload does not yet provide it. Phase 2's input.

## Status — `status/`

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
