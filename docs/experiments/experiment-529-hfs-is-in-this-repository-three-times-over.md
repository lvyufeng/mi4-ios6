# 529: HFS+ is in this repository three times over, and the missing link is not the filesystem

**A host-side reading: no device, no build, no switch.** It corrects a claim that three documents carry as
the reason 「挂载存储」 cannot be reached - 523 section 3's *"There is no HFS in this tree"*, repeated in 526
section 5 and 527 section 8 - and it replaces that reason with the one the repository actually supports.

In one paragraph: **the claim was checked against one tree and stated of the repository.** `find` over
`external/xnu-4570.1.46` returns no HFS and that tree's `bsd/conf/files` has no HFS entry, both true - and
the repository holds **three other XNU trees, all three carrying HFS+, with a real `hfs_mountroot`
registered in `vfstbllist[]`**. What is genuinely absent is not the filesystem but the **I/O Kit** link that
gives a disk a BSD device name - `IOMedia`, from `IOStorageFamily`, which is in neither the tree nor the
repository. And that link turns out **not to be required**: `vfs_mountroot()` builds root's device vnode
from the global `rootdev` alone, through `bdevvp`, and `IOFindBSDRoot` is only the usual way `rootdev` gets
set. HFS+ mounts over exactly that seam, and the tree's only block device already sits on it.

## 1. The claim, verbatim, and the scope shift inside it

523 section 3, in full:

> * **There is no HFS in this tree.** `find` over `external/xnu-4570.1.46` returns no HFS source and
>   `bsd/conf/files` has no HFS entry; Apple's HFS lives in a separate driver component. So the
>   configuration has devfs, routefs, fifo, specfs and mockfs - and nothing that can read a partition.
>
> The consequence for "让os可以正常启动并且挂载存储" is that **a storage driver is not sufficient**. The
> phone's userdata is ext4 or f2fs; neither is in XNU at any copyright date, and the tree that would mount
> it does not exist here.

Three sentences, and they are not the same kind of statement:

| sentence | status |
| --- | --- |
| `find` over **`external/xnu-4570.1.46`** returns no HFS source | **true**, and scoped to that path |
| **that tree's** `bsd/conf/files` has no HFS entry | **true**, same scope |
| "Apple's HFS lives in a separate driver component" | true - and *where it lives is this repository* |
| therefore **"there is no HFS in this tree"** and the configuration has "nothing that can read a partition" | **the scope shift**: a fact about one directory stated as a fact about the project |

The same sentence is carried on in **526 section 5** (*"there is no HFS in this tree"*), in **527 section 8**
(*"and there is no HFS in this tree"*) and in the experiment-523 README row, where it has become a premise
rather than a reading. This is `mi4-not-absent-its-build-output`'s exact shape - the thirteenth entry in
that file's table was "belongs to another Apple OSS component" - and it is the first time in this project
that the *search root* was one directory too deep.

## 2. What the repository holds

Four XNU trees, not one:

| tree | `bsd/hfs` | files | LOC (incl. `hfscommon/`) | `vfc_mountroot` in `vfstbllist[]` |
| --- | --- | --- | --- | --- |
| `external/xnu-4570.1.46` (**the tree this project builds**) | **absent** | 0 | 0 | **no HFS row; no `FT_HFS`** |
| `external/xnu-2050.18.24` | present | 61 | 68,285 | `xnu-2050.18.24/bsd/vfs/vfs_conf.c:119` |
| `external/apple-xnu-rel-2050` | present | 61 | 68,285 | `apple-xnu-rel-2050/bsd/vfs/vfs_conf.c:119` |
| `external/xnu-upstream` | present | 61 | 68,285 | `xnu-upstream/bsd/vfs/vfs_conf.c:119` |

The three are **not byte-identical copies**: `md5sum */bsd/hfs/hfs_vfsops.c` gives two distinct values
(`xnu-upstream` and `apple-xnu-rel-2050` share `911695f3…`; `xnu-2050.18.24` is `435976b9…`), and `diff -rq`
shows 4 files differing between `apple-xnu-rel-2050` and `xnu-upstream`, 7 between `xnu-2050.18.24` and
`xnu-upstream`. So a port has three sources to choose from rather than one source three times - and
`xnu-upstream` is the natural base, being the one the other two converge on.

