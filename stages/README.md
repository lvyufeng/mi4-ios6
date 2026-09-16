# Stage snapshots

Each directory here is one stage of the MSM8974 / Xiaomi Mi 4 (cancro) XNU
bring-up: a **complete, self-contained copy** of the previous stage plus that
stage's delta. Nothing is shared between snapshots and nothing is generated from
a common source tree — that is deliberate. A stage that has been validated on
hardware is frozen exactly as it was run, and any regression can be bisected by
building an older snapshot as-is.

`stage90` is the current tip; `stage85` … `stage90` are the six retained here.

## What a snapshot contains

| Path | Role |
| --- | --- |
| `build.sh` | The entry point. Builds everything into `out/stageNN/`. |
| `stageNN_main.c`, `stageNN.h` | Stage driver and the feature switches for this stage. |
| `*.c`, `*.S`, `linker.ld` | The payload: MMU, GIC, timers, pexpert/pe-state shims, the XNU-object dry-run contracts, the Mach-O loader, the handoff code. |
| `xnu_*.sh`, `xnu_compile_graph_scan.py` | Build helpers that consume the public XNU checkout in `external/`. |
| `targets/` | `cancro.mk` and the stage's object list. |
| `shims/` | Compatibility shims standing in for XNU headers. |
| `README.md`, `IMPLEMENTATION.md`, `IMPLEMENTATION_STATUS.md`, `QUICK_START.md` | Working notes. |

Paths in the build scripts are resolved from the repository root, computed at
run time:

```bash
STAGE_DIR=$PWD
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
```

so `./build.sh` works from any working directory, and the snapshot keeps working
if this directory moves again.

## The notes inside a snapshot are working notes

They are written during hardware debugging and are **not** kept in sync for their
own sake. A stage is created by copying its predecessor, so a carried-over
heading can name the previous stage — `stage90/README.md` still opens with
"Stage89: Mach-O Kernel Loader" and `stage90/QUICK_START.md` with "Stage89 Quick
Start", because stage90 added the handoff code on top of the Mach-O loader
rather than replacing it. `IMPLEMENTATION.md` and `IMPLEMENTATION_STATUS.md` are
the ones rewritten per stage, and they carry the authoritative current status.

The chronological record of each experiment lives in `../docs/experiments/`;
`../docs/history/milestones.md` covers Stage0 – Stage81.

## Retention policy

The six most recent snapshots stay in the working tree. Everything older
(`stage0` … `stage84`) has been removed from the tree and lives on in git at the
tag `stage-archive-base`, which points at the last commit where all 91 stage
directories were present.

Six is a working compromise: deep enough to bisect a regression across the
recent high-VA / loader / handoff work, shallow enough that the tree stays
reviewable. When a new stage is added, the oldest snapshot is archived to keep
the count at six.

```bash
tools/stage-archive.sh list        # what is archived, and where the rest is
tools/stage-archive.sh show 50 xnu_workspace.c   # read one file, no restore
tools/stage-archive.sh restore 50  # -> ./stage50/ (repo root, so it builds)
tools/stage-archive.sh diff 89     # archived copy vs. the working tree
```

A restored stage lands at the repository root, not here: its scripts were
written when every stage lived there, so `../out` and `../external` only resolve
correctly from that depth. Remove it with `rm -rf stage50` when done.

## Adding the next stage

1. `git mv stages/stage90 stages/stage91` then copy it back — or more simply
   `cp -r stages/stage90 stages/stage91`.
2. Rename the stage tokens: `stage90_main.c` → `stage91_main.c`, `stage90.h` →
   `stage91.h`, `targets/cancro.stage90.objects` → `cancro.stage91.objects`, and
   replace `STAGE90_` / `stage90` throughout the scripts, the object list and the
   source.
3. Point the new switches in `stage91.h` at the feature under test, keeping the
   previous stage's safe default until the new one has been validated on
   hardware.
4. Archive the oldest snapshot to stay at six: `git rm -r stages/stage85`.
5. Update `IMPLEMENTATION.md` and `IMPLEMENTATION_STATUS.md` for the new stage.

`make list` shows the retained stages; `make` builds the newest one.

## Safety

Boot these images non-persistently — `sudo fastboot boot out/stageNN/stageNN-qcdt.img`.
See the root `README.md` and `docs/reference/recovery-and-rollback.md` before
any write that survives a reboot.
