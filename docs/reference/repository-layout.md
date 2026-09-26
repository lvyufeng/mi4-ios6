# Repository layout, and what was retired to get here

This is a conventional single-tree repository: one evolving line of work on `master`, one live tree,
and the build products of the current bring-up under `out/`.

## Where everything is

| Path | What it is |
| --- | --- |
| `src/` | The live tree. The payload's 67 `.c` files are **flat here on purpose** — `scripts/build.sh` names objects `out/stage90/$(basename "${src%.*}").o`, so splitting them into subdirectories would introduce basename collisions and force edits to `build.sh`, `src/targets/cancro.mk` and `cancro.stage90.objects` for no reader-visible gain. Beside them: `src/entry/` (the ARM entry-image builder and its image), `src/platform/`, `src/supply/`, `src/shims/`, `src/shims_arm/`, `src/firehose/`, `src/targets/`. |
| `scripts/` | What builds and fires it: `build.sh` (the payload), `preflight_boot_check.sh` (**the gate**), `run_and_capture.sh` (**the runner**), `preflight_storage_write.sh`, and the `xnu_*.sh` compile-graph helpers. Every one of them resolves the repository root itself, so it runs from any working directory. |
| `records/` | The arm record `records/revert-set.txt`, `records/baseline-readings.txt`, and `records/tool-images/` with the TWRP image's detached signature. |
| `tools/` | Host-side checkers. The press path's own tools are `verify_press_ready.sh` (the readiness check), `resolve_arm_set.sh` (which recorded set are these bytes), `verify_revert_set.sh` (the park against the record), `gate_flags_for_arm.sh`, and `check_stage_paths.sh`. |
| `docs/` | Documentation, indexed in [`docs/README.md`](../README.md). `docs/experiments/**` is a historical record and is never rewritten. |
| `archive/stages/` | The five snapshots the retired snapshot-per-stage model left in the working tree (`stage85` … `stage89`). `stage0` … `stage84` live at the tag `stage-archive-base`. [`archive/stages/README.md`](../../archive/stages/README.md). |
| `out/stage90/` | Build products, ignored by git: the payload, the entry image, `frozen/` (the parked arms) and `captures/` (the run logs). |
| `external/` | The public XNU checkout the compile-graph helpers consume, ignored by git. |

`stage90` is still the name of the current payload, its `.img`, its object plan and `out/stage90/`.
Only directories moved; the identifiers did not.

## What was retired, and what happened to the refs

**The snapshot-per-stage model, retired 2026-09-26.** Until then a new stage meant a complete copy of
the previous stage's tree plus that stage's delta, and the six most recent copies stayed in the working
tree. It bought bisect-by-snapshot and a frozen, rebuildable copy of every validated stage; it cost a
tree in which the live code sat one level down inside a directory that looked historical, a `Makefile`
whose only job was directory discovery, and — the deciding cost — a press path whose scripts lived
*inside* a snapshot and therefore could not be reorganized without also editing what they pin. What
was actually needed is kept outside the tree instead: the parked arms in `out/stage90/frozen/` and the
arm hashes in `records/revert-set.txt`.

**The other branch lines, retired the same day.** "Evolve only on `master`" is a claim about the refs,
so the refs were checked first and then removed. Every ref below was a **strict ancestor of `master`
with zero exclusive commits**, which is what makes the removal lossless — `git merge-base --is-ancestor`
was run against each one before `git branch -d`, and `git branch -d` itself refuses an unmerged branch.
The shas are recorded here because the refs are gone:

| Ref | sha | Note |
| --- | --- | --- |
| `main` (remote, and its `origin/main` tracking ref) | `3917d6f273f1384c648d397573c03f0bd238a25a` | 0 exclusive commits; GitHub's default branch was already `master`, so `git push origin --delete main` was accepted. |
| `stage8-sgi-irq` | `3917d6f` | The same commit as `main`. |
| `stage85-high-va-code-exec` | `b92b151` | |
| `stage86-high-va-irq-handler` | `a20a9aa` | |
| `stage87-high-va-data-abort-handler` | `dd22400` | |
| `stage88-high-va-undef-handler` | `ade8e78` | |
| `stage89-macho-loader` | `652fc1a` | |
| `stage90-xnu-handoff` (local and remote) | `dfdf3c2` | Had been an alias of `master` for many commits. |

After the pruning, `git ls-remote origin` returns exactly two lines — `HEAD` and `refs/heads/master`,
both at `master` — and `git branch -vv` lists `master` alone.

**The relocated-stage caveat.** A snapshot restored by `tools/stage-archive.sh restore <N>` lands at
the repository **root**, not under `archive/`: it predates the move, so its scripts resolve `../out`,
`../external` and `../tools`, which only works from that depth. The five under `archive/stages/`
predate the move too and are kept for reading and for bisecting an old regression, not as a build path.

## The rules that still govern a run

Nothing above changes them. Booting is deliberately **not** a `make` target: `fastboot boot` is
non-persistent and is a per-action decision. The gate and the runner are run once each, with the flags
`tools/gate_flags_for_arm.sh` derives for the arm in `out/`, and the device is never flashed. Read
[`recovery-and-rollback.md`](recovery-and-rollback.md) before any write that survives a reboot.
