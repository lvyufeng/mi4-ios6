# Archived stage snapshots

**The snapshot-per-stage model was retired on 2026-09-26.** Until then, a new stage meant a
complete copy of the previous stage's tree plus that stage's delta, and the six most recent
copies stayed in the working tree. The repository is now **one evolving tree** and development
happens in place, on `master` only. Nothing here is copied forward and nothing here is built
by the normal workflow.

What is in this directory is what the model left behind:

| Path | What it is |
| --- | --- |
| `stage85` … `stage89` | The five snapshots that were in the working tree when the model was retired. Each is a complete, self-contained copy of that stage plus its delta. |
| the tag `stage-archive-base` | The last commit at which **all 91** stage directories were present (`stage0` … `stage90`). `stage0` … `stage84` exist only there. |

```bash
tools/stage-archive.sh list        # everything under the tag, and where it is present now
tools/stage-archive.sh show 50 xnu_workspace.c   # read one file, no restore
tools/stage-archive.sh restore 50  # -> ./stage50/ (repository root, so it builds)
tools/stage-archive.sh diff 89     # archived copy vs. the working tree
```

A restored stage lands at the repository **root**, not here: its scripts were written when every
stage lived at `<root>/stageN/`, so `../out`, `../external` and `../tools` only resolve correctly
from that depth. Remove it with `rm -rf stage50` when done. `stage85` … `stage89` in this
directory predate that move too, so their scripts also assume they are two levels up — they are
kept for reference and for bisecting an old regression, not as a build path.

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

## The notes inside a snapshot are working notes

They were written during hardware debugging and are **not** kept in sync for their own sake. A
stage was created by copying its predecessor, so a carried-over heading can name the previous
stage — `stage90/README.md` still opened with "Stage89: Mach-O Kernel Loader" because stage90
added the handoff code on top of the Mach-O loader rather than replacing it.
`IMPLEMENTATION.md` and `IMPLEMENTATION_STATUS.md` are the ones rewritten per stage, and they
carry the authoritative status *as of that stage*.

The chronological record of each experiment lives in `../docs/experiments/` and is never
rewritten — those logs are a record of what was run at the time, so their path commands name
the layout that existed then. `../docs/history/milestones.md` covers Stage0 – Stage81.

## Why the model was retired

It bought bisect-by-snapshot and a frozen, rebuildable copy of every validated stage. It cost a
tree in which the live code sat one level down inside a directory that looked historical, a
`Makefile` whose only job was directory discovery, and — the deciding cost — a press path whose
scripts lived inside a snapshot and therefore could not be reorganized without also editing
what they pin. The frozen artefacts that actually matter are kept outside the tree instead:
the parked arms in `out/stage90/frozen/` and the arm hashes in `records/revert-set.txt`.

## Safety

Boot these images non-persistently — `sudo fastboot boot out/stageNN/stageNN-qcdt.img`. See the
root `README.md` and `docs/reference/recovery-and-rollback.md` before any write that survives a
reboot.
