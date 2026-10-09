/*
 * The payload-owned ROOT MEDIA (experiment 861) - 530 section 9's wrapping route, made buildable.
 *
 * **882 (THE HFS+ ARM): DISK 0 CAN HOLD EITHER THE MACH-O OR AN HFS+ VOLUME, AND THE SPLIT IS THE
 * SAFETY ARGUMENT.** Everything below this paragraph describes the device as 861-866 built it: disk 0
 * is `/sbin/launchd`'s own bytes and the strategy and `DKIOCGETMEMDEVINFO` agree about that, which is
 * what makes the root memory-backable and process 1 exec-able. `STAGE90_XNU_HFS_ROOT_MEDIA=1` moves
 * the STRATEGY's medium to a committed HFS+ volume (`src/entry/blob/`, the `.incbin` object) while
 * `DKIOCGETMEMDEVINFO` and the length `spec_open` caches stay on the Mach-O - because the two readers
 * serve two different rows of `vfstbllist[]` (`hfs_mountroot` reads blocks; `mockfs_mountroot` maps
 * memory), and the arm is only safe if a failed HFS mount finds mockfs still able to exec. The switch
 * is off by default, so the readings below are the baseline's; the three call sites that read it are
 * `st_media_strategy_bytes`, `st_medium_disk_base` and the two `entry_live_write`s in
 * `entry_root_media_register` that publish the pair.
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
 * **866 (rung 60): THE MEDIUM AND THE EXEC ARE NO LONGER ONE FIELD APART, BECAUSE THIS DEVICE NOW
 * SERVES THE ROOT FROM ITS OWN `strategy` INSTEAD OF HANDING `mockfs` A POINTER TO IT.**
 *
 * 865's finding was that `mockfs_mountroot` asks this device `DKIOCGETMEMDEVINFO`, and an answer with
 * `mi_mdev` true makes `mockfs_fsnode.c:333-344` map the file node straight onto `mi_base << 12` - so
 * `/sbin/launchd`'s bytes come from raw memory and **no `strategy` is ever entered**. The merge that
 * hid it is at `st_media_memdev_info` below. This rung splits the two: the answer now carries
 * `mi_mdev = 0` **for every unit, unconditionally**, so mockfs's memory-backed branch is never taken
 * and the file node's pages must come through `cluster_pagein` -> `mockfs_strategy` -> `buf_strategy`
 * -> `spec_strategy` -> this file's `st_media_strategy`. (865 section 4 proposed dropping
 * `DKIOCGETMEMDEVINFO` altogether;** that is the one shape this rung must NOT take** - `mockfs_mountroot`
 * loses its only source and the run stops at the mount. The fail-safe value is `mi_mdev = 0`: the
 * ioctl still SUCCEEDS, so `vfs_mountroot` proceeds, and only the optimisation is declined.)
 *
 * **AND THE ARM CANNOT LOSE THE EXEC, WHICH IS THE WHOLE REASON IT IS SAFE TO BUILD.** The memory the
 * device serves and the memory mockfs *would* have mapped are the SAME BYTES, for both units:
 *
 *   - disk 0 is the RAM disk: `st_medium_disk_bytes(0) == st_media_bytes()`, the array's own length,
 *     and `st_media_strategy` answers from `g_stage90_ramdisk` at `b_blkno * 512`. mockfs's memory
 *     path would have read `g_stage90_ramdisk` at `f_offset`. **Byte for byte the same array.**
 *   - disk 1 is the staged sector: the strategy answers from `st_medium_virt`, and `mi_base` still
 *     names `st_medium_virt`, so the two branches differ only in *how* the byte is fetched.
 *
 * So whichever branch mockfs takes, `/sbin/launchd`'s bytes are the same bytes and `exec_mach_imgact`
 * sees the same `MH_MAGIC`. **`b_blkno` IS IN DEVICE-BLOCK UNITS AT THIS LAYER, and that is a fact
 * read off Apple's own strategy rather than assumed**: `mdevstrategy` (`bsd/dev/memdev.c:251`) opens
 * with `blkoff = buf_blkno(bp) * mdev[devid].mdSecsize`, and `cluster_io` sets `cbp->b_blkno = blkno`
 * from `VNOP_BLOCKMAP` (`vfs_cluster.c:1700`) with `blkno = f_offset / mnt_devblocksize`
 * (`mockfs_blockmap`), so a file offset of N bytes reaches this body as block N/512. A body that
 * treated `b_blkno` as a page number would serve every page from offset 0 - a wrong answer that reads
 * like a working device, which is why the two are separated here rather than reasoned about.
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

/*
 * **882: THE HFS+ ARM'S OWN SWITCH, AND WHY IT IS NOT THE MOUNT SWITCH.**
 *
 * `STAGE90_XNU_MOUNT=1` (861) decides WHO the root device is - it makes `__wrap_mdevlookup` answer
 * this `bdevsw` instead of the real (missing) RAM disk. That arm is 866's and was pressed at rung 60:
 * with it on and this switch OFF the device is disk-0-is-the-RAM-disk, `mi_mdev = 0`, and the root's
 * file-node pages arrive through `st_media_strategy` from `g_stage90_ramdisk` - the healthy baseline.
 *
 * This switch decides WHAT IS BEHIND DISK 0, and it does so in the one place that can hold the whole
 * safety argument: `st_medium_disk_base` and `st_media_mount_bytes`. With it on, disk 0's strategy
 * serves an HFS+ volume instead of the raw Mach-O, and the two accessors are then **not equal** - the
 * strategy base is the volume and the memdev base is still the Mach-O - which is exactly the split
 * `experiment-881` section 3 says the arm cannot be safe without. A failed `hfs_mountroot` falls
 * through to `mockfs_mountroot`, which asks `DKIOCGETMEMDEVINFO` for its bytes and must still be told
 * where the Mach-O is; if the same bytes answered both, the fall-through would map the volume as
 * process 1's executable and the boot would die at `load_init_program` on a `MH_MAGIC` that is not
 * there. So the blob's byte range serves `st_medium_disk_base` and `st_media_mount_bytes` only, and
 * `g_stage90_ramdisk` keeps both `DKIOCGETMEMDEVINFO` and the length `spec_open` caches.
 *
 * It defaults to 0 so that the shipped image is byte-for-byte the rung-60 arm's, and it is a
 * SEPARATE dimension from `STAGE90_XNU_MOUNT` on purpose: a root told to be this device while disk 0
 * still holds the Mach-O is the baseline, and that combination has to stay buildable.
 */
#ifndef STAGE90_XNU_HFS_ROOT_MEDIA
#define STAGE90_XNU_HFS_ROOT_MEDIA 0
#endif

extern char g_stage90_ramdisk[];
extern char STAGE90_ROOT_MEDIA_SIZE_SYM[];

/*
 * **882: WHICH ARM COMPILED THIS FILE, AS A SYMBOL RATHER THAN A COMMENT.**
 *
 * Two different symbol NAMES rather than one symbol with two values, and that is the whole point. This
 * translation unit is compiled by `tools/build_xnu_arm_kernel.sh`'s platform block (its
 * `PLATFORM_BSD_SOURCES`), and `src/entry/build_entry.sh` links the object - **two scripts, one
 * object, and the switch has to reach both.** `XNU_KERNEL_EXTRA_DEFINES` is the hook that reaches the
 * first, so a build with the wrong one of the two environment variables set would produce an image
 * where the entry side believes it is on the HFS arm and the module serves the Mach-O - a mismatch
 * whose only symptom is a mount that fails on the device for a reason no log line names. A value read
 * out of a disassembly would be `mi4-linked-code-order-is-not-source-order` again; a NAME is a
 * property of the object, so `nm` answers it and the entry build can refuse before the link.
 */
#if STAGE90_XNU_HFS_ROOT_MEDIA
int entry_root_media_hfs_root_arm_on(void)  { return 1; }
#else
int entry_root_media_hfs_root_arm_off(void) { return 0; }
#endif

/* 903: the same marker shape for the CARD-ROOT arm (root served from the CARD unit,
 * ST_MEDIA_DRIVER), so `build_entry.sh` can refuse a card-root image whose entry side does not name
 * this contract, and so a build with the switch off is byte-for-byte the 902 arm. It depends on the
 * card unit existing (STAGE90_XNU_EMMC_STRATEGY) and on the HFS arm (the volume the card carries is
 * HFS+); the entry build refuses the impossible combinations where they would otherwise be silent. */
#if STAGE90_XNU_ROOT_FROM_CARD
int entry_root_media_cardroot_arm_on(void)  { return 1; }
#else
int entry_root_media_cardroot_arm_off(void) { return 0; }
#endif

/* 888: the same marker shape for the CARD unit, so `build_entry.sh` can refuse an image that turns
 * the card switch on while the ladder's own door (887) is not in it, and can WIDEN the strategy's
 * "never reach the device" refusal only on the arm that deliberately reaches it. */
#if STAGE90_XNU_EMMC_STRATEGY
int entry_root_media_card_arm_on(void)  { return 1; }
#else
int entry_root_media_card_arm_off(void) { return 0; }
#endif

/* 905: the same marker shape for the WRITE arm (the card unit serves B_WRITE with CMD24 and
 * DKIOCISWRITABLE answers 1 for it), so `build_entry.sh` can refuse a write image whose entry side does
 * not name this contract, and so a build with the switch off is byte-for-byte the 904 arm. */
#if STAGE90_XNU_HDD_WRITE
int entry_root_media_write_arm_on(void)  { return 1; }
#else
int entry_root_media_write_arm_off(void) { return 0; }
#endif

