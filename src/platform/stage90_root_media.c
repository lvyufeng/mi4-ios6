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
 * `vfs_init_io_attributes` hard-requires plus the three it tolerates - with a fixed geometry
 * underneath, which is exactly the "evidence-bearing next step" 530 section 9 names ("root the
 * existing mockfs root at a payload-owned fake `dev_t` whose `bdevsw` answers
 * `DKIOCGETBLOCKSIZE`/`DKIOCSETBLOCKSIZE`/`DKIOCGETBLOCKCOUNT` from a buffer ... with no eMMC driver
 * at all"). The data half is the eMMC driver's job (529 section 5, the SDHCI work) and is where this
 * stand-in is replaced.
 *
 * **It is also NOT free-standing geometry.** The two disk numbers are keyed, so that selecting a
 * different partition selects a different device: `entry_root_media_register(disk)` with `disk == 0`
 * registers major A / unit 0 with geometry G0, and `disk == 1` registers major B / unit 1 with G1.
 * That is the shape the storage ladder's rung 57 needs - its "select by extent, not by name" rule
 * picks `userdata`(p25) and its superblock read addresses that partition, so the device the root is
 * rooted at must be a *different* disk than partition 1's.
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
 */
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>

/* The entry instrument's own entry point, declared not included: the object is linked by
 * `src/entry/build_entry.sh`, which is what owns `entry_live_write`. */
extern void entry_live_write(const char *key, unsigned int value);

/* One 512-byte block over a fixed count. `DEV_BSIZE` is 512 here (`bsd/arm/param.h:65`). 4096 blocks
 * is 2 MiB - small enough to be an obvious stand-in, large enough to be a believable extent. */
#define ST_MEDIA_BLOCKSIZE  512u
#define ST_MEDIA_BLOCKCOUNT 0x00001000u

/* Two disks' worth of identity, so the selection is a device and not a constant. */
#define ST_MEDIA_DISKS      2

static int     st_media_major[ST_MEDIA_DISKS];
static unsigned st_media_blocksize[ST_MEDIA_DISKS];
static unsigned st_media_blockcount[ST_MEDIA_DISKS];

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

static int
st_media_ioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
    int unit = minor(dev);
    if (unit >= ST_MEDIA_DISKS) return ENXIO;

    switch (cmd) {
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

    if (disk < 0 || disk >= ST_MEDIA_DISKS) return (dev_t)-1;

    st_media_blocksize[disk]  = ST_MEDIA_BLOCKSIZE;
    st_media_blockcount[disk] = (unsigned)ST_MEDIA_BLOCKCOUNT + (unsigned)disk;  /* keyed, so disk 1 is a different extent */

    st_media_major[disk] = bdevsw_add(-1, &st_media_bdevsw);
    if (st_media_major[disk] < 0) {
        entry_live_write("xnu_live_rootmedia_major", (unsigned)st_media_major[disk]);
        return (dev_t)st_media_major[disk];
    }

    dev = makedev(st_media_major[disk], disk);

    entry_live_write("xnu_live_rootmedia_major", (unsigned)st_media_major[disk]);
    entry_live_write("xnu_live_rootmedia_dev",   (unsigned)dev);

    return dev;
}