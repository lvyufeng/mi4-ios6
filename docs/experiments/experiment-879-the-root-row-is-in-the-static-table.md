# 879 — THE ROOT ROW IS IN THE STATIC TABLE, BEFORE MOCKFS, AND INERT WHEN THE PORT IS OFF

**A host-side measurement: no device, no boot, no arm, no park, no switch turned on in the live artifact.**
The rung-57/58/59/60 arms stay parked and unpressed. **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 874 read `vfstable_add` and concluded that the row a root needs must be a **static**
`vfstbllist[]` entry **before mockfs** — a registered row (`vfs_fsadd`) appends after mockfs and is never
tried. This step builds that row: a tracked patch (`tools/patch_vfs_conf_hfs_row.py`, applied by
`tools/stage_hfs.sh` beside the port's other edits) puts 2050's HFS row into the untracked
`bsd/vfs/vfs_conf.c`, guarded by `STAGE90_HFS_ROOT`, which the build defines **exactly when the port is on**.
Measured both ways through the real pipeline: with the port **off** the manifest is byte-identical to the
committed one and the row is **inert** (no `hfs` reference in `vfs_conf.o`); with it **on** `vfs_conf.c`
compiles and its object carries `U hfs_mountroot` / `U hfs_vfsops` — the row is in the table the root walk
reads. A new section of `tools/check_hfs_staged.sh` refuses the row being absent, mis-placed after mockfs, or
duplicated.

## 1. What 874 left, and what this step supplies

874's correction was exact and one item: *"an HFS row **in the static table before mockfs** — a tracked patch
to `vfs_conf.c`, not a `vfs_fsadd` registration."* It named the mechanism; it did not make the edit. This step
makes it, with the same discipline the port's other tree edits use: **one definition, in a tracked file,
re-appliable**, because `external/` is a re-provisionable checkout and a hand edit there is lost
([[mi4-hfs-staged]]).

## 2. The patch, and the three details that are easy to get wrong

`tools/patch_vfs_conf_hfs_row.py` makes three inserts into `vfs_conf.c`, anchored on text that is unique in
the file (and it refuses, exit 2, if any anchor is not unique — the tree moved under it):

1. **The `extern` declarations**, after the MOCKFS extern block. The anchor is that block's *full* text,
   because `#endif /* MOCKFS */` alone appears **four** times (measured), and a shorter anchor is ambiguous.
2. **`FT_HFS = 17`**, the type number, in `enum fs_type_num`. 2050's own value; 4570's highest is
   `FT_MOCKFS = 0x6D6F636B`, so 17 is free. **`FT_MOCKFS` is the LAST enumerator and carries no trailing
   comma** — so the replacement adds the comma before `FT_HFS`. (Missing it is a compile error, and it was
   the first thing measured here: `vfs_conf.c:124:25: error: missing ',' between enumerators`.)
3. **The row itself**, before the mockfs block. 2050's row
   (`xnu-2050.18.24/bsd/vfs/vfs_conf.c:119`) is copied field-for-field, then adapted:
   - 4570's `struct vfstable` has **one extra trailing field** `vfc_sysctl` (`mount_internal.h:316`), so the
     initialiser gains a trailing `, NULL` — a positional initialiser against the bigger struct would
     zero-fill silently, which is right only by coincidence.
   - **`VFS_THREAD_SAFE_FLAG` is DROPPED**: it is a 2050-local macro for a flag bit (`VFC_VFSTHREADSAFE`)
     4570's flag set does not carry, and the instruction is not to invent a bit. Every remaining flag name
     (`MNT_LOCAL`, `MNT_DOVOLFS`, `VFC_VFSLOCALARGS`, `VFC_VFSREADDIR_EXTENDED`, `VFC_VFS64BITREADY`,
     `VFC_VFSVNOP_PAGEINV2`, `VFC_VFSVNOP_PAGEOUTV2`) is present in 4570 at the same value (measured).

The whole block is wrapped `#if STAGE90_HFS_ROOT … #endif`, so when the guard is not defined the file is
compiled **exactly as 4570 shipped it**.

**A defect this step found and cleaned, in the untracked tree.** The first attempt at the row had been made by
hand in a prior session and left a **stray `FT_HFS` enumerator** (line 118, no comma, no guard, no marker),
which the patch then did not remove — so the patched file declared `FT_HFS` twice. That is the same "hand edit
to the untracked tree" the port's whole tracked-patch discipline exists to prevent, and it is now a check:
section 4 refuses a file whose `FT_HFS` count is not exactly 1.

## 3. The measurement, both ways, through the real pipeline

The pool is `STAGE90_XNU` (its `.local` fragment), which is the configuration the loop actually uses.

```
OFF  (STAGE90_XNU_HFS unset):  vfs 21 files tried / 21 compile / 0 fail
                               manifest: 733 lines, BYTE-IDENTICAL to the committed out/xnu_arm_manifest.txt
                               vfs_conf.o: no `hfs` reference            -> the row is INERT
ON   (STAGE90_XNU_HFS=1):      vfs 22 files tried / 22 compile / 0 fail
                               vfs_conf.o: U hfs_mountroot, U hfs_vfsops  -> the row is IN THE TABLE
```

The off/on manifest delta is exactly the port's own footprint: the **37** HFS paths from `src/supply/hfs_files.txt`
and `bsd/vfs/vfs_journal.c`, and nothing else.

## 4. The check (the property is structural, not a comment)

`tools/check_hfs_staged.sh` gains **section 4**, since the row is a third kind of "tracked edit to an untracked
tree" and drifts the same way (a re-provision restores the row-less file). It refuses, with the reason:

- the file has no `STAGE90_HFS_ROOT` guard → the row is **absent** (run the stager);
- no `patch_vfs_conf_hfs_row.py` marker → it may have been **hand-edited**, not patched;
- the HFS row's line number is **after the mockfs row's** → it would never be tried as root;
- `FT_HFS` is declared other than exactly once → the duplicate-enumerator defect above.

Four cases were exercised against copies of the tree (staged → pass; row removed → refuse; row moved after
mockfs → refuse; `FT_HFS` doubled → refuse). A first version of this check anchored "before mockfs" on
`#if MOCKFS`, which matches the **extern** block (above the table) rather than the table block — it reported a
correct row as mis-placed, and is anchored on the mockfs **table row** now.

## 5. The honest bound, restated exactly

**The row is in the table and the port still has no medium.**
- The row makes `vfs_mountroot` **able to try** HFS as root. It does not make HFS mount: `hfs_mountroot` calls
  into the HFS sources, and those read a device through the buffer cache — and the eMMC driver that moves a
  byte is **still owed (867)**. With the port on and no medium, the root walk reaches the HFS row and fails
  there, which is the next thing a build would show.
- The cprotect layer 878 restored is **declarations**; the cprotect **engine** (`cp_register_wraps` has no
  caller) is still owed.
- The port is still **OFF by default** (`STAGE90_XNU_HFS=0`). 874's §6(c) said `CONFIG_PROTECT=0`; 878
  measured that this is wrong — `CONFIG_PROTECT` is a `struct bufattr` layout axis and must stay consistent
  across the kernel — so (c) is withdrawn.
- **THE GOAL IS NOT MET. The medium (867) is the item that stands between this and a mounted HFS root.**