Alongside them, and *not* usable for this: the vendored Android kernel carries `fs/hfsplus`, `fs/exfat`,
`fs/ufs` and `fs/squashfs`. Those are Linux VFS implementations and cannot be linked into XNU; they are a
*reference* for the on-disk format (catalog B-tree, extent overflow, Unicode name comparison) and nothing
more. Naming them here because they are the other reason a reader who greps the repository for "hfs" gets
a confusing answer.

## 3. The row 4570 does not have

2050's table entry, verbatim (`bsd/vfs/vfs_conf.c:117-120`):

```c
	/* HFS/HFS+ Filesystem */
#if HFS
	{ &hfs_vfsops, "hfs", 17, 0, (MNT_LOCAL | MNT_DOVOLFS), hfs_mountroot, NULL, 0, 0, VFC_VFSLOCALARGS | VFC_VFSREADDIR_EXTENDED | VFS_THREAD_SAFE_FLAG | VFC_VFS64BITREADY | VFC_VFSVNOP_PAGEOUTV2 | VFC_VFSVNOP_PAGEINV2, NULL, 0},
#endif
```

with `extern struct vfsops hfs_vfsops;` and `extern int hfs_mountroot(mount_t, vnode_t, vfs_context_t);`
at `:99-100`. **`hfs_mountroot` is the `vfc_mountroot` field**, so HFS+ is a root filesystem in the same
sense mockfs is - and unlike mockfs it is an on-disk format. `bsd/conf/files:459-…` lists 36 `bsd/hfs/*.c`
lines, each `optional hfs`, and `:131` declares `OPTIONS/hfs  optional hfs` - it is a build knob, not an
inherent property.

4570 has neither the source, nor the row, nor the knob, and its `fs_type_num` (`bsd/vfs/vfs_conf.c:111-118`)
does not enumerate `FT_HFS`:

```c
enum fs_type_num {
	FT_NFS = 2,
	FT_DEVFS = 19,
	FT_SYNTHFS = 20,
	FT_ROUTEFS = 21,
	FT_NULLFS = 22,
	FT_MOCKFS  = 0x6D6F636B
};
```

2050's row uses the literal `17` and no enumerator anywhere in `bsd/` defines it, so the port adds a number
as well as a row. **The ordering rule matters as much as the row**: 4570's mockfs entry carries the comment
*"If we are configured for it, mockfs should always be the last standard entry (and thus the last FS we
attempt mountroot with)"* (`bsd/vfs/vfs_conf.c:146`), and `vfs_mountroot` takes the **first** entry whose
mountroot returns 0. An HFS row placed after mockfs would never be reached.

**And the comment cannot be used to find that row**, because it is printed twice. The same sentence appears
at `:151` above the **routefs** row, which sits below mockfs in the table - so the comment's own marker
points at a row that is *after* mockfs and that **cannot mount root at all** (`routefs`'s `vfc_mountroot` is
NULL and it carries no `VFC_VFSCANMOUNTROOT`, and `vfs_mountroot` skips exactly that combination). The table
is mockfs-then-routefs-then two `{NULL, "<unassigned>", …}` sentinels; the comment is right about mockfs and
wrong about where mockfs is. This is `mi4-a-claim-in-a-comment-is-not-a-check` at its smallest scale: the
statement a reader would use to place a new row is the one that is out of date, and the row order is the
check.

## 4. The link that is missing, and the one that is not needed

`IOFindBSDRoot` (`iokit/bsddev/IOKitBSDInit.cpp:349`) has four exits, and the project has only ever taken
the first:

