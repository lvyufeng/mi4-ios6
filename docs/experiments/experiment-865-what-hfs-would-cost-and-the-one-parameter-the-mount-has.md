# 865 — WHAT HFS+ WOULD COST, AND THE ONE PARAMETER THE MOUNT ACTUALLY HAS

**A host-side measurement: no device, no boot, no arm, no park, no switch.** Nothing in the repository is
written except this document, its README row, and one new tool (`tools/hfs_port_probe.sh`, which the
document is about). The live `out/` is untouched; the rung-57/58/59 arms stay parked and unpressed.

In one paragraph: the mount track has been built for three rungs on the assumption that the medium is the
thing to get right, and **the medium is not what decides whether the OS comes up — one argument of one
ioctl is.** `mockfs_mountroot` asks the root device `DKIOCGETMEMDEVINFO` and, if the device answers,
maps its file node **straight onto raw memory** (`pager_map_to_phys_contiguous`), handing the kernel a
`/sbin/launchd` that is not on any filesystem. Everything the ladder has staged is bypassed by that
answer. The repair is a one-word change to what `st_media_memdev_info` reports, and the reason it is not
made tonight is that **the value it reports holds up the entire exec** — `mi_phys = 0` is the field that
makes mockfs take the memory-backed path at all. Section 4 states the experiment and the two live
failure modes. Sections 1–3 are the findings that produced it, including a 36-file compile of the
HFS+ port the mount would need.

## 1. The root path, read again — and the merge that hid it

Every experiment from 861 on has described the root like this:

> the root is mockfs over an 8 KB RAM disk and **no ext4/f2fs/HFS driver is linked**

The first half is right. The second half is right but is not the load-bearing half, and the two
together were made to carry a claim they do not support. The path, with the calls:

```
bsd_init (bsd/kern/bsd_init.c:940 setconf)
  -> IOFindBSDRoot("", ...)                        the /chosen/memory-map RAMDisk exit
       -> mdevadd(-1, ...) -> __wrap_mdevlookup -> mdevlookup -> dev_t      rootdev = md0
  -> vfs_mountroot()                               (bsd/vfs/vfs_subr.c:1040)
       -> bdevvp(rootdev, &rootvp)                 VBLK vnode, spec_vnodeop_p
       -> vfs_init_io_attributes(mp, rootvp)       VNOP_IOCTL on the DEVICE vnode
       -> walk vfstbllist: the FIRST entry with a non-NULL vfc_mountroot
            -> mockfs_mountroot(mp, rvp, ctx)      (bsd/miscfs/mockfs/mockfs_vfsops.c:101)
                 -> VNOP_IOCTL(rvp, DKIOCGETMEMDEVINFO, &mi, ...)
                      mi answers -> mockfs_memory_backed = mi_mdev, mockfs_memdev_base = mi_base
                 -> mockfs builds its one file node (/sbin/launchd)
                      -> mockfs_fsnode.c:333-344
                         pager_map_to_phys_contiguous(ubc_getobject(vp,0), 0,
                                                      mi_base << PAGE_SHIFT, fsnp->size)
```

**`mi_base << PAGE_SHIFT` is the file's offset 0.** Not "the bytes at the wrong offset" — the file's
byte zero *is* the raw memory at `mi_base << 12`. There is no filesystem in the path at all. That is what
experiment 468 designed and what makes process 1 exec-able: `g_stage90_ramdisk` is `/sbin/launchd` byte
for byte, `exec_mach_imgact` claims it by `magic == MH_MAGIC`, and nothing ever has to read a disk.

And on 862 this became a **merge with a neighbour**: `st_media_memdev_info` was written to copy
`mdevadd`'s `phys = 0` argument, and its own note reads

> `mi_phys=0` (copied from `mdevadd`'s `phys=0`; mockfs **skips** the memory-backed path when `mi_phys`
> is true)

Every clause that keeps the exec alive is a clause that keeps mockfs on the memory-backed path — while
the rung directly above it prepares a device whose whole content is one filesystem sector. **The medium
and the root are one field apart and the field is set to make the medium unreachable.**

## 2. Why nothing moves through `st_medium_virt`

This is a reading of the source and of 862's own notes, not a measurement, and it is stated as such.

`mockfs_mountroot` sets two globals from the ioctl's answer and `mockfs_fsnode.c` branches on them: the
`pager_map_to_phys_contiguous` call at `:333-344` is inside the memory-backed branch, and the alternative
is the vnode's own pagein path, which for a file with no pager falls through to the **device's
`strategy`**. So there are two reachable branches and the device's answer selects between them — 862 says
the same thing in the other direction, in the note quoted above (`mi_phys` true ⇒ mockfs **skips** the
memory-backed path).

