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
/* 864: the buffer a `strategy` is handed. The kernel's own `buf_*` accessors rather than a copy of
 * their signatures: `struct buf`'s layout is the kernel's, and a locally re-spelled prototype for
 * `buf_map` or `buf_blkno` would be a second definition of a value this file only reads
 * (`mi4-one-value-two-definitions`). `memdev.c` includes the same header for the same reason. */
#include <sys/buf.h>

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
/* 864: this device's medium is the STAGED sector, not the RAM disk - so `st_media_memdev_info`
 * answers with the staged array's address and the handler below is the one that filled it. */
#define ST_MEDIA_STAGED    0x4u

static int     st_media_major[ST_MEDIA_DISKS];
static unsigned st_media_blocksize[ST_MEDIA_DISKS];
static unsigned st_media_blockcount[ST_MEDIA_DISKS];
static unsigned st_media_flags[ST_MEDIA_DISKS];

/*
 * ===================================================================================================
 * 864 - THE STAGED SECTOR: THE MEDIUM THIS DEVICE SERVES, AND WHERE ITS BYTES CAME FROM.
 *
 * 862 left the `strategy` slot as `eno_strat`, and its own doc says why: a body that "pretended" to
 * have sectors would be inventing bytes. The entry image does hold exactly one sector of the real
 * medium - the SELECTED partition's superblock, the one `st_sector_for_read`'s last arm addresses -
 * so the honest step is not a fake disk: it is to serve THAT sector and refuse the rest.
 *
 * The bytes are COPIED in (`entry_root_media_stage`), not aliased: the source is
 * `entry_storage.c`'s own `st_read_block`, and a device that held a pointer into the ladder's working
 * buffer would make "what the OS reads" and "what the ladder happens to hold" the same object - which
 * is the "one value, two definitions" defect with a mounting filesystem as the victim. 512 bytes is
 * one sector, so the copy is the whole medium this device owns, and it is `__attribute__((aligned))`
 * and a multiple of 4 so `bcopy` sees aligned halves.
 *
 * **`st_medium_virt` IS THE NUMBER `mdevstrategy`'s arithmetic needs.** The template writes
 * `fvaddr = (mdBase << 12) + blkoff` - a PHYSICAL address - because the RAM disk it serves really is
 * one. Here the medium is a `.bss` array in the kernel's own mapping, already addressable, which is
 * exactly `mdPhys == 0` (the flag 862 chose for the RAM-disk device, and the flag mockfs requires to
 * stay memory-backed). So the virtual base is the array's address and the block offset is added to it
 * directly - the `<< 12` shift exists to turn a page NUMBER into an address and there is no page
 * number here to shift.
 */
#define ST_MEDIA_SECTOR_WORDS (ST_MEDIA_BLOCKSIZE / 4u)   /* 512 / 4 = 128 */

static uint32_t st_medium_virt[ST_MEDIA_SECTOR_WORDS] __attribute__((aligned(64)));
static uint32_t st_medium_sector;        /* the medium sector the staged bytes came from */
static uint32_t st_medium_pages;         /* the disk's page count, for `DKIOCGETMEMDEVINFO`   */
static uint32_t st_medium_staged;        /* 0 until `entry_root_media_stage` has run          */

/* The two `.bss` cells the strategy's refusals are counted in, so a served read and a refused one
 * are told apart by a number in the log rather than by the absence of a log line. */