/* 911d: the same marker shape for the CARD-CAPACITY arm (a fourth, RAW whole-card unit whose
 * DKIOCGETBLOCKCOUNT is the card's real capacity and whose strategy addresses raw medium LBAs), so
 * `build_entry.sh` can refuse an image whose entry side does not name this contract, and so a build
 * with the switch off is byte-for-byte the 906 arm (no fourth unit, no `st_medium_card_bytes`). */
#if STAGE90_XNU_CARD_TOTAL
int entry_root_media_card_total_arm_on(void)  { return 1; }
#else
int entry_root_media_card_total_arm_off(void) { return 0; }
#endif

/* **965c: the FULL-EXTENT arm's marker, and unlike the five above it is emitted only WHEN ON.**
 * The reason is the parked arm's safety rather than a different rule: the entry link gc's nothing
 * (`build_entry.sh` links with no `--gc-sections`), so a new *unconditional* `..._off` function here
 * would add ~10 bytes to the image the parked 512 KiB arm sends and move its hash off `b9c224c0`.
 * 911b's `MEM_SIZE_MAX` marker is the precedent - a marker whose ABSENCE is "off" - and the entry
 * build reads presence as on, absence as off, refusing only a build that RECORDED the switch on
 * while this symbol is absent (a record with no artifact). With the switch off, the object and the
 * image are exactly 888's. */
#if STAGE90_XNU_FULL_EXTENT
int entry_root_media_full_extent_arm_on(void) { return 1; }
#endif

#if STAGE90_XNU_HFS_ROOT_MEDIA
/* Defined by `src/entry/blob/xnu_arm_entry_root_hfs.S` (`.incbin` of the committed HFS+ volume).
 * Only declared here, and only on this arm: with the switch off the section the object carries is
 * still linked, so these are present either way - but nothing else in this file may name them, which
 * is what the build's clause reads. */
extern char g_stage90_root_hfs[];
extern char g_stage90_root_hfs_end[];
#endif

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

/*
 * 888's switch is declared HERE, above the geometry it sizes, because it decides how many units this
 * device has. With it off the table is two entries and the object is the rung-60 arm's byte for byte;
 * with it on there is a third. (Its long-form rationale is with the card unit's state below, where
 * the reader meets the branch it guards.)
 */
#ifndef STAGE90_XNU_EMMC_STRATEGY
#define STAGE90_XNU_EMMC_STRATEGY 0
#endif

/* The units: the RAM disk (0), the one staged sector (1), and - 888 - the CARD-BACKED unit (2) whose
 * blocks the ladder's own door fetches from the medium. The card unit is a unit of its own because
 * disk 0 serves the Mach-O the root is exec'd from (866 section 4) and disk 1 serves the ONE staged
 * sector; replacing either of those would lose the exec. */
#if STAGE90_XNU_EMMC_STRATEGY && STAGE90_XNU_CARD_TOTAL
/* 911d: the CARD unit (2) plus the RAW WHOLE-CARD unit (3). Unit 2 stays the SELECTED PARTITION -
 * 903's mount is untouched - and unit 3 is the MEDIUM: `DKIOCGETBLOCKCOUNT` answers its real capacity
 * (`st_ext_sec_count`) and its strategy addresses raw LBAs with no partition base. Additive and
 * separate because the capacity clause must not move the mount rung's unit. */
#define ST_MEDIA_DISKS      4
#elif STAGE90_XNU_EMMC_STRATEGY
#define ST_MEDIA_DISKS      3
#else
#define ST_MEDIA_DISKS      2
#endif
#define ST_MEDIA_DRIVER     2u
#if STAGE90_XNU_EMMC_STRATEGY && STAGE90_XNU_CARD_TOTAL
#define ST_MEDIA_CARD_RAW   3u
/* **911d: the predicate, as a macro so the raw unit's branches FOLD AWAY when the switch is off.** A
 * bare `unit == ST_MEDIA_CARD_RAW` would not compile with the switch off (the name would be undefined)
 * and a `#if` around each use would scatter the condition; one macro is the one definition every site
 * reads. Off, it is the constant 0 - no unit is raw - so the shipped 903/905 image is unchanged. */
#define ST_MEDIA_RAW_UNIT(u) ((u) == ST_MEDIA_CARD_RAW)
#else
#define ST_MEDIA_RAW_UNIT(u) 0
#endif

/*
 * The flags this device answers `DKIOCGETMEMDEVINFO` from: ON `mdInited` (a non-inited device
 * returns ENXIO, `memdev.c:419`) and OFF `mdPhys`, copied from memdev's own choice (`mdevadd` is
 * called with `phys = 0`, `IOKitBSDInit.cpp:447`).
 *
 * **866: BOTH OF THOSE NOW DESCRIBE A FIELD `mockfs` NO LONGER BRANCHES ON, AND THAT IS DELIBERATE.**
 * The answer's `mi_mdev` is 0 for every unit (see `st_media_memdev_info`), so `mockfs`'s
 * memory-backed branch is never taken and `mi_phys` is never read by anything: mockfs's guard
 * (`mockfs_vfsops.c:105`) sits *inside* the branch that decides to use the answer at all. The two
 * flags are kept set as before so that this device still reports itself as an inited, non-physical
 * memory device to any *other* reader, and so that the one value this rung moves is one value.
 */
#define ST_MEDIA_MDINITED  0x1u
#define ST_MEDIA_MDPHYS    0x2u
/* 864: this device's medium is the STAGED sector, not the RAM disk - so `st_media_memdev_info`
 * answers with the staged array's address and the handler below is the one that filled it. */
#define ST_MEDIA_STAGED    0x4u
/* 888: this device's medium is the CARD, reached a block at a time through the ladder's own door
 * (`entry_storage_driver_read`). Unlike the staged unit it holds no copy - the bytes are fetched
 * when the strategy is called - so it has no `st_medium_virt` and its base accessor returns 0 to
 * say "not addressable as one array" rather than an address it cannot honour. */
#define ST_MEDIA_CARD      0x8u

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

/*
 * ===================================================================================================
 * 888 - THE DRIVER UNIT (unit 2): THE STRATEGY THAT MOVES A BYTE OFF THE CARD.
 *
 * This is the join 867 section 3 mapped and 887 built the ladder half of: `st_media_strategy` is given
 * the ladder's own read (`entry_storage_driver_read`) and fetches the block it was handed from the
 * MEDIUM. 887 found why it is a UNIT OF ITS OWN rather than a change to disk 0's strategy: disk 0
 * serves `g_stage90_ramdisk` - the Mach-O the root is exec'd from - `precisely` so `exec_mach_imgact`
 * sees a valid `MH_MAGIC` (866 section 4). A disk-0 strategy that read the card would serve the
 * selected partition's bytes where the exec expects the Mach-O, and the run would lose the exec - the
 * one thing the whole mount track protects. Disk 1 (the staged unit) is likewise one sector. So the
 * card-backed medium is a THIRD unit and neither of the first two moves.
 *
 * **IT CANNOT USE `st_medium_disk_base`, AND THAT IS THE STRUCTURAL DIFFERENCE FROM UNITS 0 AND 1.**
 * Those two media are contiguous arrays in this kernel's own mapping, so the strategy copies
 * `size` bytes from `base + blkno*512` in one `bcopy`. The card is not addressable as one array: the
 * strategy may be handed a block anywhere in a multi-hundred-megabyte partition, and the only way to
 * get those bytes is to ask the ladder for THAT block. So this unit serves the strategy a block at a
 * time - `entry_storage_driver_read(lba)` -> the ladder's `st_read_block` - and `st_medium_disk_base`
 * returns 0 for it (the "not one array" answer) so `DKIOCGETMEMDEVINFO` and the accessor tell a reader
 * the truth about a medium that is not memory.
 *
 * **THE SWITCH IS OFF BY DEFAULT, AND IT IS A SEPARATE DIMENSION FROM `STAGE90_XNU_MOUNT`.** With it
 * off the shipped image is byte-for-byte the rung-60 arm's: unit 2 is not registered, and the two
 * accessors answer exactly as they did. It is a `#ifndef` switch and not `STAGE90_XNU_STORAGE_PROBE`
 * because this file is compiled by the KERNEL build, whose environment does not carry the ladder's
 * probe value - a value this file read out of the environment would be `mi4-build-variant-comes-from-
 * an-env-default` in its most direct form. `src/entry/build_entry.sh` refuses a build that turns it on
 * while the ladder's own door is not in the image (see `xnu_entry_888`), so the two stay in step.
 */
#ifndef STAGE90_XNU_EMMC_STRATEGY
#define STAGE90_XNU_EMMC_STRATEGY 0
#endif