The RAM disk has always selected the memory-backed one. That is not a defect: it is the whole of
experiment 468, and it is why the exec works. What follows from it is only this:

**the answer to "why does nothing move through `st_medium_virt`?" is not that the strategy is wrong —
864 built it correctly — but that nothing reaches it**, because a memory-backed file node's pages come
from `mi_base << PAGE_SHIFT` and never from the device. No count of `_rootmedia_served` is owed by the
current arms, and a zero there is not evidence about the strategy.

A control that would turn the paragraph above from a reading into a measurement is cheap and is not in
this document: force `mi_mdev = 0` in `st_media_memdev_info`, rebuild, and read whether the file node's
pages arrive through `st_medium_virt` (`_strategy_dev`/`_strategy_read` carrying the request). That is a
**build**, and it is the arm section 4 says is worth taking — for its own reasons, not for this one.

## 3. What a real filesystem would cost — measured, not estimated

The other half of the mount question is the filesystem, and it was priced by reading in experiment 529
(68,285 lines; port from 2050 into 4570's VFS, add a `vfstbllist[]` row before mockfs, `FT_HFS`, an
`HFS` option). That number has never been tested. `tools/hfs_port_probe.sh` tests it: a clean sandbox in
`/tmp`, 2050's HFS+ compiled against 4570's headers with the stage90 kernel build's **own** clang
invocation, its configuration defines, and its nine forced headers.

The drifts, in the order they appear:

| # | drift | what it cost |
| --- | --- | --- |
| 1 | `false`/`true` — 4570 reaches `<stdbool.h>` through `<kern/...>`, which `#define`s them, and 2050's `hfs_macos_defs.h:152` declares them as an **enum** under `#if !TYPE_BOOL` (TYPE_BOOL is 0 in C) | one header, one block, **31 of 36 files** until fixed |
| 2 | the sandbox's own path confusion — HFS's headers include each other as `"../../hfs.h"`, so the *tree* must be on the include path under the name `hfs` | the first clean run failed 31 files on one include |
| 3 | **two headers 4570 does not have at all**: `bsd/vfs/vfs_journal.h` (`hfs.h:62`, `hfs_vfsops.c:95`) and `bsd/machine/spl.h` (`hfs_vnops.c:57`) | 29 files, one include each |
| 4 | five `M_HFS*` malloc types (`bsd/sys/malloc.h:168,169,170,188,189`) — 4570 declares none; 75/76/77/95/96 are free there and 4570's highest is 128 | 6 files |
| 5 | `DOWHITEOUT`, `ISWHITEOUT` — dropped from 4570's `vnode.h`; read only in namei's absurd branch (declare a wap for a name that does not exist) | 2 files |
| 6 | `VFC_VFSDIRLINKS` — 2050's `mount_internal.h:318`; 4570 has no such bit and **nothing reads one** (2050's only reader, `vfs_syscalls.c:3629`, is gone) | 1 site, **semantically open** |
| 7 | `kmem_alloc` — 2050's takes 3 arguments, 4570's 4 (the memory tag) | 7 call sites |
| 8 | `VTOCMP(vp)->cmp_type` laminated over `c_decmp` — 4570 keeps `decmpfs_cnode.c_decmp` as an **`int`**, 2050 keeps a pointer | **1 expression**, 1 file |

**The result:**

```
CONFIG_PROTECT=0   ok=34  fail=2  hang=0  of 36
CONFIG_PROTECT=1   ok=31  fail=5  hang=0  of 36   (the stage90 configuration's own value)
```

The two failures at `CONFIG_PROTECT=0` are `hfs_cprotect.c` (which exists only for POSIX file protection
— a root filesystem does not need it, and at `CONFIG_PROTECT=0` its remaining error is a single
`cp_wrap_func_t` at line 1751) and the one `VTOCMP` expression. **So "port HFS+" is not 68,285 lines of
work: it is eight small drifts plus one file that can be left out.** `CONFIG_PROTECT=1` costs four more
files, all on 2050's `cp_*` API (`struct cp_wrap_func`, `cp_wrap_func_t`, `CP_READ_ACCESS`,
`CP_WRITE_ACCESS`, `struct cp_root_xattr`), which 4570 renamed and restructured — real work, and the price
of mounting a volume whose files carry protection.

