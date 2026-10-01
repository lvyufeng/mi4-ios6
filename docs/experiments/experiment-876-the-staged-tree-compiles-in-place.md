# 876 — THE STAGED TREE COMPILES IN PLACE, AND WHAT WIRING IT COSTS

**A host-side measurement plus a wiring plan: no build into the kernel, no arm, no park, no switch, no
device.** The rung-57/58/59/60 arms stay parked and unpressed. **THE PRESS IS THE OPERATOR'S; the goal is
NOT met.**

In one paragraph: 875 staged 2050's HFS+ into 4570's tree and made the port's additions tracked, but the
"38/38" it re-measured was still the *probe's* number — the probe copies the sources into a `/tmp` sandbox
and compiles them there. This step compiles the **staged tree, in place**, under the real build's own
apple-target invocation and include set: **37/37 `.c` files and the shims file compile**. So the staging is
correct in the actual tree the kernel builds from, not only in the sandbox. The rest of this document is the
measured plan for the one remaining source-side step: putting HFS into the kernel build.

## 1. The measurement

The 36 HFS `.c` files, the 9 `hfscommon/` files, `vfs_journal.c`, and `src/supply/stage90_hfs_shims.c`,
compiled with:

```
clang --target=armv7-apple-darwin -mabi=aapcs -mcpu=cortex-a15 -marm -mfpu=neon-vfpv4 \
      -mfloat-abi=softfp -O2 -ffreestanding -fno-builtin -fno-common -fno-pic \
      <the build's FORCE_INCLUDES, INCLUDES, config defines, and the bsd-component defines> \
      -include src/shims/hfs/hfs_port_force.h
```

Result: **37/37 in place, 0 fail**, and the shims file compiles. The `<hfs/...>` includes resolve because the
staged directory *is* `bsd/hfs/` — no include-path trick is needed in the real tree (the probe needed one only
because it copied the directory out of place). `hfs_macos_defs.h` is the patched copy the stager wrote, and
the force header supplies the port's options, so no `conf/files` or `MASTER` row is owed.

This is the same number the probe reports, from the other direction: the probe proves the *sources* are
portable, and this proves the *staged tree* is what the build would compile.

## 2. What wiring it into the kernel costs — measured, not estimated

The kernel build (`tools/build_xnu_arm_kernel.sh`) compiles `out/xnu_arm_manifest.txt`, which
`tools/xnu_config/list_sources.py` generates from 4570's `*/conf/files`. HFS is not in those files, so three
things are owed, and each has a measured shape:

**(a) The file list.** `list_sources.py` reads only `conf/files`; the port's rows are not there, by 875's
choice (the port's additions are tracked files, not rows of an untracked one). So the manifest gains the HFS
sources **from a tracked list**: `src/supply/hfs_files.txt`, **37 paths** (the 36 `.c` under `bsd/hfs/` —
including `hfs_quota.c`, which 2050 gates on `quota` but which compiles here and whose callers expect it —
plus `vfs_journal.c`). It is exactly the set this document measured at 37/37 in place, so the list and the
measurement are the same set and cannot disagree.

**(b) The compile invocation.** The HFS files are BSD-component files, so `build_xnu_arm_kernel.sh`'s
`SRC_COMPONENT == bsd` branch already gives them `-include sys/types.h`, the BSD defines, and the `bsd`/`osfmk`
include order — the same branch this step's in-place measurement used. The one addition is
`-include src/shims/hfs/hfs_port_force.h`, for the HFS files only (it is the ONE definition of the port's
additions, and no other file should see it).

**(c) The ten shims.** `src/supply/stage90_hfs_shims.c` must be added to the link, in the same block where
`stage90_pthread_functions.o` and `stage90_pseudo_inits.o` are added to `LINK_OBJS`
(`src/entry/build_entry.sh`).

## 3. The two things that are NOT source, and are still owed

- **The root row (874).** An HFS entry must be **in the static `vfstbllist[]`, before mockfs**
  (`bsd/vfs/vfs_conf.c`). It cannot be a `vfs_fsadd` registration: 874 measured that those land *after*
  mockfs and are never tried. 4570's table is inside the untracked tree, so this row is a tracked patch to
  `vfs_conf.c` — the same shape as `tools/hfs_patch_macos_defs.py`, applied by `stage_hfs.sh`.
- **The medium (867).** Even a compiled, wired-in, root-roster HFS+ reaches no eMMC driver that moves a byte.

## 4. The honest bound

**Nothing is built into the kernel.** The staged sources compile in place and are not in
`out/xnu_arm_manifest.txt`; wiring them is section 2, and it is the next step, not this one. A kernel built
with HFS but with no HFS volume to answer, and with mockfs still last, still enters the OS — which is why
this is safe to attempt when the wiring lands: the risk is bounded by mockfs remaining the fallback, and by
the eMMC driver still not moving data. **THE GOAL IS NOT MET.**