# 875 — HFS+ STAGED, AND THE PORT'S ADDITIONS GET ONE DEFINITION

**A source-staging step: no build, no arm, no park, no switch, no device.** The rung-57/58/59/60 arms stay
parked and unpressed. **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 871/873 measured 2050's HFS+ at 38/38 compile and a zero link gap, but only *inside the
probe's `/tmp` sandbox* — 2050's sources were never in 4570's tree, and the port's additions (the five malloc
types, the port's own options, the ten shims) lived in a heredoc the probe rebuilt each run. This step makes
the port a real, re-appliable, **tracked** thing: `tools/stage_hfs.sh` copies 2050's HFS+ into 4570's tree and
applies the one in-place edit; the port's additions become **tracked files** (`src/shims/hfs/hfs_port_force.h`
and `src/supply/stage90_hfs_shims.c`) that the probe **and** the future build both use; and `make check` gains
a drift check so the untracked tree cannot silently fall behind them.

## 1. The four tracked artifacts, and why each is tracked

| file | what it is | who uses it |
| --- | --- | --- |
| `tools/stage_hfs.sh` | stages 2050's HFS+ into the untracked 4570 tree; idempotent | a build step, and `make check` (indirectly) |
| `tools/hfs_patch_macos_defs.py` | the ONE in-place edit (`false`/`true`) | the probe **and** `stage_hfs.sh` |
| `src/shims/hfs/hfs_port_force.h` | every macro and option the port ADDS to 4570 | the probe **and** the future build |
| `src/supply/stage90_hfs_shims.c` | the ten symbols 4570 lacks (871) | the probe **and** the future build |

**The rule this follows is the project's own**: `external/` is a re-provisionable checkout (`.gitignore:12`;
README: "keep large source checkouts under the ignored `external/`"), so anything that has to survive a
re-checkout must be a tracked copy plus a re-appliable script — not a hand edit to the untracked tree. The
2,050-line HFS+ tree is *data being ported*; what this step puts in the repository is *the port*.

## 2. What `stage_hfs.sh` stages, exactly

It mirrors what the probe copies, and nothing the probe did not measure:

- `bsd/hfs/` (2050) → `bsd/hfs/` (4570), minus Apple's own `Makefile` — **36 `.c` files**;
- `bsd/vfs/vfs_journal.c` + `.h` (the `#if JOURNALING` source HFS calls into);
- `bsd/machine/spl.h` (included by `hfs_vnops.c`, absent from 4570);
- the **one** in-place substitution: `hfs_macos_defs.h`'s `false`/`true` enum → the macro definitions it used
  to make, because 4570 reaches `<stdbool.h>`, which `#define`s both.

It deliberately does **not** add `conf/files` rows or a `MASTER` option: the port's own options (`HFS`,
`HFS_COMPRESSION`, `CONFIG_HFS_STD`, `JOURNALING`) and macro additions reach the cpp through the **force
header**, so the port's additions live in one tracked file rather than in rows of an untracked one.

## 3. One definition, not two

The probe *and* the build must agree on what the port adds, and 871's whole number is about the port's real
dependencies. Before this step, the probe rebuilt its additions from a heredoc each run — a second copy of the
same list, which is this project's **"one value, two definitions"** defect: the probe would be measuring its
own copy, not the build's. So:

- the additions moved into `src/shims/hfs/hfs_port_force.h`, force-included by the probe;
- the ten shim bodies moved into `src/supply/stage90_hfs_shims.c`, copied into the probe's tree;
- the `false`/`true` edit moved into one tool both callers run.

Re-measured after the refactor: the probe still reports **38/38** (both targets) and a **zero** link gap, and
`stage_hfs.sh` stages the **same 36 files** the probe compiles. The number 871/873 recorded is now a statement
about files the repository holds, not a script's private copy.

## 4. The check, and it fires in both directions

`tools/check_hfs_staged.sh`, in `make check`, refuses:

- a force-header `#define` that **4570 already defines at a different value** — the "one value, two
  definitions" defect, which is *silent* when it happens (a `#define` of an existing name just wins or loses
  by include order); and
- a shims file that no longer defines the **ten** symbols 871 measured the port at.

Both negatives were run on copies before the check was trusted: removing `is_suser` from the shim file →
refusal naming it; adding `#define M_TEMP 42` (4570's `M_TEMP` is 80) → refusal naming both values. A check
that has only ever passed cannot be told from one that never ran.

## 5. The honest bound

- **Nothing is built.** The HFS sources are in the tree and compile under the probe's invocation; they are
  **not** in `out/xnu_arm_manifest.txt`, so no kernel contains them.
- **The root row is still owed, and 874 says where.** An HFS entry must be in the **static** `vfstbllist[]`
  *before* mockfs; a `vfs_fsadd` registration lands after it and is never tried as a root.
- **The medium is still owed (867).** Even a compiled, wired-in HFS+ reaches no eMMC driver that moves a byte.
- **The press is still the operator's.**

**THE GOAL IS NOT MET.**