/*
 * ===================================================================================================
 * 903 - THE CARD-ROOT ARM: THE ROOT DEVICE IS THE CARD UNIT, NOT THE RAM BLOB.
 *
 * 902 closed "the HFS+ port mounts and execs process 1" but its volume was the committed RAM blob
 * `g_stage90_root_hfs` linked into the payload. This switch points the root at what the device's own
 * storage holds. It is a ONE-LINE redirection of `__wrap_mdevlookup`'s answer (from
 * `entry_root_media_register(devid)`, disk 0 = the blob, to `entry_root_media_register_card()`, disk 2
 * = the selected partition served block-by-block from the card), because EVERYTHING ELSE IS ALREADY IN
 * PLACE: the card unit exists (888, proven by 892's `_card_lba=0x400000`), its geometry is the
 * selection's, and its strategy already computes `lba = selected_lba + off/512 + i` and fetches each
 * block through `entry_storage_driver_read`.
 *
 * **WHY THE FALL-THROUGH SURVIVES WITHOUT TOUCHING `mi_mdev`.** `mockfs_mountroot` (mockfs_vfsops.c)
 * does not decline a non-memory device: it reads `DKIOCGETMEMDEVINFO` and sets
 * `mockfs_memory_backed = mi_mdev`, then mounts either way. So `st_media_memdev_info` keeps its 902
 * answer (`mi_mdev = 1` on the HFS arm, from the FILE-SCOPE geometry, which the blob/disk-0
 * registration set) and mockfs memory-backs `g_stage90_ramdisk` - while DISK 2 (the card, which this
 * arm returns) is served by `st_media_strategy`'s card branch, since only `mi_mdev` from the card
 * unit's own `DKIOCGETMEMDEVINFO` would memory-back the card and that call never happens for the root
 * device here (mockfs gets the FILE-SCOPE answer). HFS reads the card's volume by block; if it fails,
 * `vfs_mountroot`'s next row is mockfs, which maps the Mach-O - both paths exec.
 *
 * **THE MEDIUM MUST BE HFS+.** XNU 4570 has no ext2/3/4 client (only HFS+ is ported: 885), and the
 * selected partition (`userdata`) carries ext4 today (884/890), so this arm mounts only once an HFS+
 * volume exists at the partition's start. Building that volume (the 903 host tool) and writing it
 * (the one destructive step, the operator's) are separate acts; with no HFS+ there HFS's `BTOpenPath`
 * fails, the row falls through to mockfs, and the run is the 902 arm - a mount that reads no card block
 * and the RAM-blob root, not a brick.
 *
 * The switch is a `#ifndef` for `STAGE90_XNU_EMMC_STRATEGY`'s reason (this file is compiled by the
 * KERNEL build, whose environment does not carry the entry build's resolved value). `build_entry.sh`
 * refuses a card-root image whose module did not compile this arm (a `nm` marker, 882's shape) and an
 * arm that is on without the card unit and the HFS volume it needs.
 */
#ifndef STAGE90_XNU_ROOT_FROM_CARD
#define STAGE90_XNU_ROOT_FROM_CARD 0
#endif

/* **905: THE WRITE HALF.** With `STAGE90_XNU_HDD_WRITE=1` the card unit accepts `B_WRITE` and serves it
 * with a CMD24 through the ladder's write door (`entry_storage_driver_write`); with it off, every unit
 * refuses a write with `EROFS`, exactly as 888's arm did. The switch is checked to be 0 or 1 by
 * `build_entry.sh` (the same discipline ROOT_FROM_CARD gets) and REQUIRES the card strategy: a write
 * onto a unit with no ladder beneath it would be a store to a `NULL`/absent path. */
#ifndef STAGE90_XNU_HDD_WRITE
#define STAGE90_XNU_HDD_WRITE 0
#endif
#if STAGE90_XNU_HDD_WRITE && !STAGE90_XNU_EMMC_STRATEGY
#error "STAGE90_XNU_HDD_WRITE=1 needs STAGE90_XNU_EMMC_STRATEGY=1: the write path IS the card unit's, and a unit with no ladder beneath it has no write door to call."
#endif

/* **911d: THE CAPACITY HALF.** With `STAGE90_XNU_CARD_TOTAL=1` the module registers a fourth, RAW unit
 * (`ST_MEDIA_CARD_RAW = 3`) whose medium is the WHOLE CARD: `DKIOCGETBLOCKCOUNT` answers the real
 * capacity the ladder already read (`st_ext_sec_count`, EXT_CSD SEC_CNT) and its strategy addresses raw
 * LBAs - block N of the medium is LBA N, with no `entry_storage_selected_lba()` base - so an OS reader
 * sees the 16/32 GB the card is, not the selected partition's extent. Additive: unit 2 (903's mount
 * unit) is UNTOUCHED, so the mount rung's readings do not move. It REQUIRES the card strategy for the
 * same reason HDD_WRITE does: its bytes come through the ladder's door, and a capacity with no door
 * beneath it would report a size it cannot serve. */
#ifndef STAGE90_XNU_CARD_TOTAL
#define STAGE90_XNU_CARD_TOTAL 0
#endif
#if STAGE90_XNU_CARD_TOTAL && !STAGE90_XNU_EMMC_STRATEGY
#error "STAGE90_XNU_CARD_TOTAL=1 needs STAGE90_XNU_EMMC_STRATEGY=1: the raw whole-card unit is served through the card ladder's door, and a capacity with no door beneath it would report a size it cannot address."
#endif

/* **965c: THE FULL-EXTENT ARM.** With `STAGE90_XNU_FULL_EXTENT=1` the CARD unit's byte length
 * (`st_medium_disk_bytes(ST_MEDIA_DRIVER)`) is computed in 64 bits, so the strategy bounds the WHOLE
 * selected partition (`userdata` = 13,610,499,072 B on this device) instead of its low 32 bits (692
 * MiB). This is the one thing standing between the port and the goal's real rootfs: the decrypted
 * iOS 7.1.2 volume is 896 MiB (965), and 896 > 692, so without this arm the strategy answers EOF for
 * the volume's top ~204 MiB and `hfs_mountroot` cannot read its catalog. It is a SEPARATE switch from
 * `CARD_TOTAL` and not a value on it: `CARD_TOTAL` is about a fourth unit's CAPACITY reading, this is
 * about the mount unit's ADDRESSABLE LENGTH - one name, one meaning. It requires the card strategy
 * for the same reason the two above do, and with it OFF the module returns 888's exact `(unsigned)`
 * value, so the shipped 512 KiB arm's object is byte-for-byte what it was. */
#ifndef STAGE90_XNU_FULL_EXTENT
#define STAGE90_XNU_FULL_EXTENT 0
#endif
#if STAGE90_XNU_FULL_EXTENT && !STAGE90_XNU_EMMC_STRATEGY
#error "STAGE90_XNU_FULL_EXTENT=1 needs STAGE90_XNU_EMMC_STRATEGY=1: the length it widens is the card unit's, and a unit with no ladder beneath it has no partition to bound."
#endif


#if STAGE90_XNU_EMMC_STRATEGY
/* The ladder's door and its two addressing accessors - 887's exported half, in the SAME image when
 * this switch is on (the entry image links `entry_storage.c` and this object together; see
 * `xnu_entry_888`). Declared here rather than in a header because the ladder's own header
 * (`entry_storage.h`) is in the entry component's include set and this file is in the BSD one; the
 * names are the interface, and `build_entry.sh` refuses a build where they do not resolve. */
extern const uint32_t *entry_storage_driver_read(uint32_t lba);
extern uint32_t        entry_storage_selected_lba(void);
extern uint32_t        entry_storage_selected_count(void);
/* **911d: the WHOLE CARD's sector count (EXT_CSD SEC_CNT), for the raw unit that reports capacity
 * rather than a partition.** Declared in the same block as the two above, for the same reason: it is
 * part of the ladder's exported interface, resolved from `entry_storage.c` in the same image, and a
 * build that does not resolve it is refused by the linked-image clause rather than calling a stub. */
extern uint32_t        entry_storage_card_sectors(void);
#if STAGE90_XNU_HDD_WRITE
/* **905: the mirror door.** `entry_storage_driver_write(lba, w)` hands 128 words (one 512-byte block)
 * to the card by CMD24; it returns the ladder's own `st_write_block` so the caller can read back what
 * the command left. It is declared in the SAME block as the read door, for the same reason: the two
 * doors are one interface, and a build that resolves one and not the other is refused by the linked-
 * image clause `xnu_entry_905` rather than silently calling a linker stub. */
extern const uint32_t *entry_storage_driver_write(uint32_t lba, const uint32_t *w);
#endif
/* Defined beside `entry_root_media_stage` (which calls it), so the declaration precedes that body. */
int entry_root_media_register_card(void);
#if STAGE90_XNU_CARD_TOTAL
/* 911d: the raw whole-card unit's registration, declared beside the card unit's for the same reason. */
int entry_root_media_register_card_raw(void);
#endif
#endif

/* The two `.bss` cells the strategy's refusals are counted in, so a served read and a refused one
 * are told apart by a number in the log rather than by the absence of a log line. */
static uint32_t st_medium_served;
static uint32_t st_medium_refused;
/* **905's two counters.** `st_medium_write_refused` counts the `EROFS` refusals the guard above
 * issues (with HDD_WRITE=0 that is every write; with it on, every write to a RAM-backed unit);
 * `st_medium_write_served` counts the writes the card unit actually handed to the ladder. Together
 * they make "the arm refused" and "the write ran" two numbers in the log rather than an absence. */
static uint32_t st_medium_write_refused;
static uint32_t st_medium_write_served;
#if STAGE90_XNU_HDD_WRITE
/* **905: the one 512-byte staging buffer the write branch hands to the ladder.** The ladder's write
 * door takes a `const uint32_t *` of exactly one block (128 words, its own `ST_EXT_CSD_WORDS`); this
 * file does not include the ladder's header (see the extern block), so the word count is spelled HERE
 * and the linked-image clause `xnu_entry_905` is what keeps the two in agreement - a staging array
 * whose length did not match the door's would move fewer bytes than the block the caller named, and
 * the mount would read back a partly-stale block and call it written. */
#define ST_LADDER_WRITE_WORDS  128u                      /* 512 / 4 */
static uint32_t st_write_stage[ST_LADDER_WRITE_WORDS];
#endif

static unsigned
st_media_bytes(void)
{
    return (unsigned)((uintptr_t)STAGE90_ROOT_MEDIA_SIZE_SYM - (uintptr_t)g_stage90_ramdisk);
}

/* The Mach-O's own length, which is `st_media_bytes` - named separately for the callers that want
 * "the bytes `mockfs` must be told about" rather than "the bytes the strategy serves", which are the
 * same number on every arm but 882's. The two names are the point: a call site that is handed
 * `st_media_bytes()` reads as a claim about mockfs, and after 882 that claim is false. */
