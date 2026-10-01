# 874 — THE CARRIER 872 CHOSE LANDS *AFTER* MOCKFS

**A host-side reading: no device, no boot, no arm, no park, no switch, no build.** The rung-57/58/59/60 arms
stay parked and unpressed. **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 872 answered *where the row lives* with `vfs_fsadd` — a tracked `src/supply` file registers
HFS at run time and writes `handle->vfc_mountroot = hfs_mountroot` on the returned `struct vfstable *`. That
answer is correct about *how a row is made*. It does not answer *where the row lands*, and the answer to that
is the opposite of what a root needs: **`vfstable_add` fills the first empty slot of the static
`vfstbllist[]`, mockfs holds slot 1, and the empty slots follow it — so a registered row is appended AFTER the
one row that can mount root, and the root walk reaches mockfs first and never tries HFS.** The row is
registered and never used as a root.

## 1. The mechanism: `vfstable_add` appends

`vfstable_add` (`external/xnu-4570.1.46/bsd/vfs/vfs_init.c:578`) finds its slot by scanning the static array for
the first entry with no ops:

```c
for (slot = 0; slot < maxvfsslots; slot++)
        if (vfsconf[slot].vfc_vfsops == NULL)
                break;
...
bcopy(nvfsp, slotp, sizeof(struct vfstable));
if (slot != 0) {                                    /* chain it after the last live row */
        slotp->vfc_next = vfsconf[slot - 1].vfc_next;
        vfsconf[slot - 1].vfc_next = slotp;
}
```

So a registered filesystem goes to the **first empty slot**, which is after every row the configuration
compiled in. It is not inserted at a position and not placed before mockfs.

## 2. Where that slot is, in this configuration

`STAGE90_XNU` selects `MOCKFS` **and `ROUTEFS` and `DEVFS`** (measured: `make_defines.sh STAGE90_XNU` gives
`-DDEVFS=1 -DMOCKFS=1 -DROUTEFS=1`). So the live static rows (`vfs_conf.c:123`, order preserved) are:

| slot | row | `vfc_mountroot` | reachable as root? |
| --- | --- | --- | --- |
| 0 | `devfs` | NULL | no |
| 1 | `mockfs` | **`mockfs_mountroot`** | **yes — and first** |
| 2 | `routefs` | NULL | no |
| 3 | *(empty)* | — | ← `vfstable_add` takes this |
| 4 | *(empty)* | — | |

`mockfs` is at slot **1**, `routefs` — which the configuration compiles in and which 870's reading did not
name — is at 2, and the empty slots are 3 and 4. **A `vfs_fsadd` row lands at slot 3, after both.**

## 3. The walk reaches mockfs first

`vfs_mountroot` (`bsd/vfs/vfs_subr.c:1067`) walks the chain from `vfsconf` and takes the **first** row that
mounts without error:

```c
for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) {
        if (vfsp->vfc_mountroot == NULL && !ISSET(vfsp->vfc_vfsflags, VFC_VFSCANMOUNTROOT))
                continue;
        ...
        if (!error) { ...; break; }        /* 1058's `while (TRUE)` re-reads the device only on error */
}
```

With the HFS row after mockfs, the order tried is devfs(skip) → **mockfs(succeeds → break)**. The list is
iterated to the END only when an earlier entry returns an error, and `mockfs_mountroot` does not: it claims the
device, and its own sibling `vfs_fsadd`/`vfstable_add` comment says why the last standard entry is the last —
mockfs *"should always be the last standard entry (and thus the last FS we attempt mountroot with)"*. A row
appended past it is never attempted.

## 4. What this does to 872, and what it does not

- **872's mechanism is real and 870's row is still copyable.** `vfs_fsadd` does return the registered
  `struct vfstable *`, and `handle->vfc_mountroot = hfs_mountroot` does fill the mountroot slot.
- **What is wrong is the position.** 872 proved a row *can* be made outside the untracked tree; it did not
  check that the row that comes out is *before* mockfs, and it is not.
- **So the carrier 872 chose does not give a root.** Enabling HFS via a `vfs_fsadd` registration produces a
  registered filesystem the root walk never reaches. **The row has to be in the static table, before mockfs** —
  which means a tracked patch to the untracked `vfs_conf.c` (or a shadow-header/patch mechanism of the same
  shape this project already uses for the boot image), because 4570's table is a `static` array with no
  ordering hook.

The correction is 870's, restated exactly: *the row must be an HFS row **before** mockfs* (870 §"an HFS row
before mockfs"). 872 answered a different question — where the row's *text* lives — with a carrier that
silently violates 870's own placement rule.

## 5. The honest bound

**Nothing built, nothing armed.** No `vfs_conf.c` edit, no `files` row, no option, no registrar, no park. This
is a reading of `vfstable_add`'s slot rule and of this configuration's own row set; it needs no compiler and no
device. **The medium is still owed (867), and the press is still the operator's.** **THE GOAL IS NOT MET.**

## 6. What remains, corrected

For a real HFS root: (a) stage the HFS sources + set the `hfs` option, (b) an HFS row **in the static table
before mockfs** — a tracked patch to `vfs_conf.c`, not a `vfs_fsadd` registration — carrying
`vfc_mountroot = hfs_mountroot`, (c) `CONFIG_PROTECT=0`, and (d) the medium. (b) is the item 872 got wrong in
position; the other three are unchanged.