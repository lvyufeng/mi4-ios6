/*
 * The payload-owned ROOT MEDIA (experiment 861) - 530 section 9's wrapping route, made buildable.
 *
 * What this is, and the two things it is NOT
 * ------------------------------------------
 * `bsd_init` computes the root device in a handful of inlined instructions and hands it to
 * `vfs_mountroot`. In the built image the number it computes comes from `mdevlookup(xchar)`
 * (`iokit/bsddev/IOKitBSDInit.cpp:467`, inlined into `bsd_init` at `0x80049b2c`), and 530 section 2
 * proved `__wrap_mdevlookup` is already linked (`0x8047db1c`) - so *the root device's number has a
 * supplier the payload owns*, with no edit to Apple's `bsd_init` and no `IOMedia` in the path (530
 * section 2: "No I/O Kit object, no `IOMedia`"). That is the whole route: this file registers a
 * `bdevsw` and hands back a `dev_t`, and `vfs_mountroot` then does `bdevvp` + `vfs_init_io_attributes`
 * on it (`bsd/vfs/vfs_subr.c:3134-3352`, all `VNOP_IOCTL` on the device vnode).
 *
 * **It is NOT the eMMC driver and it moves no byte of the eMMC.** Its `strategy` is `eno_strat`: it
 * satisfies no read. What it exercises is the MOUNT PATH's ioctl half - the twelve `DKIOC` reads
 * `vfs_init_io_attributes` hard-requires plus the three it tolerates - over a fixed geometry, which
 * is exactly the "evidence-bearing next step" 530 section 9 names ("root the existing mockfs root at
 * a payload-owned fake `dev_t` whose `bdevsw` answers `DKIOCGETBLOCKSIZE`/`DKIOCSETBLOCKSIZE`/
 * `DKIOCGETBLOCKCOUNT` from a buffer ... with no eMMC driver at all"). The data half is the eMMC
 * driver's job (529 section 5, the SDHCI work) and is where this stand-in is replaced.
 *
 * **862: the buffer it answers from is the entry image's own RAM disk, and that is what makes the
 * mount keep working.** 530 section 9 says "a buffer"; `mockfs_mountroot` (`mockfs_vfsops.c:101`)
 * says WHICH buffer it must be. It asks the root device `DKIOCGETMEMDEVINFO`; only a device that
 * answers it becomes `mockfs_memory_backed`, pointing its one file node straight at the memory
 * (`mockfs_fsnode.c:333-344`). The memory-backed root today is `/dev/md0`, made by `mdevadd` over
 * `g_stage90_ramdisk` - the bytes of `/sbin/launchd` - and that is why process 1 can be exec'd from
 * the root at all. A device that answered `ENOTTY` there would still MOUNT, but its file node's
 * bytes would have to come through `cluster_pagein` -> the device's `strategy`, and `eno_strat` moves
 * no byte. So this file does not invent a second buffer: it maps the SAME `g_stage90_ramdisk` the RAM
 * disk does (its address and end are the symbols `entry_ramdisk.s` defines, handed in by
 * `build_entry.sh`), and answers `DKIOCGETMEMDEVINFO` with `mi_phys = false` exactly as `mdevadd` is
 * called (`IOKitBSDInit.cpp:447`) - because mockfs SKIPS the memory-backed path when `mi_phys` is
 * true (`mockfs_vfsops.c:104-109`).
 *
 * **It is also NOT free-standing geometry.** The geometry is DERIVED: blocks = the RAM disk's own
 * byte length / 512, pages = that length >> 12. There is no second number to keep in step, which is
 * the point - `mi_base << 12` must land on exactly the memory `mockfs` was told the device is, and a
 * literal would drift from it the first time `RAMDISK_BYTES` moved. The storage ladder's rung 57
 * ("select by extent, not by name") is the *other* medium and will want a keyed device; when it is
 * built it can register a second `st_media` unit, and until then the one unit here is the root.
 *
 * Why a C file, and why this list
 * -------------------------------
 * `memdev.c` is this tree's only block device and its `mdevbdevsw` is the template - six function
 * pointers and a `D_DISK` (`bsd/sys/conf.h:92`, value **2**). The BSD layer reaches a `bdevsw`
 * through `bdevsw_add` (`conf.h:309`), a plain C symbol. So
 * this file is compiled the way `stage90_pthread_functions.c` is: by `tools/build_xnu_arm_kernel.sh`'s
 * PLATFORM_BSD_SOURCES loop, with the **bsd** component's defines and import roots, so `struct bdevsw`,
 * the `*_fcn_t` typedefs, `eno_dump`/`eno_strat`, `D_DISK`, `makedev` and the `DKIOC*` commands are the
 * kernel's own definitions rather than a copy of them. A hand-copied `struct bdevsw` that was wrong by
 * one field would put `d_ioctl` where the kernel reads `d_strategy` - the "one value, two definitions"
 * defect this project has a memory about - and it would not fail loudly.
 *
 * The reading
 * -----------
 * `register` writes live keys on the entry instrument's own channel (`entry_live_write`, declared
 * `extern` here the way `MSM8974RootResource.cpp` declares it), so a run that reaches here says so
 * without a printf:
 *
 *     xnu_live_rootmedia_major   the major `bdevsw_add` returned, or the negative error
 *     xnu_live_rootmedia_dev     the `dev_t` handed to the mount path (`makedev(major, unit)`)
 *     xnu_live_rootmedia_blocks  the block count derived from the RAM disk (bytes / 512)
 *     xnu_live_rootmedia_pages   the PAGE count mockfs maps the file node onto (bytes >> 12) - the
 *                                one number that says the device is memory-backed and not a stand-in
 */
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>

