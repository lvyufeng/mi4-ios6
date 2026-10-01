# 872 — THE ROW WITHOUT TOUCHING THE TREE: `vfs_fsadd` CAN SUPPLY A ROOT

**A host-side reading: no device, no boot, no arm, no park, no switch, no build.** Nothing outside this
document is written. The rung-57/58/59/60 arms stay parked and unpressed. **THE PRESS IS THE OPERATOR'S;
the goal is NOT met.**

In one paragraph: 870 mapped the HFS-root wiring as "three edits + one dropped flag" and left one thing
unasked — **where the `vfstbllist[]` row lives**, since 4570's table is a `static` array inside
`external/`, which is **gitignored**, and the project's technique (tracked `src/supply/*.c` files and `-I`
shadow headers) cannot add an element to someone else's `static` initialiser. This document answers it:
**the row does not need to be static.** `vfs_fsadd` (`kpi_vfs.c:836`) registers a filesystem at run time and
**returns the registered `struct vfstable *` as its handle** (`*handle = vfstable_add(newvfstbl)`, `:1000`,
and `(*handle)->vfc_name` is read straight off it at `:1009`). A `src/supply` file can therefore register
HFS and then **write `handle->vfc_mountroot = hfs_mountroot` itself** — the same slot a static row would have
carried — with no edit to the untracked tree. What looked like a wall is a run-time call.

> **This document corrects its own first draft.** A draft was written claiming `vfs_fsadd` sets
> `vfc_mountroot = NULL` unconditionally and so can never supply a root. That read one line (`:877`,
> `newvfstbl->vfc_mountroot = NULL`) and missed another: **the mapping of `VFS_TBLCANMOUNTROOT` (0x20000) to
> `VFC_VFSCANMOUNTROOT` twelve lines later (`:905`)**, which makes a registered entry a root-walk candidate.
> The draft was deleted rather than committed. It is recorded here because it is the exact shape this
> project's defect memories warn about — a claim built from a neighbouring line, [[mi4-a-claim-in-a-comment-is-not-a-check]]'s
> cousin — and because the corrected finding is *larger*, not smaller.

## 1. The three facts that make it work

1. **The table is static, and the tree is untracked.** `static struct vfstable vfstbllist[]` (`vfs_conf.c:123`),
   reached through the non-`const` global `struct vfstable *vfsconf` (`:165`); `external/` is
   `.gitignore:12`. So a row added by hand to `vfs_conf.c` is neither tracked nor safe from re-checkout.
2. **A registration is a root candidate.** `vfs_fsadd` maps `vfe_flags & VFS_TBLCANMOUNTROOT` to
   `vfc_vfsflags |= VFC_VFSCANMOUNTROOT` (`kpi_vfs.c:905`), and the root walk (`vfs_subr.c:1067-1081`)
   considers every entry with `vfc_mountroot != NULL` **or** `VFC_VFSCANMOUNTROOT`:
   ```c
   if (vfsp->vfc_mountroot == NULL && !ISSET(vfsp->vfc_vfsflags, VFC_VFSCANMOUNTROOT))
           continue;
   ...
   if (vfsp->vfc_mountroot) error = (*vfsp->vfc_mountroot)(mp, rootvp, ctx);
   else                      error = VFS_MOUNT(mp, rootvp, 0, ctx);
   ```
3. **The handle is the vfstable, and it is writable.** `*handle = vfstable_add(newvfstbl)` (`:1000`); the
   caller then owns a `struct vfstable *` (`vfstable_t`, `kernel_types.h:74`) whose fields — including
   `vfc_mountroot` — can be set directly. `vfs_fsadd` only *initialises* it to NULL; nothing prevents the
   caller from filling it.

## 2. The shape, and why it is the right shape

A tracked `src/supply/stage90_hfs_register.c`, called early in `bsd_init` before `vfs_mountroot`, would:

```c
static struct vfs_fsentry vfe = { &hfs_vfsops, 1, hfs_opv_descs,
                                  FT_HFS, "hfs", VFS_TBLLOCALVOL | VFS_TBLCANMOUNTROOT, {0,0} };
vfstable_t handle;
if (vfs_fsadd(&vfe, &handle) == 0)
        handle->vfc_mountroot = hfs_mountroot;   /* the slot a static row would have carried */
```

**And it is the *better* shape for this project, not merely a way around the tree.** `hfs_mount` is the
wrong entry for a root: it begins `copyin(data, (caddr_t)&args, sizeof(args))` (`hfs_vfsops.c:228`), and the
root walk passes `data = 0` to `VFS_MOUNT` — so the generic path (`VFC_VFSCANMOUNTROOT` with a NULL
`vfc_mountroot`) would `copyin` from NULL and fail. `hfs_mountroot` takes no `data`; it goes straight to
`hfs_mountfs`. **The tracked file needs `vfc_mountroot`, and `vfs_fsadd`'s handle gives it that.**

The ordering premises are the same two any of these solutions has, and they are honest premises, not hidden
ones: the registrar must run **before** `vfs_mountroot`, and no other reader must have cached `vfsconf`
beforehand. Both are checkable in the linked image (a build clause in the class of `xnu_entry_864`).

## 3. What this changes

- **870's "three edits" drops the structural asterisk.** The row has a home that is **tracked, reviewable,
  and lost-proof against a tree re-fetch** — a `src/supply/*.c` in the project, not a patch to `external/`.
- **It also drops the compile-time option as a hard requirement.** The `files`-row half of 870 still needs
  the HFS sources to exist somewhere the build compiles them (they are in 2050's tree today); but the *row*
  itself no longer requires `vfs_conf.c` to be modified for the option.
- **The gap to a real root is now wholly: (a) stage the HFS sources and set the `hfs` option, (b) this
  registrar, (c) `CONFIG_PROTECT=0`, and (d) the medium — 867.** Nothing structural remains.

## 4. What this does not change

- **Nothing is built, nothing is armed.** No `src/supply` file, no patch, no option, no park. The parked
  rungs (57–60) are records, not a queue.
- **THE GOAL IS NOT MET.** Even a registered, mountable HFS+ needs the medium behind it; the eMMC driver
  clause (867) is separate and open.

## 5. Safety

Unchanged and untouched: **no device, no `fastboot`, no switch, no press, no build.** This document is prose
in `docs/experiments/`; `make check` is exit 0.