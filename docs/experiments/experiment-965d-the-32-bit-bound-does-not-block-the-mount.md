# 965d — the 32-bit bound does not block the 896 MiB mount (a 965c retraction) (2026-10-09)

965c read `st_medium_disk_bytes(ST_MEDIA_DRIVER)` returning a **32-bit** product (692 MiB for a 13.6 GiB
partition) and concluded that this bound was *what stopped the real 896 MiB iOS 7.1.2 rootfs from
mounting*: 「a mount over that bound reads EOF for the volume's top ~204 MiB and cannot parse its catalog」.
The switch it added (`STAGE90_XNU_FULL_EXTENT=1`, computing the bound in 64 bits) was parked as
`armed-d13-e28361f9` on that premise.

This rung checks the premise **against the volume's actual bytes**, host-side and device-free. The premise
is **false**: every block the real rootfs's mount and exec read lies **below 530 MiB**, well under the
32-bit bound. The switch is a real correctness fix, but it is **not** what the mount waits on. The
falsification of a claim I made in the previous rung is the deliverable here
(`[[mi4-a-claim-in-a-comment-is-not-a-check]]`).

## 1. The volume, measured

`/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` (HFSX `0x4858` v5, 896.0 MiB, blockSize 4096,
`totalBlocks` 229376) was walked block-by-block host-side with a catalog reader. The reader is validated
by the thing it must reproduce: it enumerates the catalog tree and counts the files, and that count equals
the volume header's own `fileCount`.