#define st_media_mount_bytes() st_media_bytes()

/*
 * **882: THE BYTES DISK 0's STRATEGY SERVES, WHICH IS THE BLOB WHEN THE HFS ARM IS ON.**
 *
 * Every other reader of this device - `DKIOCGETMEMDEVINFO`, `si_devsize` through
 * `DKIOCGETBLOCKCOUNT` at `spec_open`, `st_media_size` - stays on the Mach-O, and that is deliberate
 * rather than incomplete: those readers are mockfs's, and mockfs is the fall-through the arm's safety
 * rests on. Only `st_medium_disk_base` and the strategy's bound ask this question, because only they
 * are the filesystem's path. See the switch's comment above for why the split is load-bearing.
 *
 * It is a `noinline` function, not an `#if` inside the accessor, so a build clause can read which
 * constant each arm returns BY VALUE (`mi4-linked-code-order-is-not-source-order`: a clause reading
 * the linked disassembly to prove an ORDER reads LAYOUT, and the linker's tail-merge refuted 855).
 */
__attribute__((noinline)) static unsigned
st_media_strategy_bytes(void)
{
#if STAGE90_XNU_HFS_ROOT_MEDIA
    return (unsigned)((uintptr_t)g_stage90_root_hfs_end - (uintptr_t)g_stage90_root_hfs);
#else
    return st_media_bytes();
#endif
}

/*
 * **THE TWO ACCESSORS THE STRATEGY IS WRITTEN AGAINST, SO ITS BODY READS AS ONE RULE INSTEAD OF A
 * `switch`, AND SO THERE IS EXACTLY ONE DEFINITION OF WHAT EACH UNIT'S MEDIUM IS.**
 *
 * The rule is the handover, and there is only one of it: **a unit whose medium has been staged IS the
 * staged sector; every other unit is the RAM disk.** That is the same fact `DKIOCGETMEMDEVINFO`
 * branches on (`ST_MEDIA_STAGED`), read from the same cell, so the two halves of this device cannot
 * come to disagree about which medium unit 1 has. **A per-unit SIZE ARRAY WAS THE FIRST DRAFT AND IT
 * WAS WRONG**: it needed a third cell to be kept in step with `st_media_blockcount[]` and
 * `st_medium_pages`, and a strategy whose base and length disagreed about which medium they described
 * would serve bytes from a `.bss` array that is 512 long at offsets the length test believed were the
 * RAM disk's. Deriving both from `ST_MEDIA_BLOCKSIZE` - the same constant `entry_root_media_stage`
 * sets `st_media_blockcount[1]` to `1` of - makes `length == blockcount * blocksize` true by
 * construction for the staged unit rather than by two edits that happen to agree.
 *
 * **Zero is the one answer that means "no medium at all"**, and it is what `st_medium_disk_bytes`
 * returns for a unit number this device does not own (`unit >= ST_MEDIA_DISKS`) - the only way the
 * strategy can be reached with nothing behind it. A unit whose medium is not staged is not a hole: it
 * answers the RAM disk's length, which is the medium it really does have at that moment (the same one
 * 862 gave it, reached by the same address). `base` is a plain address: both media are already
 * addressable (`mdPhys == 0`), so no `<< 12` appears here and none should - that shift turns a page
 * NUMBER into an address, and there is no page number to shift.
 */
static const uint8_t *
st_medium_disk_base(uint32_t unit)
{
#if STAGE90_XNU_EMMC_STRATEGY
    /* 888: the card unit is NOT one array - the strategy fetches it block by block - so there is no
     * base to return and 0 says exactly that. It is the same "no medium at all" answer a unit past
     * the table gets, and the strategy treats both identically, because neither can be served from
     * `base + offset`. */
    if (unit == ST_MEDIA_DRIVER)
        return 0;
#if STAGE90_XNU_CARD_TOTAL
    if (ST_MEDIA_RAW_UNIT(unit))
        return 0;                  /* 911d: fetched block by block, same as the card unit - no array */
#endif
#endif
    if (unit == 1u && st_medium_staged != 0u)
        return (const uint8_t *)st_medium_virt;
#if STAGE90_XNU_HFS_ROOT_MEDIA
    return (const uint8_t *)g_stage90_root_hfs;   /* 882: the volume, not the Mach-O - see above */
#else
    return (const uint8_t *)g_stage90_ramdisk;
#endif
}

#if STAGE90_XNU_EMMC_STRATEGY && STAGE90_XNU_CARD_TOTAL
/*
 * **911d: the whole card in BYTES, 64-bit, because 16 GB does not fit a 32-bit `unsigned`.** The
 * strategy's `len` bounds a byte offset, and the existing accessors return `unsigned` - which is exact
 * for the RAM disk and for the selected partition (both < 4 GiB), but TRUNCATES the raw unit's
 * `card_sectors * 512` (`0x01d5a000 * 512` = ~15.76 GB). A `len` that disagreed with the block count
 * `DKIOCGETBLOCKCOUNT` reports would be exactly the "one value, two definitions" defect this file
 * keeps re-learning: the disk would claim 30.7M blocks and serve only the low 4 GiB of them. So the raw
 * unit's length is computed HERE in 64 bits and the strategy uses a 64-bit `len` on this arm. */
static uint64_t
st_medium_card_bytes(void)
{
    return (uint64_t)entry_storage_card_sectors() * (uint64_t)ST_MEDIA_BLOCKSIZE;
}
#endif

static unsigned
st_medium_disk_bytes(uint32_t unit)
{
    if (unit >= ST_MEDIA_DISKS)
        return 0u;
#if STAGE90_XNU_EMMC_STRATEGY
    /* 888: the card unit's byte length is the SELECTED partition's - `selected_count` sectors of
     * `ST_MEDIA_BLOCKSIZE` bytes each. Deriving it from the ladder's own selection (rather than a
     * second constant) keeps the device's length and the ladder's addressing the same number, which
     * is the "one value, two definitions" rule this file keeps re-learning: a strategy whose length
     * and whose addressing disagreed would serve a byte range that is not what was selected.
     *
     * **965c: THIS `(unsigned)` IS A 32-BIT WALL, AND IT IS LEFT EXACTLY AS 888 WROTE IT.** `userdata`
     * (`mmcblk0p25`) is 13,610,499,072 B, and `(unsigned)` of that is 725,597,184 B (692 MiB) - a
     * length below the decrypted iOS 7.1.2 rootfs's 896 MiB, so a strategy bounded here would serve
     * `EOF` for the volume's top ~204 MiB. Rather than change this function's return TYPE (which would
     * move the `.o` even for a build that does not use the arm - verified: it did, at byte 1858),
     * 965c adds `st_medium_card_full_bytes()` BELOW, under `STAGE90_XNU_FULL_EXTENT`, and the strategy
     * picks it for the card unit on that arm. With the switch off this body is byte-for-byte 888's. */
    if (unit == ST_MEDIA_DRIVER)
        return (unsigned)((uint64_t)entry_storage_selected_count() * ST_MEDIA_BLOCKSIZE);
#if STAGE90_XNU_CARD_TOTAL
    /* 911d: the RAW unit's byte length is the WHOLE CARD's - the sector count the ladder read from
     * EXT_CSD - not the selected partition's. This is the number the 「16GB/32GB存储」 clause turns on:
     * an OS reader asking this unit's size gets the card, and a 32 GB part reads ~2x this value. */
    if (ST_MEDIA_RAW_UNIT(unit))
        return st_medium_card_bytes();
#endif
#endif
    if (unit == 1u && st_medium_staged != 0u)
        return ST_MEDIA_BLOCKSIZE;        /* the ONE sector 864 handed over */
    return st_media_strategy_bytes();     /* 882: the volume on the HFS arm, the Mach-O without it */
}

#if STAGE90_XNU_FULL_EXTENT
/*
 * **965c: the card unit's length in 64 BITS - the one thing between this port and the real iOS rootfs.**
 * The CARD mount unit (ST_MEDIA_DRIVER) is the selected partition (`userdata`, 13,610,499,072 B on this
 * device), and `st_medium_disk_bytes()` above returns its 32-bit product = 725,597,184 B (692 MiB). The
 * decrypted iOS 7.1.2 rootfs (965) is **896 MiB**; 896 > 692, so a strategy bounded at the 32-bit value
 * serves `EOF` for the volume's top ~204 MiB and `hfs_mountroot` cannot read its catalog. This function
 * is that bound in 64 bits, `entry_storage_selected_count() * ST_MEDIA_BLOCKSIZE`, the SAME product 888
 * computes - the width is the only difference, so the length and the ladder's addressing stay one number
 * ([[mi4-one-value-two-definitions]]). It is a SEPARATE function, not a change to the accessor's return
 * type, so the arm-OFF object keeps 888's exact bytes; the strategy picks THIS for the card unit under
 * the switch (see its `len` computation). `CARD_TOTAL` is unrelated: that widens a fourth unit's
 * CAPACITY reading, this the mount unit's ADDRESSABLE LENGTH - one arm, one switch each.
 */