/* The entry instrument's own entry point, declared not included: the object is linked by
 * `src/entry/build_entry.sh`, which is what owns `entry_live_write`. */
extern void entry_live_write(const char *key, unsigned int value);

/*
 * THE RAM DISK THIS DEVICE IS MEMORY-BACKED OVER (experiment 862).
 *
 * Every symbol here is an ADDRESS that `entry_ramdisk.s` defines (`g_stage90_ramdisk` at the array,
 * `g_stage90_ramdisk_end` at its end); `STAGE90_ROOT_MEDIA_SIZE_SYM` is passed by `build_entry.sh`
 * as `-Dexplicit=...`, so this translation unit carries NO second copy of any of them. That is the
 * whole point of the round trip: a module that hard-coded `0x2000` would be the project's oldest
 * defect (`mi4-one-value-two-definitions`) the first time `RAMDISK_BYTES` moved.
 *
 * The size is a byte length here and a PAGE COUNT everywhere it is used (`mdevadd` stores `size >>
 * 12`, `mockfs` shifts it back) - the conversion is done once, in `st_media_memdev_info`, not at the
 * three call sites.
 */
#ifndef STAGE90_ROOT_MEDIA_SIZE_SYM
#error "STAGE90_ROOT_MEDIA_SIZE_SYM must name the RAM disk end symbol - entry_trace.c's build sets it"
#endif

extern char g_stage90_ramdisk[];
extern char STAGE90_ROOT_MEDIA_SIZE_SYM[];

/* Page size for the byte -> page conversion. The kernel's own `PAGE_SHIFT` is 12 on this port; the
 * literal is used rather than including a machine header so this file stays in the BSD component's
 * include set. `build_entry.sh` statically asserts `g_stage90_ramdisk` is 4096-aligned, which is
 * what makes a page-count base well defined. */
#define ST_MEDIA_PAGE_SHIFT 12u

/* One 512-byte block. `DEV_BSIZE` is 512 here (`bsd/arm/param.h:65`). The block COUNT is derived:
 * the device's true size is whatever the RAM disk is, so `blockcount = size / 512` and the two can
 * never disagree. The `+ disk` keying of 861 is gone with the fake geometry it was keying: a
 * memory-backed device's size is a fact about the memory it maps, not a knob. */
#define ST_MEDIA_BLOCKSIZE 512u

/* Two disks' worth of identity, so the selection is a device and not a constant. */
#define ST_MEDIA_DISKS      2

/*
 * The flags this device answers `DKIOCGETMEMDEVINFO` from: ON `mdInited` (a non-inited device
 * returns ENXIO, `memdev.c:419`) and OFF `mdPhys`. `mdPhys` off is load-bearing and copied from
 * memdev's own choice (`mdevadd` is called with `phys = 0`, `IOKitBSDInit.cpp:447`): mockfs's
 * `mockfs_mountroot` reads `mi_phys` into `mockfs_physical_memory` and then *skips the memory-backed
 * optimisation entirely* when it is true (`mockfs_vfsops.c:104-109`), so answering "physical" would
 * re-open the `cluster_pagein` path this whole experiment exists to close.
 */
#define ST_MEDIA_MDINITED  0x1u
#define ST_MEDIA_MDPHYS    0x2u

static int     st_media_major[ST_MEDIA_DISKS];
static unsigned st_media_blocksize[ST_MEDIA_DISKS];
static unsigned st_media_blockcount[ST_MEDIA_DISKS];
static unsigned st_media_flags[ST_MEDIA_DISKS];

static unsigned
st_media_bytes(void)
{
    return (unsigned)((uintptr_t)STAGE90_ROOT_MEDIA_SIZE_SYM - (uintptr_t)g_stage90_ramdisk);
}

/* ----------------------------------------------------------- the switch bodies (memdev's shape) */