static uint32_t st_medium_served;
static uint32_t st_medium_refused;

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
 * The data half. **861/862 left this as `eno_strat` on purpose** - a stand-in with no sectors to move
 * has Apple's own "not a strategy" stub as its honest answer, and a body that "pretended" would be
 * inventing bytes. 864 replaces it with a body that serves the ONE sector the entry image actually
 * holds (see `st_medium_virt` above) and refuses everything else, which is a smaller claim than a
 * disk and a true one.
 *
 * The shape is `mdevstrategy`'s (`bsd/dev/memdev.c:235-345`), which is in this tree and linked, and
 * every accessor is the kernel's own `buf_*` - `buf_device`, `buf_blkno`, `buf_count`, `buf_flags`
 * (`B_READ`), `buf_map`, `buf_unmap`, `buf_setcount`, `buf_seterror`, `buf_setresid`, `buf_biodone`.
 * A locally re-spelled `struct buf` or an assumed accessor would silently read the wrong field, which
 * is why the header is included and no field is touched directly.
 *
 * **IT ANSWERS FROM ITS OWN COPY AND NEVER FROM THE LADDER'S BUFFER.** `entry_root_media_stage` is
 * the only writer of `st_medium_virt`; `entry_storage.c` hands the bytes over and keeps its buffer.
 * The two `st_medium_*` counters make the refusals readable: a `strategy` that never runs leaves
 * `xnu_live_rootmedia_served` absent from the log, and one that runs only to refuse says so in
 * `xnu_live_rootmedia_refused`.
 *
 * **THE TRANSFER IS BOUNDED BY THREE INDEPENDENT TESTS, AND THEY ARE THREE BECAUSE THEY FAIL
 * DIFFERENTLY.** `st_medium_staged == 0` is "there is no medium" (the mount path was reached without
 * the ladder); `buf_blkno(bp) != 0` is "the OS asked for a sector this device does not have"
 * (`st_medium_sector` is at a partition-relative block 0, see the staging call); and
 * `buf_count(bp) > 512` is "the OS asked for more than one sector" - a request whose first half is
 * servable and whose rest is not, refused whole rather than half-served.
 */
extern void bcopy(const void *from, void *to, size_t len);   /* `bsd/dev/memdev.c` calls it too */

