# Experiment 902 — the HFS vnode-op descriptors, and the root that mounts

**The press that mounted HFS+. `armed-storage-92435ddc`, pressed 2026-10-02 (spent).** The 892 fix (the
HFS arm's `mi_mdev = 1`) plus 902's vnode-op descriptors produce an image whose `vfs_mountroot` takes
the **HFS+ row**, mounts the volume, and execs `/sbin/launchd` **from that volume**, through
`st_media_strategy` — not from the RAM disk.

## The defect 902 fixes

901's press (arm `b3e19b48`, the 900 malloc-zone fix) panicked at `fault_addr=0x4`, `pc` inside
`VNOP_STRATEGY+0x24`. Root cause: `hfs_getnewvnode` (`bsd/hfs/hfs_cnode.c:1297`) builds each vnode with
`vfsp.vnfs_vops = hfs_vnodeop_p`, and **`hfs_vnodeop_p` is a BSS global whose ONLY writer is
`vfs_opv_init()`** (`bsd/vfs/vfs_init.c:422`), walking the static `vfs_opv_descs[]`
(`bsd/vfs/vfs_conf.c`). 4570's table carries no HFS rows (it has no HFS), so `hfs_vnodeop_p` stayed
NULL, the vnode's `v_op` was NULL, and the mount's first B-tree node read (`BTOpenPath` -> the buffer
cache -> `VNOP_STRATEGY`, i.e. `v_op[1]`) dereferenced address 4. 901 measured exactly this: ONE
strategy read (the volume header at offset 0x400) and then the fault — the second read (`BTOpenPath`'s
header-node read at volume block 2) never happened.

**The fix** is 2050's own four `#if HFS` rows in `vfs_opv_descs[]`, added by the same tracked patch that
adds the root row: `tools/patch_vfs_conf_hfs_row.py` group B puts `&hfs_vnodeop_opv_desc`,
`&hfs_std_vnodeop_opv_desc`, `&hfs_specop_opv_desc` (and the FIFO one under its own `#if FIFO`) into
4570's table, guarded by `STAGE90_HFS_ROOT`. `tools/check_hfs_staged.sh` §4(d) refuses their absence.

## The press, read off the log

The arm is the same switch set as 901 (`out/stage90/xnu_arm_entry-config.txt`: `PROBE=60 MOUNT=1
HFS_ROOT_MEDIA=1 EMMC_STRATEGY=1`); only the pool changed (the descriptors) — the entry bin is
`92435ddc…`, a new hash.

**`hfs_stage` markers**: 1, 2, 6, 7, 8, 3, 4, **5**, then many more `6,7,8,3` cycles. Step 5 is
`hfs_MountHFSPlusVolume` reaching *after* the extents `BTOpenPath` **returned** — 901 never got past
4. The later cycles are the catalog/attributes B-trees opening (`hfs_getnewvnode` per fork).

**`st_media_strategy` reads** (offset = `b_blkno * 512`; the volume's blockSize is 4096, so mapping a
server offset to a volume block is a shift by 12):

| served | offset | what it is |
|---|---|---|
| 1 | 0x400 | the HFS+ volume header (`hfs_mountfs`, marker 1) |
| 2–3 | 0x2000 | extents B-tree, volume block 2 (`BTOpenPath`) |
| 4–5 | 0x1a000 | catalog B-tree, volume block 26 (`BTOpenPath`) |
| 6–7 | 0xa000 | attributes B-tree, volume block 10 (`BTOpenPath`) |
| 8 | 0x1b000 | catalog node 27 (a lookup / `hfs_vget`) |
| **9** | **0x22000** | **volume block 34 = `/sbin/launchd`'s `__TEXT` page** |

Read 1 is the mount's header. Reads 2–8 are the HFS **mount** opening its three B-trees (this is the
mount; a filesystem that fails to mount cannot open all three). Read 9 is the **exec**: `/sbin/launchd`
lives at blob byte 139264 = 0x22000 = volume block 34, request `blkno=0x110`, `count=0x1000` — the file's
first 4096-byte page, exactly what the Mach-O loader reads.