static int
st_media_open(dev_t dev, int flags, int devtype, struct proc *p)
{
    int unit = minor(dev);
    if (unit >= ST_MEDIA_DISKS) return ENXIO;
    return 0;
}

static int
st_media_close(dev_t dev, int flags, int devtype, struct proc *p)
{
    return 0;
}

/*
 * The data half. **Left as `eno_strat` on purpose** - this stand-in has no sectors to move, so its
 * honest answer is Apple's own "not a strategy" stub rather than a body that pretends. A NULL here
 * would make the `bdevsw` short of the field the kernel dereferences; `eno_strat` is Apple's
 * `enodev_strat`, the same one `mdevcdevsw` puts in the slot it does not use (`memdev.c:153`).
 */
/* (strategy slot, below, is eno_strat) */

static int
st_media_size(dev_t dev)
{
    int unit = minor(dev);
    if (unit >= ST_MEDIA_DISKS) return 0;
    return (int)st_media_blocksize[unit];
}

/*
 * THE MEMORY-BACKED ANSWER, in a named `noinline` function so a build clause can read it BY VALUE.
 *
 * `mockfs_mountroot` (`mockfs_vfsops.c:101`) asks the root device `DKIOCGETMEMDEVINFO`; only if the
 * call SUCCEEDS (returns 0) does it set `mockfs_memory_backed = mi_mdev` and point the file node's
 * pager straight at `mi_base << 12` (`mockfs_fsnode.c:333-344`, `pager_map_to_phys_contiguous`).
 * When it does, `/sbin/launchd`'s bytes come out of the mapped physical pages with no `strategy` at
 * all; when it does not, they must come through `cluster_pagein` -> the device's `strategy`, which
 * `eno_strat` does not implement. So this one body is the difference between "mockfs mounts" and
 * "mockfs mounts AND process 1 can be exec'd from it".
 *
 * It is `noinline` on purpose: 855's build clause read a two-arm `if`'s stores out of the LINKED
 * disassembly and was refuted by the linker's own tail-merge (LINKED ORDER IS NOT SOURCE ORDER). A
 * check that reads this function's body is reading a property of ONE named function, which is
 * layout-independent.
 */
__attribute__((noinline)) static int
st_media_memdev_info(dk_memdev_info_t *info)
{
    info->mi_mdev = 1;                                   /* yes, a memory device (`boolean_t` = int)  */
    info->mi_phys = (st_media_flags[0] & ST_MEDIA_MDPHYS) ? 1 : 0;
    info->mi_base = (uint32_t)((uintptr_t)g_stage90_ramdisk >> ST_MEDIA_PAGE_SHIFT);
    info->mi_size = (uint64_t)(st_media_bytes() >> ST_MEDIA_PAGE_SHIFT);
    return 0;
}

static int
st_media_ioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
    int unit = minor(dev);
    if (unit >= ST_MEDIA_DISKS) return ENXIO;

    switch (cmd) {
    /*
     * 862: the ioctl mockfs_mountroot reads. Answering it with a page count of the entry image's own
     * RAM disk is what makes this device memory-backed - see st_media_memdev_info.
     */
    case DKIOCGETMEMDEVINFO:
        return st_media_memdev_info((dk_memdev_info_t *)data);
    /* --- the twelve `vfs_init_io_attributes` hard-requires: a non-zero return fails the mount --- */
    case DKIOCGETBLOCKSIZE:
        *(uint32_t *)data = st_media_blocksize[unit];
        break;
    case DKIOCGETFEATURES:
        *(uint32_t *)data = 0u;
        break;
    case DKIOCGETCOMMANDPOOLSIZE:
        *(uint32_t *)data = 1u;
        break;
    case DKIOCGETMAXBLOCKCOUNTREAD:
    case DKIOCGETMAXBLOCKCOUNTWRITE:
        *(uint64_t *)data = st_media_blockcount[unit];
        break;
    case DKIOCGETMAXBYTECOUNTREAD:
    case DKIOCGETMAXBYTECOUNTWRITE:
        *(uint64_t *)data = (uint64_t)st_media_blocksize[unit] * st_media_blockcount[unit];
        break;
    case DKIOCGETMAXSEGMENTCOUNTREAD:
    case DKIOCGETMAXSEGMENTCOUNTWRITE:
        *(uint64_t *)data = 1u;
        break;
    case DKIOCGETMAXSEGMENTBYTECOUNTREAD:
    case DKIOCGETMAXSEGMENTBYTECOUNTWRITE:
        *(uint64_t *)data = (uint64_t)st_media_blocksize[unit];
        break;
    case DKIOCGETMINSEGMENTALIGNMENTBYTECOUNT:
        *(uint64_t *)data = (uint64_t)st_media_blocksize[unit];
        break;
    /* --- the three optional reads: a non-zero return only means the feature is not advertised --- */
    case DKIOCGETTHROTTLEMASK:
        *(uint64_t *)data = 1u;
        break;
    case DKIOCISVIRTUAL:
    case DKIOCISSOLIDSTATE:
        *(uint32_t *)data = 0u;
        break;
    /* --- accepted so a mount that probes first does not fail on it --- */
    case DKIOCSETBLOCKSIZE:
        break;
    case DKIOCGETBLOCKCOUNT:
        *(uint64_t *)data = st_media_blockcount[unit];
        break;
    case DKIOCISWRITABLE:
        *(uint32_t *)data = 1u;
        break;
    default:
        return ENOTTY;
    }
    return 0;
}