| exit | condition | what it produces |
| --- | --- | --- |
| **/chosen/memory-map `RAMDisk`** (`:436-450`) | a `RAMDisk` property exists | `mdevadd(-1, ml_static_ptovirt(word0) >> 12, word1 >> 12, 0)`, then `rd=md0` selects it via `mdevlookup` and jumps to `iofrootx` (`:467-490`) - **what this project uses** |
| `rd=mdX` naming an unconfigured memory device (`:489`) | `mdevlookup` returns < 0 | **`panic("IOFindBSDRoot: specified root memory device, %s, has not been configured\n", …)`** |
| `rd=<bsd name>` (`:491-523`) | `rdBootVar[0]` set and not `md…` | `IOBSDNameMatching(look)` (`:522`) - an **`IOMedia`** matched by BSD name; `en*` is special-cased to network |
| the default (`:530-566`) | nothing above | `IOService::serviceMatching("IOMedia")` with `Content = Apple_HFS` (`:530-531`), then the unbounded `waitForService` loop that prints `Still waiting for root device` (`:556`) |

**Every one of the last three needs `IOMedia`.** And `IOMedia` is not in the repository:
`find` over the whole repo for `IOStorageFamily*`, `IOMedia*`, `IOBlockStorageDevice*` or
`IOApplePartitionScheme*` returns **nothing**, `grep -rl` for the class names in any `.c`/`.cpp`/`.h`
returns **nothing**, and `IOKitBSDInit.cpp` refers to `"IOMedia"` and `"IOMediaBSDClient"` (`:872-874`) only
as **class-name strings** handed to `serviceMatching` - the classes themselves come from
`IOStorageFamily.kext`, an Apple OSS component this repository does not contain. So the sentence "a storage
driver is not sufficient" is true, but 523's reason for it is not: what is missing is not the filesystem
but this layer.

**And this layer is not on the required path.** `vfs_mountroot()` (`bsd/vfs/vfs_subr.c:1040`) is short and
does not mention I/O Kit at all:

```c
	if (mountroot != NULL) { error = (*mountroot)(); return (error); }
	if ((error = bdevvp(rootdev, &rootvp))) { … return (error); }
	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) {
		if (vfsp->vfc_mountroot == NULL && !ISSET(vfsp->vfc_vfsflags, VFC_VFSCANMOUNTROOT)) continue;
		mp = vfs_rootmountalloc_internal(vfsp, "root_device");
		mp->mnt_devvp = rootvp;
		if (vfsp->vfc_mountroot) error = (*vfsp->vfc_mountroot)(mp, rootvp, ctx);
		else                    error = VFS_MOUNT(mp, rootvp, 0, ctx);
		if (!error) { … mount_list_add(mp); … }
```

`rootdev` is a **plain global**: `dev_t rootdev;` at `bsd/kern/bsd_init.c:237`, `extern dev_t rootdev;`
at `bsd/sys/systm.h:138`, and its only writer in the boot path is `bsd_init.c:1111`'s call to
`IOFindBSDRoot` - with a fallback that proves how loose the coupling is:

```c
	err = IOFindBSDRoot(rootdevice, sizeof(rootdevice), &rootdev, &flags);
	if( err) { … rootdev = makedev( 6, 0 ); strlcpy(rootdevice, "sd0a", sizeof(rootdevice)); flags = 0; }
```

So the boot needs **a `dev_t`**, not an `IOMedia`. `bdevvp` (`bsd/vfs/vfs_subr.c:1059`, defined just above)
turns that `dev_t` into a `VBLK` vnode with `spec_vnodeop_p` and takes no I/O Kit input at all. **The device
vnode root is mounted from is a BSD-level object, and `IOFindBSDRoot` is a supplier of one number.** The
payload is kernel code and already writes kernel state by name of address; setting `rootdev` is not a new
kind of act for it.

## 5. The seam, measured: what HFS+ asks of a block device

`hfs_mountfs` is reached from `hfs_mountroot` (`bsd/hfs/hfs_vfsops.c:178`) and touches the device **only**
through `VNOP_IOCTL` and `buf_strategy` (`hfs_readwrite.c:2821`). That no-I/O-Kit claim is checked rather
than assumed: `grep -rn '#include <IOKit' bsd/hfs/` returns **nothing**, and `grep -rn 'IOService\|IOMedia\|
IOStorage\|IOBlockStorage' bsd/hfs/` returns exactly **one** line - a *comment* at `hfs_vfsops.c:4834`
("`a special cpentry to the IOMedia/LwVM code for handling`") with no code under it. It is a vnode
filesystem, and the ioctl contract it imposes is small:

