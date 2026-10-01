# 870 — THE WIRING: WHAT PUTTING HFS INTO `vfstbllist[]` ACTUALLY COSTS

**A host-side reading: no device, no boot, no arm, no park, no switch, no build.** Nothing outside this
document is written. The rung-57/58/59/60 arms stay parked and unpressed. **THE PRESS IS THE OPERATOR'S;
the goal is NOT met.**

In one paragraph: 868 and 869 closed the HFS+ port's **compile and link** halves (37/37 files, a ten-symbol
gap of one-line shims). What neither measured is the **wiring** — how a filesystem becomes the thing
`vfs_mountroot` will mount. This document reads it out of 4570's `vfs_conf.c` and 2050's own HFS row. The
answer is **three edits and one dropped flag**, and — the finding that matters — **2050's HFS row is
copyable field-for-field into 4570's table**, because the two `struct vfstable`s differ by exactly one
trailing field. **The wiring is not a wall; it is an entry.**

## 1. The table, and the one property that decides everything

`bsd/vfs/vfs_conf.c` holds `vfstbllist[]` — a static array of `struct vfstable`, one row per filesystem. The
root is chosen *from the row*, not from the `vfsops`: `vfs_mountroot` calls the row's own
`vfc_mountroot(mount_t, vnode_t, vfs_context_t)` slot. The tree's own comment says it (verbatim, 4570
`vfs_conf.c:145`):

> `/* If we are configured for it, mockfs should always be the last standard entry (and thus the last FS we
> attempt mountroot with) */`

So the root is **the first row with a non-NULL `vfc_mountroot`** that answers. Today that is mockfs alone —
the configuration is `[ RELEASE mockfs development ]` (`tools/xnu_config/boot/STAGE90_XNU.local`), and, as
that file's own header states, *"mockfs is the only filesystem in the tree whose `vfc_mountroot` is
non-NULL."* To boot a real HFS+ volume the root has to be **HFS**, which means an HFS row **before** mockfs.

## 2. The three edits, and the flags they need

### 2.1 An `FT_HFS`

4570's enum is `FT_NFS=2, FT_DEVFS=19, FT_SYNTHFS=20, FT_ROUTEFS=21, FT_NULLFS=22, FT_MOCKFS=0x6D6F636B`.
**2050's HFS uses typenum `17`** and nothing in 4570 claims it, so the port takes 2050's own number. (This
is the same free-number discipline as `M_JNL_JNL`/`M_JNL_TR` in 869 — take the value the source already
used, having first proved it free.)

### 2.2 The row — and it is copyable

2050's own row (`bsd/vfs/vfs_conf.c:119`):

```c
{ &hfs_vfsops, "hfs", 17, 0, (MNT_LOCAL | MNT_DOVOLFS), hfs_mountroot, NULL, 0, 0,
  VFC_VFSLOCALARGS | VFC_VFSREADDIR_EXTENDED | VFS_THREAD_SAFE_FLAG | VFC_VFS64BITREADY |
  VFC_VFSVNOP_PAGEOUTV2 | VFC_VFSVNOP_PAGEINV2, NULL, 0},
```

Every one of those names exists in 4570 — **except `VFS_THREAD_SAFE_FLAG`**:

| flag | 4570 | note |
| --- | --- | --- |
| `VFC_VFSLOCALARGS` | `0x002` | `mount_internal.h:320` |
| `VFC_VFSREADDIR_EXTENDED` | `0x080` | `:325` |
| `VFC_VFS64BITREADY` | `0x100` | `:326` |
| `VFC_VFSVNOP_PAGEINV2` | `0x2000` | `:328` |
| `VFC_VFSVNOP_PAGEOUTV2` | `0x4000` | `:329` |
| `MNT_LOCAL`, `MNT_DOVOLFS` | present | identical in both trees — `MNT_DOVOLFS` is `0x00008000` in 2050 and 4570 alike, with the same *"deprecated flag in Mac OS X 10.5"* comment (`mount.h:308` / `:313`) |
| **`VFS_THREAD_SAFE_FLAG`** | **absent** | 2050 defines it **locally in `vfs_conf.c:76`** as `VFC_VFSTHREADSAFE` (`0x200`) *"only under `CONFIG_VFS_FUNNEL`"*, and as **`0`** otherwise (`:78`). Never a public flag. |

