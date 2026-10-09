/*
 * The HFS root volume, as bytes in the entry image (experiment 882; HFSX since 965a, the family the
 * real iOS 7.1.2 rootfs uses).
 *
 * This file is `.incbin` and nothing else, and that is the whole design: the image it includes is the
 * output of `tools/build_hfs_root_image.sh`, which is the ONE definition of what XNU's root volume is
 * (`src/entry/blob/xnu_arm_entry_root_hfs.img`, HFSX 0x4858 v5, sha256 c154fc61... as of 965a). The sha
 * is a pointer for a reader, NOT a check: `mkfs.hfsplus` is not byte-reproducible, so the committed file
 * is not regenerated on every build and this number can drift from it - the fields the kernel actually
 * reads are re-derived from the committed file by `tools/check_hfs_root_blob.py` in `make check`, which
 * is why a stale sha here is a stale comment rather than a defect. A hand-written array here would
 * be a second definition of those 524288 bytes, and the bytes are not a layout a human can check
 * (`mi4-one-value-two-definitions`). `.incbin` is how `entry_ramdisk.s`'s precedent is kept without
 * restating a byte: that file assembles its Mach-O because its layout IS the interface (two fields are
 * expressions over labels); this one has no interface to express, only 128 allocation blocks.
 *
 * WHY A BLOB AND NOT A GENERATED FILE. `out/` is gitignored, so an image built into `out/` on a
 * machine that has not run the generator is absent at build time - and the arm's whole safety claim is
 * that a FAILED HFS mount falls through to mockfs. A build that silently embedded nothing would make
 * the fall-through the only path and the arm would measure nothing. So the bytes are committed, the
 * build asserts the file is exactly ROOT_HFS_BYTES, and `tools/check_hfs_root_blob.py` (in `make
 * check`) re-derives the one field the kernel reads that a stale blob would get wrong: the catalog
 * B-tree's extent and node size, which is what `hfs_mountroot` must find at `startBlock * 4096`.
 *
 * THE ALIGNMENT IS LOAD-BEARING. The volume's first sector is a boot sector and its volume header is
 * at byte 1024, both read by LOGICAL BLOCK NUMBER through `st_media_strategy`. The device serves
 * `base + b_blkno * 512`, so the array's address IS the volume's byte 0 and must therefore be a page
 * and sector multiple - `.align 12` is what makes `mi_base << 12` land on the array rather than into
 * the padding in front of it when mockfs is the row that mounts.
 */
    .section .data.hfsroot, "aw", %progbits
    .align 12                       /* 4096: the base is taken in PAGES by `DKIOCGETMEMDEVINFO` */

    .global g_stage90_root_hfs
    .type g_stage90_root_hfs, %object
g_stage90_root_hfs:
    .incbin "xnu_arm_entry_root_hfs.img"

/* The end alias, the same shape `entry_ramdisk.s` defines for `g_stage90_ramdisk_end`: the length is
 * read out of the LINK rather than written down a second time, so `st_media_bytes`-style arithmetic in
 * the root-media module cannot drift from the array it describes. */
    .global g_stage90_root_hfs_end
    .type g_stage90_root_hfs_end, %object
    .set g_stage90_root_hfs_end, .