| ioctl | where HFS+ uses it | fatal if unanswered? | does 4570's `memdev` answer it? |
| --- | --- | --- | --- |
| `DKIOCGETBLOCKSIZE` | `hfs_vfsops.c:1326` | **yes** (`ENXIO`, and a bad size is refused at `:1331`) | **yes** (`bsd/dev/memdev.c:391`) |
| `DKIOCGETPHYSICALBLOCKSIZE` | `:1340` | **no** - `ENOTSUP`/`ENOTTY` are tolerated and physical is assumed equal to logical (`:1343-1352`) | no |
| `DKIOCSETBLOCKSIZE` | `:1364`, `:1413` | **yes**, but only when `log_blksize > 512` | **yes** (`:395`) |
| `DKIOCGETBLOCKCOUNT` | `:1373`, `:1421` | **yes** | **yes** (`:407`) |
| `DKIOCGETFEATURES`, `DKIOCISSOLIDSTATE`, `DKIOCISVIRTUAL`, `DKIOCISWRITABLE`, `DKIOCSYNCHRONIZECACHE`, `DKIOCCSSETLVNAME` | `:1475`, `:1485`, `:1554`, `hfs_vnops.c:2400`, `hfs_readwrite.c:749`, `hfs_vfsutils.c:685` | all guarded or best-effort | no |

**Three calls are load-bearing, and the only block device in this tree already implements all three** - the
same `bdevsw` whose `mdevbioctl` answers exactly `DKIOCGETBLOCKSIZE` (`:391`), `DKIOCSETBLOCKSIZE` (`:395`)
and `DKIOCGETBLOCKCOUNT` (`:407`) alongside `DKIOCGETMEMDEVINFO` (`:419`) and the four max-block/segment
queries (`:375-387`). On the header side the port is nearly free: **nine of the ten `DKIOC*` constants HFS+
uses are already defined in 4570's `bsd/sys/disk.h`** (`:203`, `:204`, `:228`, `:274`, and the rest); only
`DKIOCCSSETLVNAME` is absent, and it appears at a single call site - a convenience volume-name pass-through
in `hfs_vfsutils.c:685`.

Registering such a device is likewise a solved shape in this tree: `memdev.c` calls
`bdevsw_add(-1, &mdevbdevsw)` (`:591`) to get a major number, then `devfs_make_node(makedev(major, minor),
DEVFS_BLOCK, …, "md%d", devid)` (`:607`) to make the node - and a real driver would do the same, with
`buf_strategy` doing the transfer where `mdevstrategy` (`:238`) does a memory copy.

**So the storage chain is three links and not four**, and the corrected status is:

1. **a root-capable filesystem** - **in the repository**, three copies, needing a port into 4570's VFS plus
   a `vfstbllist[]` row (before mockfs), an `FT_HFS`, and an `HFS` config option;
2. **a block device over the eMMC** - **absent**, and this is the real work: SDHCI at `0xf9824900`
   (`hc_mem`, 0x11c) with the vendor core at `0xf9824000` (`core_mem`, 0x800), IRQs 123 and 138, GCC clocks,
   a PMIC rail, pinmux, an ADMA descriptor ring in DMA-reachable memory, and the DLL/CDC calibration the
   `sdhci-msm.c` register block exists for - 3,359 lines of driver in the vendored kernel before any of it
   is an XNU `bdevsw`;
3. **`IOMedia` / `IOStorageFamily`** - **absent from the repository**, and **not on the required path**,
   because `rootdev` can be set directly.

## 6. A hazard the ordering creates, and that mockfs hides

`mockfs_mountroot` accepts **any** `rvp`. Its own opening comment says so, and its TODO says what that
costs (`bsd/miscfs/mockfs/mockfs_vfsops.c:75-78`):

```c
	/*
	 * TODO: Validate that the device at least LOOKS like a mach-o (has a sane header); this would prevent us
	 *   from causing EBADMACHO panics further along the boot path.
	 */
```