static void
st_media_strategy(struct buf *bp)
{
    caddr_t vaddr;
    uint32_t unit = (uint32_t)minor(buf_device(bp));

    st_medium_refused++;
    entry_live_write("xnu_live_rootmedia_refused", st_medium_refused);
    entry_live_write("xnu_live_rootmedia_strategy_dev", (unsigned)buf_device(bp));
    entry_live_write("xnu_live_rootmedia_strategy_blkno", (unsigned)buf_blkno(bp));
    entry_live_write("xnu_live_rootmedia_strategy_count", (unsigned)buf_count(bp));
    entry_live_write("xnu_live_rootmedia_strategy_read",
                     (buf_flags(bp) & B_READ) ? 1u : 0u);

    if (st_medium_staged == 0u) {                 /* no medium: the ladder has not run */
        buf_seterror(bp, ENXIO);
        buf_biodone(bp);
        return;
    }
    if (unit >= ST_MEDIA_DISKS) {
        buf_seterror(bp, ENXIO);
        buf_biodone(bp);
        return;
    }
    if ((buf_flags(bp) & B_READ) == 0) {          /* this device is read-only: it has one sector */
        buf_seterror(bp, EROFS);
        buf_biodone(bp);
        return;
    }
    if (buf_blkno(bp) != 0) {                     /* the only block this device has */
        buf_seterror(bp, EINVAL);
        buf_biodone(bp);
        return;
    }
    if (buf_count(bp) > ST_MEDIA_BLOCKSIZE) {     /* more than the one sector it owns */
        buf_seterror(bp, EINVAL);
        buf_biodone(bp);
        return;
    }

    if (buf_map(bp, &vaddr) != 0) {               /* memdev panics here; this arm must not */
        buf_seterror(bp, EFAULT);
        buf_biodone(bp);
        return;
    }
    bcopy((const void *)st_medium_virt, (void *)vaddr, (size_t)buf_count(bp));
    buf_unmap(bp);
    buf_setresid(bp, 0);
    buf_biodone(bp);

    st_medium_refused--;
    st_medium_served++;
    entry_live_write("xnu_live_rootmedia_served", st_medium_served);
    entry_live_write("xnu_live_rootmedia_refused", st_medium_refused);
}

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
st_media_memdev_info(dev_t dev, dk_memdev_info_t *info)
{
    uint32_t unit = (uint32_t)minor(dev);

    info->mi_mdev = 1;                                   /* yes, a memory device (`boolean_t` = int)  */
    info->mi_phys = (st_media_flags[unit] & ST_MEDIA_MDPHYS) ? 1 : 0;
    /*
     * **THE BASE AND THE SIZE ARE THE DEVICE'S OWN, AND FOR DISK 1 THEY ARE THE STAGED SECTOR.**
     * Disk 0 is the RAM disk 862 chose (its address and length are the link's own symbols). Disk 1 is
     * the medium 864 staged: the base is the address of the staged array and the page count is the
     * one the accessor derived - so `mi_base << 12` lands on `st_medium_virt` and `mockfs`'s
     * `pager_map_to_phys_contiguous` maps the file node straight onto the sector the ladder read.
     *
     * **`mi_size` IS A COUNT OF PAGES, NOT OF SECTORS** (`memdev.c`'s `mdevadd` is called with
     * `size >> 12` and `mockfs_fsnode.c:342` shifts it back with `<< PAGE_SHIFT`). The one sector this
     * device holds is therefore `<= 1` page, and the count is rounded so it is never 0.
     */
    if (unit >= ST_MEDIA_DISKS) {
        info->mi_base = 0u;
        info->mi_size = 0u;
        return EINVAL;
    }
    if (st_media_flags[unit] & ST_MEDIA_STAGED) {
        info->mi_base = (uint32_t)((uintptr_t)st_medium_virt >> ST_MEDIA_PAGE_SHIFT);
        info->mi_size = (uint64_t)((st_medium_pages != 0u) ? st_medium_pages : 1u);
    } else {
        info->mi_base = (uint32_t)((uintptr_t)g_stage90_ramdisk >> ST_MEDIA_PAGE_SHIFT);
        info->mi_size = (uint64_t)(st_media_bytes() >> ST_MEDIA_PAGE_SHIFT);
    }
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
        return st_media_memdev_info(dev, (dk_memdev_info_t *)data);
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
    /* strategy */  st_media_strategy,   /* 864: was `eno_strat` - it moves bytes now */
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

    /*
     * **THE KEYS ARE DISK 0's, AND THE GUARD IS THE POINT.** These four names ("major", "dev",
     * "blocks", "pages") have described the RAM-disk device since 861/862, and a run reads them as
     * that device's geometry. Disk 1 registers through this same function (the major is the same;
     * only the unit differs), so writing them a second time would silently re-point four published
     * numbers at a different disk - the "one value, two definitions" defect wearing a log. Disk 1's
     * numbers are published by `entry_root_media_stage` under their own `_stage_*` names instead.
     */
    if (disk == 0) {
        entry_live_write("xnu_live_rootmedia_major", (unsigned)st_media_major[disk]);
        entry_live_write("xnu_live_rootmedia_dev",   (unsigned)dev);
        entry_live_write("xnu_live_rootmedia_blocks",(unsigned)st_media_blockcount[disk]);
        /* The one number the whole experiment turns on: the page count mockfs maps the file node
         * onto. `xnu_live_rootmedia_pages * 4096 == bytes`, and `bytes` is `g_stage90_ramdisk_end`
         * minus `g_stage90_ramdisk` in the link - so a run that prints this page count beside a
         * non-zero `..._blocks` says the device is memory-backed over the RAM disk and not merely
         * present. */
        entry_live_write("xnu_live_rootmedia_pages", (unsigned)(bytes >> ST_MEDIA_PAGE_SHIFT));
    } else {
        entry_live_write("xnu_live_rootmedia_stage_major", (unsigned)st_media_major[disk]);
        entry_live_write("xnu_live_rootmedia_stage_dev",   (unsigned)dev);
    }

    return dev;
}

