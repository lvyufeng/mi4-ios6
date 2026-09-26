# History

Superseded design documents and the per-stage milestone log, kept for the record.

- `milestones.md` — the "Confirmed milestones" log that used to sit in the root
  `README.md`, covering Stage0 through Stage81. It is a chronological narrative of
  what each stage proved on hardware, including the recovered `last_kmsg` numbers.
  Stage82 onwards is recorded in `../experiments/` instead.
- `stage0-payload-plan.md`, `stage1-boot-wrapper-plan.md` — plans from the very
  first stages (both stages are now archived).
- `stage82-pmap-design.md`, `stage84-sgi-irq-timer-planning.md` — design documents
  for Stage82 and Stage84 (both archived).

## Archived stages

The snapshot-per-stage model was retired on 2026-09-26, so there is nothing to
keep up to date here any more. The five snapshots that were still in the working
tree at that point are kept in place at `archive/stages/stage85` …
`archive/stages/stage89`; every earlier snapshot is still in the git history, and
the tag `stage-archive-base` points at the last commit where all 91 `stageN/`
directories were present in the tree.

```bash
tools/stage-archive.sh list          # archived stage numbers
tools/stage-archive.sh restore 50    # -> ./stage50/, at the root so it builds
```

## A note on paths in archived documents

Stages 00-84 used to live in the repository root as `stageN/`, and the experiment
logs from that period contain commands such as `./stage51/build.sh`. Those commands
are left exactly as they were run. Rewriting them would falsify the log, and the
paths only make sense again after the corresponding stage has been restored anyway.

For the six retained snapshots the paths *have* been updated: their build scripts
now resolve the repository root themselves (`REPO_ROOT=$(cd "$STAGE_DIR/../.." &&
pwd)`), so `stages/stageNN/build.sh` works from any working directory and keeps
working if the directory moves again.