Three consequences, recorded because each is a way to misread a later run:

* **"root is mockfs" is not evidence that HFS+ was tried and refused.** mockfs is last and mounts anything,
  so the only way to distinguish "HFS+ is absent" from "HFS+ ran and rejected the volume" is the console
  text (`hfs: … Not mounting.` comes from `hfs_mountfs`'s own `printf`s) - not which filesystem ended up as
  root.
* **A driver plus no filesystem gets an EBADMACHO panic, not a clean failure.** If a future arm registers an
  eMMC `bdevsw` and sets `rootdev` to it while HFS+ is still unported, mockfs mounts the partition and hands
  the kernel whatever the first bytes are as pid 1.
* **`rd=mdX` is not a generic "name a device" escape** - it panics when the memory device is not configured
  (`IOKitBSDInit.cpp:489`), so it cannot be repurposed to select a non-memory `bdevsw` device.

## 7. The fourth link: there is no volume

Even with a driver and a filesystem, there is nothing in this repository to mount. `find` for `*.dmg`,
`*.ipsw` or a rootfs anywhere outside `out/` returns only the phone's own firmware partition dumps
(`xiaomi4-cancro-backup-20260604-112053/*.img`, which are bootloader and modem partitions) and this
project's own `out/stage*/stage*.img` boot images. **There is no HFS+ volume, and no macOS/iOS root
filesystem image.**

That is not a defect in the plan, because the goal answers it itself: 「如果os已经能进去了的话，可以twrp写入
到存储里了」 - the volume is to be **created on the device's eMMC by TWRP**, not found in the repository. An
HFS+-formatted partition written there is exactly what `hfs_mountroot` reads. This is also where 523's
framing of the filesystem requirement should be corrected: 523 costed *"a filesystem that can read what is
on the partition"* against **the phone's existing Android userdata**, which really is ext4 or f2fs and
really is in no XNU tree. The goal asks for something different - **write our OS to storage, then mount
it** - and for that the partition's format is ours to choose. HFS+ is the one choice the repository already
contains a reader for.

The TWRP step itself stays **withheld**, unchanged: its precondition is 「如果os已经能进去了的话」, the OS
still does not survive its first idle pass, and nothing in this document touches the device.

## 8. What this does not decide

* **Whether to port HFS+.** 68,285 lines of 2050-era C against 4570's VFS is a bounded but real job, and
  this document measures it rather than choosing it. The alternatives are a smaller purpose-built
  read-only filesystem over a container of our own, and they are not compared here.
* **What the eMMC driver's shape is** - section 5 link 2 is a register map, not a design. In particular
  nothing here says whether the first block device should be SDHCI-proper with ADMA or a polled PIO reader
  bounded to a small transfer, which is the same bounded-vs-unbounded choice the console's putc is
  currently making.
* **Whether the payload can set `rootdev` safely, and where.** The order is fixed and narrow: `setconf()`
  at `bsd/kern/bsd_init.c:940` calls `IOFindBSDRoot` (`:1106`, `:1111`) and `vfs_mountroot()` is the very
  next thing (`:946`). So there are exactly two candidate points - inside the `IOFindBSDRoot` supply, or
  between `:940` and `:946` - and which of them the payload can reach, and with what it must have already
  registered (the `bdevsw` and its `devfs` node), is the next thing to read. It is a reading, not a plan.
* **Nothing about 526.** Its run is still owed a power press, and 522's third non-return still stands.

## 9. Safety

Unchanged and untouched: **no device, no build, no switch, no edit to any running script**, and nothing is
ever flashed in this project. `fastboot boot` only, one non-persistent boot per run, through
`preflight_boot_check.sh --allow-xnu-entry` then `run_and_capture.sh --allow-xnu-entry`. This document is a
reading of files already in the repository - no file outside `docs/` was written. The phone is off the bus
(last `usb 3-10` event: 522's `18d1:d00d` device 88 disconnecting at 14:14:46 on 2026-09-22) and owes a
power press before 526 can run.