/* ------------------------------------------------------- 864: the staged medium and its binding */

/*
 * **THE HANDOVER: THE LADDER'S SECTOR BECOMES THIS DEVICE'S MEDIUM.**
 *
 * `entry_storage.c` calls this from `entry_root_media_mount_disk`, after it has read the selected
 * partition's superblock into its own buffer. The arguments are that read's own numbers - the sector
 * it addressed, the disk's page count, the 128 words of the sector - and this function's whole job is
 * to stop them being that file's private state:
 *
 *   - the words are COPIED into `st_medium_virt`, so the device owns bytes it can serve at any later
 *     moment (a `strategy` may arrive after the ladder's buffer has been reused by another read);
 *   - disk 1 is REGISTERED (`entry_root_media_register(1)`) and its geometry is then OVERWRITTEN with
 *     the one this medium actually has: **one 512-byte block**, because one sector is what was handed
 *     over. `st_media_blockcount[1] = 1` is not a placeholder; it is the size of this device, and
 *     `DKIOCGETBLOCKCOUNT` answering 1 is what stops a filesystem from being told it can address a
 *     disk it cannot read.
 *   - `ST_MEDIA_STAGED` redirects `DKIOCGETMEMDEVINFO` to the staged array (see
 *     `st_media_memdev_info`), which is the flag that makes the file node's pager land on THIS sector.
 *
 * **A REFUSAL IS A RETURN, NOT A PANIC.** `nwords` above one sector is clamped rather than trusted -
 * the caller's buffer is exactly `ST_EXT_CSD_WORDS` words, and a longer count would read past it.
 * Zero words, or a registration that could not find a major, leaves `st_medium_staged` at 0 - so the
 * `strategy` refuses with `ENXIO` and a run reads that as a boot where the handover did not happen.
 */
int
entry_root_media_stage(uint32_t sector, uint32_t pages, const uint32_t *words, uint32_t nwords)
{
    uint32_t i, n;
    dev_t dev;

    if (words == 0 || nwords == 0u)
        return EINVAL;

    n = (nwords > ST_MEDIA_SECTOR_WORDS) ? (uint32_t)ST_MEDIA_SECTOR_WORDS : nwords;
    for (i = 0u; i < n; i++)
        st_medium_virt[i] = words[i];
    for (; i < (uint32_t)ST_MEDIA_SECTOR_WORDS; i++)
        st_medium_virt[i] = 0u;                /* the rest of the sector is not this image's to invent */

    st_medium_sector = sector;
    st_medium_pages  = (pages != 0u) ? pages : 1u;   /* at least the page the one sector lives in */

    dev = entry_root_media_register(1);
    if ((int)dev < 0) {
        entry_live_write("xnu_live_rootmedia_stage_err", (unsigned)dev);
        return (int)dev;
    }

    /* The registration's file-scope geometry is disk 0's; this device's is the handover's. */
    st_media_blocksize[1]  = ST_MEDIA_BLOCKSIZE;
    st_media_blockcount[1] = 1u;                             /* ONE sector - see the header comment */
    st_media_flags[1]      = ST_MEDIA_MDINITED | ST_MEDIA_STAGED;
    st_medium_staged       = 1u;

    entry_live_write("xnu_live_rootmedia_staged", 1u);
    entry_live_write("xnu_live_rootmedia_stage_sector", sector);
    entry_live_write("xnu_live_rootmedia_stage_pages", st_medium_pages);
    entry_live_write("xnu_live_rootmedia_stage_blocks", 1u);
    entry_live_write("xnu_live_rootmedia_stage_base",
                     (unsigned)((uintptr_t)st_medium_virt >> ST_MEDIA_PAGE_SHIFT));
    entry_live_write("xnu_live_rootmedia_stage_w0", st_medium_virt[0]);
    entry_live_write("xnu_live_rootmedia_stage_w1", st_medium_virt[1]);
    return 0;
}