Two things this number is not. It is a **compile** count: whether the objects **link** against 4570's
`vfs_*`, whether `hfs_init`'s allocations and `BTReserveSetup()` work in this kernel, and whether anything
**mounts** are all untested. And a port still needs a `vfstbllist[]` row **before** mockfs, an `FT_HFS`,
and an `HFS` option — none of which this probe touches.

## 4. The finding, which is not a rung

**The medium is not what decides whether the OS comes up; one ioctl argument is.** The mount has exactly
one parameter that matters and it is a word in a struct the payload already fills:

> Make the staged device **stop answering `DKIOCGETMEMDEVINFO`**, or make it answer in a way that does not
> select the memory-backed path, and `mockfs_fsnode.c`'s file node must come through `cluster_pagein` →
> the device's `strategy` — which is exactly the body 864 built and which **has never executed**.

That is a one-value experiment, it needs no HFS port, and it is the smaller of the two ways to make the
storage track mean something. The larger way — port HFS+, then make the root device answer the three
ioctls `hfs_mountfs` hard-requires (`DKIOCGETBLOCKSIZE`, `DKIOCSETBLOCKSIZE`, `DKIOCGETBLOCKCOUNT`, all of
which 4570's `memdev`/`st_media_ioctl` already implement) and put the HFS row before mockfs — is now known
to be tractable, and is the wrong place to start.

**And the reason this was not built tonight is the single fact that makes it risky, and it should be
stated as a risk rather than a plan:** `mi_phys = 0` is precisely the field that makes mockfs take the
memory-backed path — so it is also the field whose removal moves the `/sbin/launchd` source out of
`pager_map_to_phys_contiguous` and into a device. There are two live ways that goes wrong and neither is
visible from the host:

1. **The exec is lost.** `mockfs_fsnode.c:333-344` is the only thing making the file's bytes the RAM
   disk's; route it through the device and the bytes become `st_medium_virt`, which at the moment of an
   early mount holds **the staged partition's superblock**. A file node that begins elsewhere resolves
   `/sbin/launchd` to something `exec_mach_imgact` will not claim — and mockfs's own TODO
   (`mockfs_vfsops.c:75-78`) says what it does with a device that does not look like a Mach-O:
   **"this would prevent us from causing EBADMACHO panics further along the boot path."** That panic is
   silent in this configuration (the printf strings are compiled out), so the failure arrives as a hang.
2. **The mount is lost.** If `DKIOCGETMEMDEVINFO` starts failing, mockfs_mountroot's memory-backed setup
   is skipped for a root that has no other source; the run stops at the mount instead of at the exec.

Which is why the same build must also make the staged bytes **the executable** rather than a sector of
somebody else's partition — that is a decision about which partition is staged and what is written to it,
and it is the decision the goal already made in the other direction: 「可以twrp写入到存储里了」 is about
writing an OS to storage, and 860 did it for TWRP. Staging a partition whose first bytes are a Mach-O is a
different arm from staging a filesystem's superblock, and the two cannot be the same build.

## 5. What this changes, and what it does not

- **The park backlog is not touched.** Rungs 57 (`armed-storage-71b54d73`), 58 (`armed-storage-b00b87bb`)
  and 59 (`armed-storage-7189e9b`, the only one with `STAGE90_XNU_MOUNT=1`) stay **parked and unpressed**.
  This document adds no arm, spends none, and asserts nothing about any of them.
- **Nothing is claimed about the unpressed arms.** Whether rung 59's strategy serves anything is a
  question only a press answers, and section 4's finding says **it would not be reached even then** —
  which is a reason to press it for the ladder's own reading, not a reason to expect storage.
- **The HFS+ port is now costed rather than estimated**, and the number is small enough that
  experiment 529 §8's alternatives ("a smaller purpose-built read-only filesystem") no longer need to be
  considered on cost grounds.
- **The goal is not met and this document does not move it.** The OS enters on a synthetic root; the
  medium has never been read by the mounting OS; TWRP-to-storage stays done-and-separate (860) with the
  XNU driver clause still open.

## 6. Safety

Unchanged and untouched: **no device, no build of the payload, no switch, no arm, no press, and no file
in the repository written except this document, its README row, and `tools/hfs_port_probe.sh`.** The tool
writes only under `/tmp/mi4-hfs-port-probe` and reads the repository; it is deliberately **not** in
`make check`, because it is a measurement with a decision attached and not a guard on the tree. `make
check` is **exit 0** with the new file present. Nothing is ever flashed in this project; `fastboot boot`
only. The parked arms are records and not a queue.