static uint64_t
st_medium_card_full_bytes(void)
{
    return (uint64_t)entry_storage_selected_count() * (uint64_t)ST_MEDIA_BLOCKSIZE;
}
#endif

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
 * inventing bytes. 864 replaced it with a body that served the ONE sector the entry image holds; **866
 * (rung 60) makes it serve the root's medium as well, and that is the rung this body is now about.**
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
 * **866 (rung 60): IT SERVES, AND FROM HERE THE ROOT ITSELF ARRIVES THROUGH IT.**
 *
 * Disk 0 is the entry image's RAM disk - `/sbin/launchd`'s own bytes, the array `mdevadd` was given
 * in 468 - and disk 1 is the sector 864 staged. Until this rung the strategy only ever answered
 * about disk 1, because `mi_mdev` true kept mockfs on the memory-backed path for the root; now that
 * the answer is `mi_mdev = 0` for every unit, **the root's file-node pages arrive here too**, and
 * the body below is what serves them.
 *
 * **IT IS `mdevstrategy`'s ARITHMETIC, WITH THE ONE DIFFERENCE THAT MATTERS AND NO OTHER.**
 * Apple's body (`bsd/dev/memdev.c:235-345`) computes `blkoff = buf_blkno(bp) * mdSecsize` and reads
 * `(mdBase << 12) + blkoff`; that `<< 12` exists because the RAM disk's daemon stores its base as a
 * PAGE NUMBER. **Here both media are already addressable** - `g_stage90_ramdisk` is a label in this
 * kernel's mapping and `st_medium_virt` is a `.bss` array - which is exactly `mdPhys == 0`, so the
 * base is a virtual address and the block offset is added to it directly. **`ST_MEDIA_BLOCKSIZE` is
 * the `mdSecsize` of that product**: the block number this body is handed is in 512-byte units
 * because `cluster_io` takes it from `mockfs_blockmap`'s `b_blkno = f_offset / blksize` with
 * `blksize = mnt_devblocksize = DKIOCGETBLOCKSIZE = 512`. **That is the fact that makes these two
 * media the SAME MEDIUM the memory-backed path would have mapped** - the file byte at offset N comes
 * from the same address whichever branch ran - and it is read off Apple's own strategy rather than
 * assumed, because the one wrong reading here (treating `b_blkno` as a page) still returns data and
 * still looks like a working device.
 *
 * **THE TRANSFER IS BOUNDED BY FOUR INDEPENDENT TESTS, AND THEY ARE FOUR BECAUSE THEY FAIL
 * DIFFERENTLY.** A unit with no medium (`st_medium_disk_bytes` == 0: disk 1 before the ladder has
 * staged, or a unit number past the two) is "there is no medium at all" -> `ENXIO`; a request at or
 * past the end is the EOF case and returns with the residual equal to the count, exactly as
 * `mdevstrategy` does; a request that runs past the end is trimmed to the end rather than refused
 * whole; and a write is `EROFS`, because neither medium is this image's to write. **The `EINVAL` the
 * old body returned for `blkno != 0` is gone with the reasoning that produced it**: at 864 the device
 * owned exactly one sector, so any other block was out of the medium - and a root that must supply a
 * whole executable is not one sector, so that test would have refused the very read this rung exists
 * to serve.
 */
extern void bcopy(const void *from, void *to, size_t len);   /* `bsd/dev/memdev.c` calls it too */

