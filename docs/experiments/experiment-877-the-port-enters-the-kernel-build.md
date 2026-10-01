# 877 — THE HFS+ PORT ENTERS THE KERNEL BUILD, AND THE ONE THING THAT STOPS IT

**A host-side measurement plus the wiring: sources reach the manifest, the force header reaches the
compile line, the shims reach the link — and each is verified through the real pipeline in both
positions. No arm, no park, no switch turned on, no device.** The rung-57/58/59/60 arms stay parked and
unpressed. **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 875 staged 2050's HFS+ into 4570's tree and made the port's additions tracked; 876
compiled the staged tree in place and wrote the plan. This step **executes the plan** — the 37 sources
join `out/xnu_arm_manifest.txt` from the tracked `src/supply/hfs_files.txt`, the one definition
(`src/shims/hfs/hfs_port_force.h`) is force-included for the HFS files only, and
`src/supply/stage90_hfs_shims.c` is compiled by the platform block and added to the link. Measured
through the real pipeline (`--dir hfs`, `MANIFEST`/`XNU_KERNEL_OBJ_OUT` redirected so no live artifact
is touched): **32 of the 36 `bsd/hfs/` `.c` files compile, and 4 do not** — the four are exactly the
cprotect-API gap 865 named. The port is **wired but not turned on** (`STAGE90_XNU_HFS`, default 0),
because the one thing that closes that gap turns out to be a *struct-layout* choice, not a per-file
option. The default path leaves the manifest byte-identical to the committed 733.

## 1. The three halves, and where each one lives

- **The file list.** `src/supply/hfs_files.txt` (37 paths) is handed to
  `tools/xnu_config/list_sources.py` through a new `--extra` option, from
  `tools/build_xnu_arm_kernel.sh`'s manifest step. `--extra` **refuses** a named-but-absent path and
  writes no manifest: `external/` is a re-provisionable checkout, and a manifest that silently dropped
  37 files would build a kernel claiming HFS and containing none — the project's oldest defect wearing a
  green build. Measured on the list with the staged tree: 770 paths (733 base + 37), exactly the 37
  added by diff.
- **The compile line.** `SRC_COMPONENT == bsd` already gives the HFS files `-include sys/types.h`, the
  BSD define set and the BSD import roots (876 §2b). The one addition is
  `-include src/shims/hfs/hfs_port_force.h`, **for `bsd/hfs/*` and `bsd/vfs/vfs_journal.c` only** — the
  force header is the port's definition and no other file should see it.
- **The link.** `src/supply/stage90_hfs_shims.c` joins `PLATFORM_BSD_SOURCES` in the platform block
  (compiled with the BSD define set, as the pthread and crypto tables are) and its object is added to
  `LINK_OBJS` in `src/entry/build_entry.sh`.

## 2. The measurement — the wiring is correct, the port is 32/36

`STAGE90_XNU_HFS=1 … ./tools/build_xnu_arm_kernel.sh --dir hfs`:

```
C files tried:        36
compile:              32
fail:                 4
```

The four that fail are `hfs_cprotect.c`, `hfs_readwrite.c`, `hfs_vfsops.c`, `hfs_vnops.c`, and every
error is in the cprotect API family: `CP_WRITE_ACCESS`, `CP_PREFETCH`, `CP_PREV_MAJOR_VERS`,
`CP_RELOCATION_INFLIGHT`, `struct cprotect`, `struct cp_root_xattr`. **This is exactly what 865
predicted**, and it is the difference between this run (`CONFIG_PROTECT=1`, the configuration's value)
and 876's 37/37 in-place measurement (which passed no `CONFIG_PROTECT`, so it was 0). The platform block
also compiles `stage90_hfs_shims.o`, and `nm` shows a `T` definition for each of the ten symbols 871
named.

## 3. Why it cannot be closed by a per-file switch — the finding that made this default off

The tempting shortcut is `-DCONFIG_PROTECT=0` for these four files. It is refused because the option is
**not a filesystem option, it is a struct-layout axis**:
`bsd/sys/buf_internal.h:82-92` gates two fields *inside `struct bufattr`* (`ba_cpx`, `ba_cp_file_off`)
on `CONFIG_PROTECT`. The other 704 files are built with the configuration's `CONFIG_PROTECT=1`, so
compiling four files with `=0` would give them a **second definition** of `struct bufattr` — the defect
class this project pays for most often ([[mi4-one-value-two-definitions]]), and it would not fail
loudly: it would read and write the wrong offset.

So the port does not reach a kernel until its cprotect work lands, and `STAGE90_XNU_HFS` is **0 by
default**. The build is correct in both positions and 0 is the one that cannot lie about what the tree
can do. Confirmed: with the switch off, `out/xnu_arm_manifest.txt` is **byte-identical** to the
committed manifest, and nothing in the platform block or the link changes.

## 4. The honest bound

**The port is wired and not turned on; nothing is built into the live kernel.** Two of the three
outstanding items from 876 remain: the **root row** (874 — an HFS entry in the *static* `vfstbllist[]`
before mockfs, not a `vfs_fsadd`), and the **medium** (867 — an eMMC driver that moves a byte). A third
is now measured: the **cprotect API** the four files read, which is a real port of 2050's cprotect layer
onto 4570's restructured one, not a flag. **THE GOAL IS NOT MET.**