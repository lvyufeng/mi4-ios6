# 895 — why the root is still memory: the HFS root row is not in the entry image

**A HOST-SIDE FINDING. No device, no press, no arm.** It reads the linked entry image and names the
reason 894's press took the mockfs (memory) root: **the 874 HFS+ root row is not in `vfstbllist[]`.**

## 1. What 894 left open

894's press cleared the ENOEXEC (the `mi_mdev` fix worked) but mounted `md0` — mockfs from memory, not
the HFS+ volume — and `rootmedia_strategy_served` was **absent** (the strategy was never called). The
open question was *why the HFS row does not take the root*. It is not `rd=md0` and it is not the medium.

## 2. The reading

The linked image's `vfstbllist` (`.data` at `0x805c8f98`) holds exactly **three** rows:

```
devfs_vfsops  ... devfs     (no mountroot)
mockfs_vfsops 0x805c1b1c ... mockfs    mountroot 0x802cca9c  <-- the only one
routefs_vfsops ... routefs  (no mountroot)
```

and `arm-none-eabi-nm out/stage90/xnu_arm_entry.elf` has **no `hfs_vfsops` and no `hfs_mountroot`**
(the only `*mountroot` symbols are `mockfs_mountroot` and the generic `vfs_mountroot`). So
`vfs_mountroot` (`vfs_subr.c:1040`) walks the table, calls the first entry whose `vfc_mountroot` is
non-NULL — **mockfs** — and breaks. The HFS row is simply not there.

## 3. Why it is not there

`STAGE90_XNU_HFS_ROOT_MEDIA` and the HFS root row are **two different switches on two different
builds**:

- **`STAGE90_XNU_HFS_ROOT_MEDIA`** is an *entry* switch. It moves `st_medium_disk_base(0)` to the
  volume blob `g_stage90_root_hfs` (882). It does **not** add any filesystem.
- **`STAGE90_HFS_ROOT`** is the *port* switch that `tools/stage_hfs.sh` bakes into
  `bsd/vfs/vfs_conf.c` (`tools/patch_vfs_conf_hfs_row.py`, guarded `#if STAGE90_HFS_ROOT`) and that
  `tools/build_xnu_arm_kernel.sh` sets when the HFS+ port (37 files) is built. **The row and
  `hfs_mountroot` are compiled into `bsd/vfs/vfs_conf.o` only under `STAGE90_HFS_ROOT`.**

The entry image links the **pooled** `out/xnu_kernel_obj/bsd_vfs_vfs_conf.o`, and that object was
built with `STAGE90_HFS_ROOT` **off** — it defines `vfstbllist` but carries **no HFS symbols**
(`nm` shows only `vfstbllist`). And `out/stage90/xnu_arm_entry-sources.txt` carries **0** HFS
sources. So the entry image has the volume *bytes* (`g_stage90_root_hfs`, 524288 B) and the *medium
switch*, but **nothing that reads them**: the HFS+ port — which 885 built into the *platform* objects
and which 873 proved links — was never linked into the *entry* image.

**The two switches can be set independently, and this is the defect class
`mi4-one-value-two-definitions`:** "the root is HFS+" is stated by a medium switch and delivered by a
port switch, and an arm can set the first while the second is off. Nothing refused.

## 4. The repair (structural, not prose)

`src/entry/build_entry.sh`'s `xnu_entry_459` block now reads the linked image's `vfstbllist` and
states which of three cases holds (`xnu_entry_895`):

- an **HFS row is present** → it must carry `hfs_mountroot` at the mountroot word **and** sit **before
  mockfs**, else `layout_fail` (the two ways a present row is still dead);
- **no HFS row but `STAGE90_XNU_HFS_ROOT_MEDIA=1`** → a **NOTE** that the medium is the volume but
  nothing calls `hfs_mountroot`, so the root is mockfs and the mount clause is unreachable from this
  arm — i.e. 894's reading, refused as a *silent* surprise;
- neither → the baseline.

A NOTE rather than a refusal because the HFS-*medium* arm is legitimate (it tests the strategy and the
fall-through); but the condition is now **structural**, so no arm's prose can claim a mount the linked
image cannot deliver (`mi4-a-claim-in-a-comment-is-not-a-check`).

## 5. What closes the storage clause

For the goal's clause — *XNU mounting storage off the real eMMC* — the entry image needs the HFS+ port
**linked in**, and the row before mockfs. That is a build: set `STAGE90_HFS_ROOT=1`, build the pool
(or at least `vfs_conf.o` + `hfs_mountroot` + its closure) with the port on, and link
`vfs_conf.o` + the 37 HFS objects + the shims into the entry image — plus the entry's own
`layout_fail` clauses for the new object pages (`xnu_entry_459`'s page arithmetic, the entry-group page
move that pins two copies, and the seam site). **It is not a one-line switch.** Until then, no entry
arm can mount HFS+, and the storage clause stays open.

**THE GOAL IS NOT MET:** the OS mounts a memory root; the HFS+ reader is not in the image that mounts.