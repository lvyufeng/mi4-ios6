# 943 — the root device follows the tree (2026-10-08)

This rung is the third in the series that started with the D13 rebase ([[mi4-913-ios7-rebase-decision]]):
an asset, a layout, or a **device path** pinned to 4570 that must follow the selected tree. 942 closed the
`struct sysent` ABI; 943 closes the *root-device* check 459, which is the whole of what 903–906 proved.

## The wall

With 942 closed the D13 entry build reached **459's root-device check** and stopped on three 4570-shaped
facts at once:

    FAIL: the pool has 0 of the 3 bsd_miscfs_mockfs_*.o objects - a pool built without the mockfs option

Once that was reached in turn, the check was found to be mockfs-shaped in four places, and mockfs is a
**4570-only filesystem**:

- **The pool census** required exactly **3** `bsd_miscfs_mockfs_*.o` objects.
- **The name list** required `mockfs_vfsops` and `mockfs_mountroot` to be defined in the image.
- **The `vfstbllist` read** found mockfs's row, required `mockfs_mountroot` at the derived
  `vfc_mountroot` offset, and required `routefs` (also 4570-only) to be present with no mountroot.
- **One of the four printf strings** — `"load_init_program: attempting to load"` — is 4570's.

## Finding — D13 roots off the native `hfs`, not off a `mockfs` patch

`external/xnu-hd2-darwin13/xnu/bsd/miscfs/` is `deadfs devfs fifofs specfs union` — **no mockfs, no
routefs**. D13's `vfs_conf.c` `vfstbllist` is instead:

    { &hfs_vfsops, "hfs", ..., hfs_mountroot, ... }     <- first row, wired in the TREE
    { &mfs_vfsops, "mfs", ..., mfs_mountroot, ... }
    { &devfs_vfsops, "devfs", ..., NULL, ... }
    <unassigned> terminators

`vfs_mountroot` (`vfs_subr.c:1068-1075`) takes the **first** row whose `vfc_mountroot` is non-NULL. On
4570 that is `mockfs` — which is why 874/895 *patch* an HFS row into the table and then check it landed
before mockfs. On D13 the hfs row is **the table's own first row**: the tree already ships `hfs_mountroot`
wired in, so `vfs_mountroot` mounts hfs with no patch and no competitor. This is the same end state 874
patched toward, and it is the tree's shape, not a port.

So the fix is a **tree gate**, not a rename ([[mi4-off-option-two-spellings]]): with `is_d13` (the same
`osfmk/sys/types.h` discriminator the whole build uses) 459 reads D13's own facts —

1. the pool has **0** mockfs objects and **1** `bsd_hfs_hfs_vfsops.o` (the object carrying `hfs_vfsops`
   and `hfs_mountroot`, i.e. the root provider);
2. the name list substitutes `hfs_vfsops`/`hfs_mountroot` for `mockfs_vfsops`/`mockfs_mountroot`;
3. the printf string is `"attempting to start init of"` (`kern_exec.c:3710`) rather than
   `"load_init_program: attempting to load"`;
4. the table read asserts hfs is the **first** row of `vfstbllist`, its `vfc_mountroot` word holds
   `hfs_mountroot`, `devfs` has no mountroot at that word, and **no** mockfs/routefs row exists.

The `devfs`/`routefs`-has-no-mountroot assertion 459 already made is kept for D13's `devfs`; the mockfs
and routefs halves become assertions of *absence*, which is the D13 tree's own statement.

4570 is untouched: every new branch is the `if [[ $is_d13 -eq 1 ]]` arm, and the old body is the `else`
— textually identical to what 942 left. The `if [[ $is_d13 -eq 0 ]]` guard the first draft put around the
874/895 note was removed as redundant (it sat inside the 4570-only `else`); the diff now adds **100**
lines and changes **7** (the `for sym in …` header, the two `say` lines, the string list header, and the
four closing braces), with the 4570 `else` body appearing only as diff context.

## State after this rung

459 passes on D13. The record line reads

    xnu_entry_943: the pool is the D13 STAGE90_XNU kernel (705 objects), no object in it calls
    _consume_printf_args, no mockfs object (the tree ships none), and hfs_vfsops (the tree's root
    provider) is in it
    xnu_entry_943: vfstbllist's first row is hfs (ops at 0x805f4d70, mountroot 0x801e0fc0 at word +8);
    devfs has no mountroot at that word and the tree ships no mockfs or routefs - so vfs_mountroot
    mounts the native hfs, the tree's root provider

and the build advances **past the root-device check to 481's timer-table check**, which is the next
wall: D13's `cpu_data` offsets and its `arm_init` call shape differ from 4570's, so the 481 clause (the
`PE_init_platform -> cpu_timebase_init -> fiq_context_init` triple and the `tbd_ops` it registers) is
4570-shaped. That is rung 944.

## Provenance

`src/entry/build_entry.sh` (`verify_root_device`). Host-side, reversible, **no press**.

- The D13 toolchain had to be brought to a consistent state first: the kernel pool rebuilt as
  `STAGE90_XNU` (705 objects), and the platform block rebuilt with the arm's **full** switch set
  (`-DSTAGE90_XNU_HFS_ROOT_MEDIA=1 -DSTAGE90_XNU_EMMC_STRATEGY=1 -DSTAGE90_XNU_ROOT_FROM_CARD=1
  -DSTAGE90_XNU_HDD_WRITE=0 -DSTAGE90_XNU_CARD_TOTAL=1`) — the five `nm`-marker checks at
  `build_entry.sh:27697-27865` each read one arm-marker off `stage90_root_media.o`.
- D13 entry build: `/tmp/d13_entry_build30.log` (459 passes; stops at 481's timer-table check).
- `make check` exits 0.
- D13 linked image facts (read here, not transcribed): `hfs_vfsops`=T/D `0x805f4d70`, `hfs_mountroot`=T
  `0x801e0fc0`, `vfstbllist` at `0x805fff28`, first row `{0x805f4d70, "hfs\0\0\0"}`; `mockfs_*`/`mfs_*`/
  `routefs_vfsops` absent from the image.