static void
st_media_strategy(struct buf *bp)
{
    caddr_t vaddr;
    uint32_t unit = (uint32_t)minor(buf_device(bp));
    uint32_t count = (uint32_t)buf_count(bp);
    const uint8_t *base;
#if STAGE90_XNU_EMMC_STRATEGY && (STAGE90_XNU_CARD_TOTAL || STAGE90_XNU_FULL_EXTENT)
    uint64_t len;                     /* 911d: 64-bit - the raw whole-card unit's length exceeds 4 GiB;
                                       * 965c: and so does the card mount unit's once FULL_EXTENT is on */
#else
    unsigned len;
#endif
    uint64_t off;                     /* 512-byte blocks * 512 - 64-bit because a block number needs it */

    st_medium_refused++;
    entry_live_write("xnu_live_rootmedia_refused", st_medium_refused);
    entry_live_write("xnu_live_rootmedia_strategy_dev", (unsigned)buf_device(bp));
    entry_live_write("xnu_live_rootmedia_strategy_blkno", (unsigned)buf_blkno(bp));
    entry_live_write("xnu_live_rootmedia_strategy_count", (unsigned)count);
    entry_live_write("xnu_live_rootmedia_strategy_read",
                     (buf_flags(bp) & B_READ) ? 1u : 0u);

    /*
     * **866: THE CELL THAT SAYS WHICH MEDIUM THIS CALL WAS ABOUT.** With `mi_mdev = 0` the root's own
     * file-node pages arrive here, so a run must be able to tell a disk-0 request (the RAM disk - the
     * root) from a disk-1 one (the staged sector, which nothing reads yet). `_strategy_medium` is 0
     * for the RAM disk and 1 for the staged sector, derived from the same `st_medium_staged` cell the
     * accessors and `DKIOCGETMEMDEVINFO` all read - **one definition of "which medium unit 1 has",
     * published rather than re-derived**, because a second spelling here is how the base and the
     * length would come to disagree. What the bounds below used is not published again: it is the same
     * `st_media_bytes()`/`ST_MEDIA_BLOCKSIZE` pair `DKIOCGETMEMDEVINFO` already answers with, and a
     * second cell would be the second spelling this file exists to avoid.
     */
    entry_live_write("xnu_live_rootmedia_strategy_medium",
                     (unit == 1u && st_medium_staged != 0u) ? 1u : 0u);

    if (unit >= ST_MEDIA_DISKS) {
        buf_seterror(bp, ENXIO);
        buf_biodone(bp);
        return;
    }
    #if STAGE90_XNU_EMMC_STRATEGY && (STAGE90_XNU_CARD_TOTAL || STAGE90_XNU_FULL_EXTENT)
    /* 911d: compute this unit's byte length in 64 bits so the raw unit's card total is not truncated,
     * and keep every other unit's value byte-for-byte what `st_medium_disk_bytes` returns.
     * 965c adds the OTHER 64-bit case: on a `FULL_EXTENT` arm the CARD mount unit's own length exceeds
     * 32 bits (userdata is 12.68 GiB), so it too is computed here rather than through the 32-bit
     * `st_medium_disk_bytes`. The two switches are independent - one widens unit 3's capacity reading,
     * the other unit 2's mount length - so each has its OWN 64-bit source and neither implies the
     * other; the `else` chain falls through to the accessor for units 0/1 unchanged. */
#if STAGE90_XNU_CARD_TOTAL
    if (ST_MEDIA_RAW_UNIT(unit))
        len = st_medium_card_bytes();
    else
#endif
#if STAGE90_XNU_FULL_EXTENT
    if (unit == ST_MEDIA_DRIVER)
        len = st_medium_card_full_bytes();
    else
#endif
        len = (uint64_t)st_medium_disk_bytes(unit);
#else
    len  = st_medium_disk_bytes(unit);
#endif
    base = st_medium_disk_base(unit);
#if STAGE90_XNU_EMMC_STRATEGY
    /* **`len == 0` IS THE "NO MEDIUM" TEST, AND IT IS THE ONLY ONE.** Before 888 this guard also
     * refused `base == 0`, which was true of every unit that had a medium. The card unit breaks that
     * equivalence on purpose: it HAS a medium (the selected partition) and no base, because its bytes
     * are fetched a block at a time rather than copied from one array. So the base test moves BELOW
     * the card branch, where it is again exactly what it says - "a unit with a medium this body cannot
     * serve from one array". **With the switch off this whole restructure is not compiled**, so the
     * shipped object is the rung-60 arm's byte for byte (the build's clause checks exactly that). */
    if (len == 0u) {                              /* no medium: disk 1 before the ladder staged */
        buf_seterror(bp, ENXIO);
        buf_biodone(bp);
        return;
    }
#else
    if (len == 0u || base == 0) {                 /* no medium: disk 1 before the ladder staged */
        buf_seterror(bp, ENXIO);
        buf_biodone(bp);
        return;
    }
#endif
    /* **905: THE WRITE REFUSAL IS NOW PER-UNIT.** 888's arm refused every `B_WRITE` with `EROFS`,
     * because no unit was this image's to write. This rung makes the CARD unit (ST_MEDIA_DRIVER)
     * writable and leaves the two RAM-backed units refusing exactly as before - so the guard tests the
     * UNIT and not merely the direction. With `STAGE90_XNU_HDD_WRITE=0` the condition folds to the old
     * one at compile time (`unit == ST_MEDIA_DRIVER` is still true, so a write to the card is refused
     * too), and nothing about the shipped 903/904 arm moves. `st_medium_write_refused` counts the
     * refusals so the log can tell "the arm refused" from "no write was attempted at all". */
    if ((buf_flags(bp) & B_READ) == 0) {
#if STAGE90_XNU_HDD_WRITE
        if (unit != ST_MEDIA_DRIVER) {
#endif
            st_medium_write_refused++;
            entry_live_write("xnu_live_rootmedia_write_refused", st_medium_write_refused);
            buf_seterror(bp, EROFS);
            buf_biodone(bp);
            return;
#if STAGE90_XNU_HDD_WRITE
        }
#endif
    }

    off = (uint64_t)(uint32_t)buf_blkno(bp) * ST_MEDIA_BLOCKSIZE;   /* mdSecsize's product */
    /* The one reading a run must have: WHICH offset of the medium was asked for. Published before
     * the bounds are applied, so a refused request names the offset it was refused at. */
    entry_live_write("xnu_live_rootmedia_strategy_offset", (unsigned)off);

    if (off >= (uint64_t)len) {
        /* `mdevstrategy`'s EOF rule, and it is not an error: reading AT the end returns nothing and
         * leaves the residual equal to the count. A genuine over-read (past the end) is EINVAL. */
        if (off > (uint64_t)len) {
            buf_seterror(bp, EINVAL);
        }
        buf_biodone(bp);
        return;
    }
    if (off + (uint64_t)count > (uint64_t)len)
        count = (uint32_t)((uint64_t)len - off);   /* trim to the end rather than refuse whole */

#if STAGE90_XNU_EMMC_STRATEGY
    /*
     * **888: THE CARD UNIT IS SERVED BLOCK BY BLOCK, AND THAT IS THE WHOLE OF THE JOIN.** Units 0 and
     * 1 are arrays in this kernel's mapping, so one `bcopy` answers any request; the card is not. The
     * strategy may be handed a block anywhere in the selected partition, so each 512-byte block is
     * fetched by ITS OWN LBA through the ladder's door, `entry_storage_driver_read`, and copied into
     * the caller's map.
     *
     * **THE COUNT CAN EXCEED ONE BLOCK AND THAT IS WHY THIS IS A LOOP RATHER THAN ONE CALL.** A
     * filesystem reads a cluster at a time; the ladder's door moves exactly one 512-byte block (its
     * `st_read_single_block(1u)`), so a request for N blocks is N door calls, the dest advancing by a
     * block and the LBA by one each time. `off / ST_MEDIA_BLOCKSIZE` is the requesting block's number
     * *on this disk*; the block's LBA on the MEDIUM is that plus the selected partition's base, and
     * the base-plus-number sum is computed in 64 bits because the sum, not either addend, is what a
     * multi-hundred-megabyte partition's LBA can overflow a 32-bit cell with.
     *
     * **THE MAP IS TAKEN ONCE, BEFORE THE FIRST FETCH.** `buf_map` is not free and re-mapping per
     * block would be a second place for the transfer to fail; the door returns `st_read_block`, whose
     * contents are overwritten by the NEXT call, so each block MUST be copied out before the next door
     * call - which the loop's order makes true by construction (copy, then advance, then fetch again).
     */
    if (unit == ST_MEDIA_DRIVER || ST_MEDIA_RAW_UNIT(unit)) {
        uint32_t nblk, i;
        /* **911d: THE ONE NUMBER THAT DIFFERS BETWEEN THE PARTITION UNIT AND THE RAW UNIT.** Block N of
         * the SELECTED PARTITION is LBA `selected_lba() + N`; block N of the RAW unit IS the medium's
         * LBA N. So the raw unit's base is 0 and unit 2's is the selection's - and every LBA below is
         * `base_lba + blkno + i`, ONE expression with one definition, so a raw read and a partition read
         * cannot drift apart. On the partition unit this is 903's number unchanged (`base_lba ==
         * selected_lba()`), which is why 903's readings do not move when this unit is added. */
        uint32_t base_lba = (unit == ST_MEDIA_DRIVER) ? entry_storage_selected_lba() : 0u;

        off = (uint64_t)(uint32_t)buf_blkno(bp) * ST_MEDIA_BLOCKSIZE;
        entry_live_write("xnu_live_rootmedia_card_off", (unsigned)off);
        if (buf_map(bp, &vaddr) != 0) {
            buf_seterror(bp, EFAULT);
            buf_biodone(bp);
            return;
        }
        nblk = count / ST_MEDIA_BLOCKSIZE;              /* whole blocks in the (already trimmed) count */
#if STAGE90_XNU_HDD_WRITE
        /*
         * **905: THE SAME LOOP, THE OTHER DIRECTION, AND THAT IS THE WHOLE OF THE WRITE BRANCH.**
         * The read branch above fetches each LBA through `entry_storage_driver_read` and copies OUT of
         * the ladder's buffer; this one copies the caller's map INTO a local block and hands it to
         * `entry_storage_driver_write`, which issues CMD24 at the same LBA the read branch would have
         * issued CMD17 at. **The destination LBA arithmetic is IDENTICAL - `selected_lba + blkno + i`
         * - and that is not a coincidence to be trusted but a property to be read**: a write that
         * landed at an LBA the read would not ask for is the one failure of this rung that a mount
         * would not surface, because HFS+ would then read back its own stale bytes and call them
         * clean. `st_media_ioctl`'s `DKIOCISWRITABLE` already answered 1 for this unit (943), which is
         * the byte the mount reads to choose rw; with `HDD_WRITE=0` this arm is not compiled and the
         * `EROFS` guard above refuses the write instead, so the two answers cannot disagree.
         *
         * **ONE LOCAL BUFFER, 128 words, refilled and handed over per block** - the ladder's write door
         * returns ITS OWN buffer (`st_write_block`), so the caller's map must not be handed in directly
         * (it is not necessarily 4-byte-aligned across the whole count, and the door writes words out
         * one at a time). The copy is one 512-byte block, the same size the read branch copies.
         */
        if ((buf_flags(bp) & B_READ) == 0) {
            for (i = 0u; i < nblk; i++) {
                uint32_t lba = (uint32_t)((uint64_t)base_lba
                                          + (off / ST_MEDIA_BLOCKSIZE) + i);
                uint32_t j;
                for (j = 0u; j < (uint32_t)ST_LADDER_WRITE_WORDS; j++)
                    st_write_stage[j] = ((const uint32_t *)(void *)vaddr)[i * (uint32_t)ST_LADDER_WRITE_WORDS + j];
                (void)entry_storage_driver_write(lba, st_write_stage);
            }
            buf_unmap(bp);
            buf_setresid(bp, (uint32_t)buf_count(bp) - count);
            buf_biodone(bp);
            entry_live_write("xnu_live_rootmedia_card_wr_blocks", nblk);
            entry_live_write("xnu_live_rootmedia_card_wr_first_lba",
                             (uint32_t)((uint64_t)base_lba
                                        + (off / ST_MEDIA_BLOCKSIZE)));
            entry_live_write("xnu_live_rootmedia_card_wr_last_lba",
                             (uint32_t)((uint64_t)base_lba
                                        + (off / ST_MEDIA_BLOCKSIZE) + ((nblk != 0u) ? (nblk - 1u) : 0u)));
            st_medium_write_served++;
            entry_live_write("xnu_live_rootmedia_write_served", st_medium_write_served);
            return;
        }
#endif
        for (i = 0u; i < nblk; i++) {
            uint32_t lba = (uint32_t)((uint64_t)base_lba
                                      + (off / ST_MEDIA_BLOCKSIZE) + i);
            const uint32_t *w = entry_storage_driver_read(lba);
            bcopy((const void *)w, (void *)(vaddr + i * ST_MEDIA_BLOCKSIZE), ST_MEDIA_BLOCKSIZE);
        }
        buf_unmap(bp);
        buf_setresid(bp, (uint32_t)buf_count(bp) - count);
        buf_biodone(bp);
        entry_live_write("xnu_live_rootmedia_card_blocks", nblk);
        entry_live_write("xnu_live_rootmedia_card_last_lba",
                         (uint32_t)((uint64_t)base_lba
                                    + (off / ST_MEDIA_BLOCKSIZE) + ((nblk != 0u) ? (nblk - 1u) : 0u)));
        /* **911d: the RAW unit's first served LBA, under its OWN key.** Unit 2's `_card_last_lba` above
         * is the partition-relative cell 903's rows read; writing it a second time for the raw unit
         * would re-point a published number at a different disk - the "one value, two definitions"
         * defect. On a raw read `_card_raw_first_lba == _card_raw_last_lba - (nblk-1)` and both are
         * MEDIUM LBAs, so a raw read of the GPT header (medium LBA 1) reads it back as 1, not 1 +
         * selected_lba. */
        if (ST_MEDIA_RAW_UNIT(unit)) {
            entry_live_write("xnu_live_rootmedia_card_raw_first_lba",
                             (uint32_t)((uint64_t)base_lba + (off / ST_MEDIA_BLOCKSIZE)));
            entry_live_write("xnu_live_rootmedia_card_raw_last_lba",
                             (uint32_t)((uint64_t)base_lba + (off / ST_MEDIA_BLOCKSIZE)
                                        + ((nblk != 0u) ? (nblk - 1u) : 0u)));
        }
        st_medium_refused--;
        st_medium_served++;
        entry_live_write("xnu_live_rootmedia_served", st_medium_served);
        entry_live_write("xnu_live_rootmedia_refused", st_medium_refused);
        return;
    }
#endif

    if (base == 0) {                              /* a unit with a medium but no array to serve it from */
        buf_seterror(bp, ENXIO);
        buf_biodone(bp);
        return;
    }

    if (buf_map(bp, &vaddr) != 0) {               /* memdev panics here; this arm must not */
        buf_seterror(bp, EFAULT);
        buf_biodone(bp);
        return;
    }
    bcopy((const void *)(base + (uint32_t)off), (void *)vaddr, (size_t)count);
    buf_unmap(bp);
    buf_setresid(bp, (uint32_t)buf_count(bp) - count);
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
 *
 * **866 (rung 60): `mi_mdev` IS NOW 0, UNCONDITIONALLY, AND THAT ONE WORD IS THE RUNG.**
 *
 * 862 wrote `mi_mdev = 1` after `mdevadd`'s own answer, and its note explains the reasoning that
 * produced it (see the file top): with the answer TRUE, mockfs takes the memory-backed branch and
 * `/sbin/launchd` is mapped from raw memory, so process 1 is exec-able and nothing has to implement a
 * `strategy`. **That is also exactly the reason `st_media_strategy` had never executed**, even after
 * 864 built it and 865 proved the two are one field apart. This rung takes the other arm: with
 * `mi_mdev` FALSE the memory-backed branch is not taken, and the file node's pages must come through
 * `cluster_pagein` -> `mockfs_strategy` -> `buf_strategy` -> `spec_strategy` -> `st_media_strategy`.
 *
 * **The call still SUCCEEDS** - `mi_base` and `mi_size` are filled exactly as before - and that
 * distinction is the arm's whole safety argument, because the two ways this could go wrong are not
 * symmetric. 865 section 4 proposed making the device stop answering `DKIOCGETMEMDEVINFO` at all, and
 * **that is the shape rung 60 must NOT take**: `mockfs_mountroot` takes its memory-backed setup
 * inside `if (!VNOP_IOCTL(...))`, so an `ENOTTY` here leaves `mockfs_memdev_base` at zero for a root
 * that has no other source, and the run stops at the MOUNT instead of at the exec. A successful call
 * carrying `mi_mdev = 0` declines the optimisation and nothing else - which is also why the base and
 * the size stay filled: they are the same numbers a reader of this cell would have seen before, so
 * the only value this rung moves is the one the branch is decided on.
 *
 * **AND THE BRANCH IT TAKES CANNOT LOSE THE EXEC, because both branches read the same bytes.** The
 * strategy serves disk 0 from `g_stage90_ramdisk` and disk 1 from `st_medium_virt` (`st_medium_disk_base`),
 * which are the very arrays `mi_base` names below; a file byte at offset N therefore comes from the
 * same address whether the pager mapped it or the strategy copied it. `exec_mach_imgact` sees the same
 * `MH_MAGIC` either way. **That is what separates this arm from the one 865 section 4 refused to
 * build**: the risk there was that the file node's bytes would become *somebody else's sector*
 * (`st_medium_virt` holding a partition's superblock), and the fix is not to avoid the strategy but to
 * point it at the bytes the root already had.
 *
 * It is `noinline` on purpose: 855's build clause read a two-arm `if`'s stores out of the LINKED
 * disassembly and was refuted by the linker's own tail-merge (LINKED ORDER IS NOT SOURCE ORDER). A
 * check that reads this function's body is reading a property of ONE named function, which is
 * layout-independent - and this rung's clause reads exactly the `mi_mdev` store in it.
 */
/*
 * **The whole memory-backed answer is mockfs's, and mockfs is a 4570 artefact (921).** Darwin-13 has
 * no `bsd/miscfs/mockfs`, so nothing reads `DKIOCGETMEMDEVINFO` and the header does not declare it
 * (`bsd/sys/disk.h`) - the `#ifdef` below is on that declaration, so the body is present exactly when
 * the tree can ask it. On 4570 the switch is defined and nothing here changes.
 */
#ifdef DKIOCGETMEMDEVINFO
__attribute__((noinline)) static int
st_media_memdev_info(dev_t dev, dk_memdev_info_t *info)
{
    uint32_t unit = (uint32_t)minor(dev);

    #if STAGE90_XNU_HFS_ROOT_MEDIA
    /*
     * **892: ON THE HFS ARM THE STRATEGY SERVES THE VOLUME, SO DECLINING MEMORY-BACKING LOSES THE
     * EXEC.** 866 set this to 0 unconditionally, and its safety paragraph rested on "the strategy
     * serves disk 0 from `g_stage90_ramdisk`, which is the very address `mi_base` names" - a fact
     * that is TRUE ONLY WHILE THE STRATEGY'S MEDIUM IS THE MACH-O. 882's `STAGE90_XNU_HFS_ROOT_MEDIA`
     * moves `st_medium_disk_base(0)` to the volume (`g_stage90_root_hfs`), so with `mi_mdev = 0`
     * mockfs's file node is served the volume's raw bytes - `0x482B` at offset 1024, not `MH_MAGIC`
     * - and `load_init_program` fails `/sbin/launchd` with ENOEXEC. That is the rung-61 press (892).
     *
     * The answer must describe the medium mockfs can exec from - the MACH-O - and accept the
     * memory-backing (so mockfs's fall-through maps `g_stage90_ramdisk`, below), while the STRATEGY
     * still serves the volume for the HFS reader. If `hfs_mountroot` succeeds the root is HFS+ and
     * `/sbin/launchd` arrives through the strategy; if it fails, mockfs mounts and memory-backs the
     * Mach-O. **BOTH PATHS THEN EXEC** - which is the split 882's own comment describes
     * ("`DKIOCGETMEMDEVINFO` ... stay[s] on the Mach-O") and which 866's hard 0 silently defeated.
     */
    info->mi_mdev = 1;
#else
    info->mi_mdev = 0;                                   /* 866: DECLINE memory-backing - the rung */
#endif
    /*
     * **866: THE BOUNDS TEST MOVES ABOVE THE `mi_phys` STORE, AND THAT IS A REPAIR RATHER THAN A
     * REORDERING.** 862 wrote `info->mi_phys = (st_media_flags[unit] & ST_MEDIA_MDPHYS) ...` first and
     * the `unit >= ST_MEDIA_DISKS` refusal after it, so a `minor(dev)` of 2 or more indexed
     * `st_media_flags` past its two entries and read whatever the linker put next before this function
     * declined the unit. In practice `unit` is a number this device handed out, so the read never
     * happened - but "the caller never does that" is the same reasoning that makes an out-of-range
     * read survive until a caller does, and the fix is one statement's position. **`mi_phys` is read
     * by nothing on this arm anyway** (mockfs only consults it inside the branch `mi_mdev = 0` now
     * closes - see the flag block above): the store is kept because it is part of an answer that
     * describes a device, not because a branch depends on it.
     */
    if (unit >= ST_MEDIA_DISKS) {
        info->mi_phys = 0;
        info->mi_base = 0u;
        info->mi_size = 0u;
        return EINVAL;
    }
    info->mi_phys = (st_media_flags[unit] & ST_MEDIA_MDPHYS) ? 1 : 0;
    /*
     * **THE BASE AND THE SIZE ARE THE DEVICE'S OWN, AND FOR DISK 1 THEY ARE THE STAGED SECTOR.**
     * Disk 0 is the RAM disk 862 chose (its address and length are the link's own symbols). Disk 1 is
     * the medium 864 staged: the base is the address of the staged array and the page count is the
     * one the accessor derived.
     *
     * **866: THESE ARE NOW THE SAME BASE THE STRATEGY SERVES FROM, AND THAT IS NOT A COINCIDENCE.**
     * `st_medium_disk_base` returns exactly these two addresses for exactly these two units, so the
     * branch mockfs takes and the branch this file implements describe ONE medium and not two. A
     * reader who wants to know whether the level still holds the bytes can compare this function's
     * two stores against that accessor's two returns - which is what the build clause does.
     *
     * **`mi_size` IS A COUNT OF PAGES, NOT OF SECTORS** (`memdev.c`'s `mdevadd` is called with
     * `size >> 12` and `mockfs_fsnode.c:342` shifts it back with `<< PAGE_SHIFT`). The one sector this
     * device holds is therefore `<= 1` page, and the count is rounded so it is never 0.
     */
    if (st_media_flags[unit] & ST_MEDIA_STAGED) {
        info->mi_base = (uint32_t)((uintptr_t)st_medium_virt >> ST_MEDIA_PAGE_SHIFT);
        info->mi_size = (uint64_t)((st_medium_pages != 0u) ? st_medium_pages : 1u);
    } else {
        info->mi_base = (uint32_t)((uintptr_t)g_stage90_ramdisk >> ST_MEDIA_PAGE_SHIFT);
        info->mi_size = (uint64_t)(st_media_bytes() >> ST_MEDIA_PAGE_SHIFT);
    }
    return 0;
}
#endif /* DKIOCGETMEMDEVINFO */

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
#ifdef DKIOCGETMEMDEVINFO
    case DKIOCGETMEMDEVINFO:
        return st_media_memdev_info(dev, (dk_memdev_info_t *)data);
#endif /* DKIOCGETMEMDEVINFO */
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
        /* **911d: for the RAW unit this is the CARD's block count, not a partition's** - the
         * registration sets `st_media_blockcount[ST_MEDIA_CARD_RAW] = entry_storage_card_sectors()`, so
         * a reader asking the raw unit's size gets the whole medium (16 GB -> `0x01d5a000` sectors; a
         * 32 GB part reads ~2x). That is the capacity the 「16GB/32GB存储」 clause turns on, and it is
         * read here through the SAME `DKIOCGETBLOCKCOUNT` every other unit answers - no new opcode.
         * (`bsd/sys/disk.h` in this tree has no `DKIOCGETMEDIASIZE`; block count x block size is how
         * this OS reports media size, so the two published numbers ARE the answer.) */
        *(uint64_t *)data = st_media_blockcount[unit];
        break;
    case DKIOCISWRITABLE:
        /* **905: the answer is now CONSISTENT with what the strategy will do.** Before this rung
         * `DKIOCISWRITABLE` returned 1 for EVERY unit while `st_media_strategy` refused every `B_WRITE`
         * with `EROFS` - a byte that promised a write the body would decline, and the one place a mount
         * could have concluded rw on a unit that would then fail. Under `HDD_WRITE=1` the card unit
         * (ST_MEDIA_DRIVER) IS writable, so 1 is honest for it, and the two RAM-backed units answer 0
         * because their write is still refused. With the switch off every unit answers 0 and the old
         * unconditional 1 is replaced by an answer the strategy agrees with.
         *
         * **THE PAIR IS THE CLAIM, AND IT IS CHECKED BY THE BUILD, NOT BY THIS COMMENT**: `xnu_entry_905`
         * refuses an image in which `st_media_strategy` carries a write branch while this byte still
         * answers 0 for the card, or vice versa - `mi4-a-claim-in-a-comment-is-not-a-check`. */
#if STAGE90_XNU_HDD_WRITE
        *(uint32_t *)data = (unit == ST_MEDIA_DRIVER) ? 1u : 0u;
#else
        *(uint32_t *)data = 0u;
#endif
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
        /*
         * **882: THE ONE PAIR OF NUMBERS THAT SAYS WHICH ARM THIS IS.** `_blocks` and `_pages` above
         * are the Mach-O's - what `DKIOCGETMEMDEVINFO` and `spec_open` see - and they are the same on
         * both arms, because mockfs's fall-through must not move. The strategy's medium is the number
         * that moves, and on the HFS arm it must equal the blob's byte count (524288) while `_pages`
         * still says 2. A run that reads `_strategy_bytes` == `_blocks*512` is on the baseline (the
         * strategy serves the Mach-O); one that reads `_strategy_bytes` != `_blocks*512` is on the
         * split, and only that reading proves the volume is the medium rather than an assumption.
         */
        entry_live_write("xnu_live_rootmedia_strategy_bytes",
                         (unsigned)st_media_strategy_bytes());
        entry_live_write("xnu_live_rootmedia_hfs", (unsigned)STAGE90_XNU_HFS_ROOT_MEDIA);
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
#if STAGE90_XNU_EMMC_STRATEGY
    /* 888: the same handover registers the CARD unit - the medium the strategy fetches block by
     * block. It is done HERE and not from `entry_storage.c` on purpose: the module owns the whole
     * card-arm registration, so the entry side needs no edit at all, and the two units are created
     * at the one moment the ladder has both read the medium and published its selection. */
    (void)entry_root_media_register_card();
#endif
    return 0;
}

#if STAGE90_XNU_EMMC_STRATEGY
/*
 * **888: THE CARD UNIT'S REGISTRATION - THE SELECTED PARTITION, ADDRESSED BY THE LADDER'S OWN NUMBERS.**
 *
 * This mirrors `entry_root_media_stage`'s use of `entry_root_media_register(1)`, one unit up. The
 * geometry is NOT the RAM disk's (which is what `entry_root_media_register` would set): the device's
 * size is `entry_storage_selected_count()` sectors - the extent the GPT walk selected by - and its
 * base is not an array. `DKIOCGETBLOCKCOUNT` answering the real sector count is what lets a
 * filesystem know how large this disk is; the staged unit's intentional `1` (one sector) does not
 * apply here, because this unit is meant to be mounted, not merely probed.
 *
 * **THE SELECTION'S ZERO IS A REFUSAL, TAKEN BEFORE ANY REGISTRATION.** `entry_storage_selected_count`
 * returns 0 when the walk selected nothing (no GPT, no extent, no sector count); a device registered
 * over that would have zero blocks and serve nothing, so this refuses with a visible return instead -
 * the same "a zero is a refusal" rule `entry_root_media_mount_disk` follows one level up.
 *
 * **ITS KEYS ARE ITS OWN (`_card_*`), AND THAT IS THE POINT.** Disk 0's four numbers ("major", "dev",
 * "blocks", "pages") describe the RAM-disk device and a run reads them as that device's geometry;
 * writing them a second time for unit 2 would silently re-point published numbers at a different disk
 * - the "one value, two definitions" defect the staged unit's `_stage_*` names already avoid.
 */
int
entry_root_media_register_card(void)
{
    dev_t dev;
    uint32_t count = entry_storage_selected_count();
    uint32_t lba   = entry_storage_selected_lba();

    /* 903: the card unit is registered ONCE. The root answer (`__wrap_mdevlookup`) is asked on every
     * boot, and `entry_root_media_stage` also asks for the unit at the same mount; a second
     * `entry_root_media_register` would `bdevsw_add` a SECOND major for the same medium and the root
     * would hold a `dev_t` whose strategy is a different registration than the one 892 measured. A
     * zero `st_media_major` is "not yet registered" (the array is `.bss`, and a real major is a small
     * positive integer `bdevsw_add` returns). */
    if (st_media_major[ST_MEDIA_DRIVER] != 0u)
        return (int)makedev(st_media_major[ST_MEDIA_DRIVER], ST_MEDIA_DRIVER);

    if (count == 0u) {
        entry_live_write("xnu_live_rootmedia_card_refused", 1u);
        return EINVAL;
    }

    dev = entry_root_media_register((int)ST_MEDIA_DRIVER);
    if ((int)dev < 0) {
        entry_live_write("xnu_live_rootmedia_card_err", (unsigned)dev);
        return (int)dev;
    }

    /* The registration's file-scope geometry is the RAM disk's; this device's is the selection's. */
    st_media_blocksize[ST_MEDIA_DRIVER]  = ST_MEDIA_BLOCKSIZE;
    st_media_blockcount[ST_MEDIA_DRIVER] = count;           /* the selected extent, in sectors */
    st_media_flags[ST_MEDIA_DRIVER]      = ST_MEDIA_MDINITED | ST_MEDIA_CARD;

    entry_live_write("xnu_live_rootmedia_card_refused", 0u);
    entry_live_write("xnu_live_rootmedia_card_registered", 1u);
    entry_live_write("xnu_live_rootmedia_card_dev", (unsigned)dev);
    entry_live_write("xnu_live_rootmedia_card_lba", lba);
    entry_live_write("xnu_live_rootmedia_card_blocks", count);
    entry_live_write("xnu_live_rootmedia_card_bytes",
                     (unsigned)((uint64_t)count * ST_MEDIA_BLOCKSIZE));
    return (int)dev;               /* 903: the dev_t, so the mount path can answer the root with it */
}

#if STAGE90_XNU_CARD_TOTAL
/*
 * **911d: THE RAW WHOLE-CARD UNIT - THE CAPACITY THE 「16GB/32GB存储」 CLAUSE ASKS FOR.**
 *
 * Unit 2 (903's mount unit) is the SELECTED PARTITION: `DKIOCGETBLOCKCOUNT` answers its extent and its
 * strategy offsets every LBA by `entry_storage_selected_lba()`. This unit is the MEDIUM: its block count
 * is the WHOLE CARD's and its strategy addresses raw LBAs, so a reader that asks the raw unit gets the
 * card's real size and can read any sector of it - including the GPT and every partition's own start,
 * which unit 2 cannot address because its window begins inside the selected partition.
 *
 * **IT IS ADDITIVE AND SEPARATE ON PURPOSE.** The clause is a RECOGNITION clause - it must not move the
 * MOUNT rung - so it is a NEW unit (3), not a change to unit 2. `ST_MEDIA_CARD_RAW = 3` and
 * `ST_MEDIA_RAW_UNIT()` fold the whole thing away when `STAGE90_XNU_CARD_TOTAL=0`, so the shipped
 * 903/906 image is unchanged to the byte.
 *
 * **THE ZERO IS A REFUSAL.** `entry_storage_card_sectors()` returns 0 when EXT_CSD was never read; a
 * device registered over that would claim zero blocks and serve nothing, so this refuses with a visible
 * return and publishes `_card_raw_refused` - the same rule `entry_root_media_register_card` follows.
 *
 * **ITS KEYS ARE ITS OWN (`_card_raw_*`), for the reason `_card_*` are** (see the note above): the
 * registered numbers must not re-point unit 2's published geometry at a different disk.
 *
 * **THE READ IS 903's, ADDRESSED FROM THE MEDIUM'S BASE.** The strategy's `base_lba` is 0 for this unit,
 * so it calls `entry_storage_driver_read(blkno)` - the SAME ladder door 903's reads use, at the LBA the
 * raw medium names - and copies out per block exactly as unit 2 does. No new command, no new register:
 * this is the read path 903/906 built, addressed from a different base. It is READ-ONLY (no write
 * branch reaches it, and `DKIOCISWRITABLE` answers 0 for it below).
 */
int
entry_root_media_register_card_raw(void)
{
    dev_t dev;
    uint32_t sectors = entry_storage_card_sectors();

    if (st_media_major[ST_MEDIA_CARD_RAW] != 0u)      /* registered once, like unit 2 */
        return (int)makedev(st_media_major[ST_MEDIA_CARD_RAW], ST_MEDIA_CARD_RAW);

    if (sectors == 0u) {
        entry_live_write("xnu_live_rootmedia_card_raw_refused", 1u);
        return EINVAL;
    }

    dev = entry_root_media_register((int)ST_MEDIA_CARD_RAW);
    if ((int)dev < 0) {
        entry_live_write("xnu_live_rootmedia_card_raw_err", (unsigned)dev);
        return (int)dev;
    }

    /* The registration set the RAM disk's geometry; this device's is the WHOLE CARD's. */
    st_media_blocksize[ST_MEDIA_CARD_RAW]  = ST_MEDIA_BLOCKSIZE;
    st_media_blockcount[ST_MEDIA_CARD_RAW] = sectors;   /* the MEDIUM, in sectors - not the selection */
    st_media_flags[ST_MEDIA_CARD_RAW]      = ST_MEDIA_MDINITED | ST_MEDIA_CARD;

    entry_live_write("xnu_live_rootmedia_card_raw_refused", 0u);
    entry_live_write("xnu_live_rootmedia_card_raw_registered", 1u);
    entry_live_write("xnu_live_rootmedia_card_raw_dev", (unsigned)dev);
    entry_live_write("xnu_live_rootmedia_card_raw_blocks", sectors);
    /* the card's byte length, published in its HIGH and LOW halves because it exceeds 32 bits:
     * ~15.76 GB = 0x03AB400000, so the high word is nonzero and a reader sees the whole card, not a
     * truncated low 4 GiB. This is the reading the capacity clause exists to produce. */
    entry_live_write("xnu_live_rootmedia_card_raw_bytes_hi",
                     (unsigned)(((uint64_t)sectors * ST_MEDIA_BLOCKSIZE) >> 32));
    entry_live_write("xnu_live_rootmedia_card_raw_bytes_lo",
                     (unsigned)(((uint64_t)sectors * ST_MEDIA_BLOCKSIZE) & 0xffffffffu));
    return (int)dev;
}
#endif
#endif