static struct bdevsw st_media_bdevsw = {
    /* open */      st_media_open,
    /* close */     st_media_close,
    /* strategy */  eno_strat,
    /* ioctl */     st_media_ioctl,
    /* dump */      eno_dump,
    /* psize */     st_media_size,
    /* flags */     D_DISK,
};

/* ------------------------------------------------------------------ registration (memdev:561) */

/*
 * `bdevsw_add(-1, &sw)` picks the first free major, exactly as `mdevadd` does (`memdev.c:591`), and
 * returns a negative error rather than a major on failure. `disk` names which keyed geometry to use
 * (see the file comment); it is also the unit, so disk 0 and disk 1 are different numbers by both
 * halves of `makedev`.
 *
 * **No `devfs_make_node`, and that is a decision rather than an omission.** `memdevadd` also makes a
 * `/dev` node (`memdev.c:607`), but `devfs_make_node` lives in `bsd_miscfs_devfs_devfs_tree.o`, which
 * this image does **not** link - so a call here would become one of the auto-generated stand-ins and
 * `xnu_live_rootmedia_node` would report a non-NULL pointer to a stub that made nothing. The mount
 * path does not need the node: `vfs_mountroot` -> `bdevvp` builds its vnode directly from the `dev_t`
 * this returns (`bd.vdev = bdev`; `bd.vminor = minor(bdev)`), so the number is the whole interface.
 * A `/dev` entry is a userspace convenience and belongs with whatever later step needs one, not here.
 *
 * The return is the `dev_t` (`makedev(major, disk)`), so `__wrap_mdevlookup` can hand the mount path
 * the number this call owns - `mdevlookup` returns `mdev[devid].mdBDev`, which is
 * `makedev(mdevBMajor, devid)` (`memdev.c:602`), and this mirrors it.
 */
dev_t
entry_root_media_register(int disk)
{
    dev_t dev;
    unsigned bytes;

    if (disk < 0 || disk >= ST_MEDIA_DISKS) return (dev_t)-1;

    /*
     * The geometry is DERIVED from the RAM disk, not chosen. `st_media_bytes()` is the link's own
     * end-minus-start (see the file top); blocks are 512 of those bytes. The `% 512` is a fact the
     * build already guarantees (`entry_ramdisk.s` pads to `RAMDISK_BYTES`, and `build_entry.sh`
     * checks the total is a whole number of sectors), but the module does not rely on a caller's
     * check for its own arithmetic: a truncated block count would make the file mockfs serves
     * shorter than the executable, so it refuses with a visible zero rather than rounding.
     */
    bytes = st_media_bytes();
    st_media_blocksize[disk]  = ST_MEDIA_BLOCKSIZE;
    st_media_blockcount[disk] = (bytes % ST_MEDIA_BLOCKSIZE) ? 0u : (bytes / ST_MEDIA_BLOCKSIZE);
    st_media_flags[disk]      = ST_MEDIA_MDINITED;      /* never ST_MEDIA_MDPHYS - see the file top */

    st_media_major[disk] = bdevsw_add(-1, &st_media_bdevsw);
    if (st_media_major[disk] < 0) {
        entry_live_write("xnu_live_rootmedia_major", (unsigned)st_media_major[disk]);
        return (dev_t)st_media_major[disk];
    }

    dev = makedev(st_media_major[disk], disk);

    entry_live_write("xnu_live_rootmedia_major", (unsigned)st_media_major[disk]);
    entry_live_write("xnu_live_rootmedia_dev",   (unsigned)dev);
    entry_live_write("xnu_live_rootmedia_blocks",(unsigned)st_media_blockcount[disk]);
    /* The one number the whole experiment turns on: the page count mockfs maps the file node onto.
     * `xnu_live_rootmedia_pages * 4096 == bytes`, and `bytes` is `g_stage90_ramdisk_end` minus
     * `g_stage90_ramdisk` in the link - so a run that prints this page count beside a non-zero
     * `..._blocks` says the device is memory-backed over the RAM disk and not merely present. */
    entry_live_write("xnu_live_rootmedia_pages", (unsigned)(bytes >> ST_MEDIA_PAGE_SHIFT));

    return dev;
}