| quantity | value | in MiB |
|---|---|---|
| volume header `fileCount` | 7142 |  —  |
| catalog-walker file count | **7142** (exact match — the reader's validation) |  —  |
| volume header `folderCount` | 2850 |  —  |
| catalog fork — first extent | block 9220, 768 blocks | 36.0 |
| catalog fork — second extent | block 124571, 768 blocks | 486.6 |
| **highest catalog/metadata end block** (2nd extent end) | **125339** | **490.0** |
| **highest file-data end block** (max over all 7142 data forks) | **135750** | **530.3** |
| 32-bit strategy bound `725197312 B / 4096` | 177050 | 691.6 |
| allocation-bitmap bits set in `[177050, 229376)` | **1** (block 229375 — the tail reserve) |  —  |
| alternate volume header (`totalBlocks - 2`) | 229374 | 896.0 |

**The entire live volume fits under 530.3 MiB.** The bitmap confirms it independently of the catalog: above
the 32-bit bound (`177050`) only a single block is allocated at all — the last block of the volume, the
reserved tail, which no mount reads as data. So the claim "the top ~204 MiB is served EOF and the catalog
is unparseable" is wrong: the catalog's first extent (block 9220 = **36 MiB**, not "above 692 MiB") and its
second extent (124571 = **486.6 MiB**) are both far inside the bound, and so is every file's data.

## 2. The one high read a mount *could* make, and why it never happens

The single region at or above the bound that the HFS mount *can* touch is the **alternate volume header**
at block `totalBlocks - 2` (229374, 896 MiB). It is read in exactly one place:

- `bsd/hfs/hfs_vfsops.c:3706` — the alt-header path is entered **only when the primary is judged corrupt**.
  The primary here is intact (its signature, version, block size and totals all parse), so this path is not
  taken.

And even that path is disarmed on this arm:

- `bsd/hfs/hfs_vfsutils.c:452` — `hfs_alt_id_sector` is set to **0** whenever the **partition is larger
  than the volume** (`spare_sectors > blockSize / logical_block_size`). On `userdata` the partition is
  13.6 GiB and the volume is 896 MiB, so `hfs_alt_id_sector = 0` and the alternate header is never read —
  independent of whether the primary is corrupt.

So the alternate header — the only ≥ 691.6 MiB read available — is unreachable on this medium.

## 3. What the 32-bit bound therefore is

It remains a **real disagreement with the block count** (one value, two definitions): the same unit reports
`DKIOCGETBLOCKCOUNT` = 26,582,225 sectors = 13.6 GiB (64-bit), while `st_media_strategy`'s `len` is
725,197,312 B = 691.6 MiB. A read at or above 691.6 MiB is refused `EINVAL` by the strategy
(`stage90_root_media.c:862-872`, `if (off >= len) { if (off > len) buf_seterror(EINVAL); ... }`) — a hard
refusal, not a silent zero-fill. That refusal is a **correctness bug** for any medium with real data up
there; it is simply **not exercised by this rootfs**, whose data all lives below 530 MiB.

**Consequence for the arm.** `STAGE90_XNU_FULL_EXTENT=1` stays, but its stated role changes: it is a
*correctness fix* (make the strategy's `len` agree with the block count), not a *blocker removal*. The
965c press is a valid test of the **card serving path** (965b's ladder) — if the real volume mounts, that
proves the ladder reads the real medium — but a successful mount is **not** evidence that `FULL_EXTENT`
took, because the mount would succeed under the 32-bit bound too.

## 4. What this does NOT change

- The **965c switch itself is unchanged and still correct**: the 64-bit accessor, the gated strategy
  branch, the `nm` marker, the preflight size clause, and the record/`ENTRY_ARM_KEYS` threading all stay.
  Its byte-neutrality at `FULL_EXTENT=0` and its effect at `FULL_EXTENT=1` are as measured in 965c §4.
- The **parked arm** is still `armed-d13-e28361f9`: this rung edits only *comments* in
  `stage90_root_media.c`. The D13 platform object rebuilt with `FULL_EXTENT=1` hashes **identically**
  (`sha256` `2a987c5c54464e531698428a0afdfd7ab3ad27cc6497c94d23aa0ea97bb422ee`) — the ENTRY link has no
  DWARF for the external platform `.c`, so a comment edit does not move the `.o` or the image. No new arm,
  no rebuild of the image, no press.
- The **real question for the userspace clause** is unchanged and now correctly located: whether XNU mounts
  this volume is decided by the **ladder's read path** (965b's card serving, the `blkno`/`_card_last_lba`
  trace), not by the bound. Whether it can then *exec* `/sbin/launchd` (located by the catalog walker
  above; all its blocks ≤ 530 MiB) is likewise a ladder/driver question, not a `FULL_EXTENT` one.

## 5. What would falsify this

- A block in `[177050, 229376)` **other than 229375** set in the allocation bitmap → real data in the
  region, and the bound *would* bite (it does not: one bit, the tail reserve).
- A catalog fork extent with `end >= 177050` → the catalog itself would be truncated by the bound (it ends
  at 125339, 490 MiB).
- Any data fork with `end >= 177050` → a real file would be unreadable under the bound (the maximum is
  135750, 530.3 MiB).
- The file count from the walker **not** matching `fileCount` → the reader is wrong and its high-water
  marks are meaningless (it matches exactly, 7142).

## 6. Provenance

- The real volume read host-side: `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` (HFSX `0x4858` v5, blockSize
  4096, `totalBlocks` 229376, `fileCount` 7142, 896.0 MiB); catalog walker + allocation-bitmap scan run
  against those bytes.
- The mount path read in the D13 tree: `bsd/hfs/hfs_vfsops.c` (alt-VH at `:3706`, primary-corruption gate),
  `bsd/hfs/hfs_vfsutils.c:452` (`hfs_alt_id_sector = 0` when partition > volume).
- The strategy bound: `src/platform/stage90_root_media.c` (`st_medium_disk_bytes`,
  `st_medium_card_full_bytes`, `st_media_strategy`'s `off >= len` rule).
- Device unmodified; adb idle. Retracts the causal claim in
  `experiment-965c-the-strategy-bound-reaches-the-real-rootfs.md` §1. Follows
  [[mi4-965-real-rootfs-is-hfsx]].