**And 4570 does not use bit `0x200` at all** (its nearest are `0x100` and `0x1000`), while 2050 defines
`VFC_VFSTHREADSAFE 0x200` and 4570 has no equivalent. So the honest port keeps 2050's own two-spelling
macro — `#define VFS_THREAD_SAFE_FLAG VFC_VFSTHREADSAFE` under 4570's funnel config, else `0` — rather than
inventing a bit.

### 2.3 `hfs_mountroot`

It exists and is the right shape: `int hfs_mountroot(mount_t, vnode_t, vfs_context_t)` (`hfs.h:719`,
`hfs_vfsops.c:178`). It is **not** a member of `struct vfsops` — 2050's `hfs_vfsops` has 13 slots and no
mountroot — because the table row carries it. So no `vfsops` change is needed.

## 3. The struct drift — one trailing field, and it is the whole risk

`struct vfstable` differs between the trees by **exactly one field**, added last in 4570:

```c
/* 4570 mount_internal.h:316, absent in 2050's (which ends at vfc_descsize) */
struct sysctl_oid *vfc_sysctl;	/* dynamically registered sysctl node */
```

So 2050's 12-field row becomes a 13-field row in 4570 by appending **`, NULL`**. **This is the one place the
copy can go silently wrong**: because a `struct` initialiser is positional, a 12-field row compiled against
a 13-field struct does not error — it leaves `vfc_sysctl` zero-initialised, which is *correct here* but is a
coincidence, not a design. The port should append the explicit `, NULL` so the row's arity is checkable, and
a build clause can require the row to have 13 fields.

## 4. What is left, priced

The wiring, complete:

1. **Option** `hfs` — 2050's `bsd/conf/files:131` (`OPTIONS/hfs optional hfs`); the configuration gains it
   (`STAGE90_XNU = [ RELEASE mockfs hfs development ]`).
2. **`files` rows** — 2050's `bsd/conf/files:459+` list ~34 `.c` files under `optional hfs`, plus the
   `hfscommon/` subtree. These move to 4570's `bsd/conf/files`.
3. **`FT_HFS = 17`** + the 13-field **row** before mockfs.
4. **The ten link shims** of 869 (`fslog_fs_corrupt`, three `IOBSD*`, five renames, the no-op
   `proc_apply_thread_selfdiskacc`).
5. **The driver** (experiment 867) — still separate, still gated on the unwashed selection (rungs 57–60).

And then the honest caveat that makes this document a *reading* and not a promise: **the configuration
enables `hfs`, it does not make it the root.** With both `hfs` and `mockfs` present, whether the boot reaches
HFS's row — and whether a real HFS+ volume is there to answer it — is a **device fact**, not a host one. The
mockfs root the OS enters on today is synthetic; HFS would need the eMMC behind it, which is 867's clause.

## 5. What this changes, and what it does not

- **NOTHING IS BUILT, NOTHING IS ARMED.** No `vfs_conf.c` edit, no `files` row, no option, no park. The
  parked arms (57/58/59/60) are records, not a queue, and are not touched.
- **The wiring clause is closed as a map**: three edits + one dropped flag + a one-field struct delta,
  with 2050's own row copyable field-for-field.
- **THE GOAL IS NOT MET.** The OS enters on a synthetic root; no HFS+ volume is mounted; the medium has
  never been read by the mounting OS; the eMMC driver clause is still open.

## 6. Safety

Unchanged and untouched: **no device, no `fastboot`, no switch, no press, no build.** This document is prose
in `docs/experiments/`; `make check` is exit 0.