**The read at 0x22000 is the discriminator, and it is not a coincidence that can be explained away.**
The fall-through root, `mockfs`, is memory-backed here (892's `mi_mdev = 1`) and serves file bytes from
`g_stage90_ramdisk` at `mi_base << 12` through `pager_map_to_phys_contiguous` — **it never enters a
strategy** (that is what 866's `mi_mdev = 0` rung was about). And if it did, `mockfs_blockmap` sets
`b_blkno = f_offset / mnt_devblocksize` with `mnt_devblocksize = 512`, so launchd at **file offset 0**
would be block **0**, not 0x110. A request at 0x110 names launchd's *volume* block; **only HFS knows
that number**, because only HFS read the catalog to find where the file's data lives. So the exec went
through HFS.

**Corroboration, from the console.** `HFS_MOUNT_DEBUG` is 1 in both `hfs_vfsops.c:123` and
`hfs_vfsutils.c:72`, so a failed HFS mount would print `hfs_mountroot: hfs_mountfs returned N`,
`hfs_mounthfsplus: BTOpenPath returned (N)`, etc. **The log contains no `hfs` line at all** — every HFS
error path is silent, so `hfs_mountfs` returned 0. `bsd_init`'s `cannot mount root, errno = %d`
(`kern/bsd_init.c:962`) did not print either, so `vfs_mountroot` returned 0: **the root mounted.** And
900's panic (`_malloc_zone ZONE: type = 76`) and 901's NULL-`v_op` fault (`fault_addr=0x4`) are both
gone.

**The exec.** `load_init_program` (`kern/kern_exec.c:5119`) prints `attempting to load /sbin/launchd`
(5122/5141), and `load_init_program_at_path` **returns 0 → `return;`** with no further output on
success. On failure it prints `failed loading /sbin/launchd: errno %d` and then
`panic("Process 1 exec of %s failed")`. The log shows the `attempting` line and **neither** the failure
line **nor** the panic; the run instead continues to `mini4: the OS starts the process at 0x10e0`.
That is the loader's design: **the exit code was 0.** `/sbin/launchd` was exec'd. (901, by contrast,
panicked before any exec.)

**`BSD root: md0` is not a counter-signal.** That line is `IOKitBSDInit.cpp:475` and prints the name
built from the `rdBootVar` boot-arg (`"md0"`), not the filesystem: `mdevlookup` is wrapped by the
payload (`xnu_live_mdevlookup_ret=0x05000000` = major 5, unit 0 = the `st_media` device), so the boot
string says `md0` on **every** arm, HFS or RAM-disk. It names the root **device**; it cannot name the
filesystem on it. The filesystem is read off `vfstbllist` ordering (verified in the linked image:
`devfs, hfs(FT_HFS=17), mockfs, routefs`) and the strategy reads above.

## Where the run ended

Not on a mount fault. The run ended on the harness's own forced-end clock: `entry_seam_end_run+0xc`
storing `RESTART_REASON` at `0x0fa0065c`, the known since-690 store fault. Before that, pid 1 had been
launched and parked: 73568 idle-path entries, `SetIdlePop` x4. This is the same terminal state every
successful boot since 894 has shown — the device will not reset itself (XNU's reboot path faults), so
the harness ends the run by its own clock.

## What this does and does not establish

**Establishes.** The HFS+ port, linked into the entry image with the root row and the vnode-op
descriptors, **mounts a real HFS+ volume as root and execs process 1 from it**. This closes the
"is the port in the image that mounts, and can it mount" question (895's owed step) and it is the
first root in this project that is a real filesystem rather than a memory device.

**Does not establish — the goal's storage clause is still open.** The volume `st_media_strategy` serves
is the **committed RAM blob** `g_stage90_root_hfs` (`xnu_live_rootmedia_strategy_bytes=0x00080000`,
512 KiB), not the eMMC. The device carried no HFS+ volume (880: `userdata` is ext4), so this mounts a
host-built image, not the phone's storage. Reading the **real eMMC** is the separate driver track
(867), and a *persistent* HFS+ root requires either reformatting `userdata` (889 step 3, destructive,
the operator's) or a driver that reads a foreign filesystem. Neither is done here.

**Careful reading of `strategy_medium=0`.** No `xnu_live_rootmedia_strategy_medium=1` record appears,
which could be read as "no unit-1 (staged sector) read". That is expected and not a defect: the
staged sector is registered under major 3/4 (the ladder's own registrations) while the root reads are
served by unit 0 of major 5, and the root's pages are unit-0 reads, so `_strategy_medium` is 0 for
them by construction. The evidence for the root's filesystem is the **offset**, not this cell.

## Goal status

**GOAL NOT MET** in the storage sense. The OS boots and the **root filesystem is HFS+, exec'd from the
volume** — but the volume is a RAM blob, not the device's own storage. The next clause is the eMMC
driver (867) / persistence (889 step 3).