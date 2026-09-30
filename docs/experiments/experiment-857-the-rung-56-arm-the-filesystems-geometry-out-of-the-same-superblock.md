# 857 — THE RUNG-56 ARM: THE FILESYSTEM'S GEOMETRY, OUT OF THE SAME 512 BYTES RUNG 55'S THIRD READ ALREADY LANDED

**A build and a park, and nothing else; PARKED, NOT PRESSED, NOT ARMED.**

Arm `armed-storage-63c6d77d`, `STAGE90_XNU_STORAGE_PROBE=55` (**VALUE 55 = ORDINAL RUNG 56**),
entry bin `63c6d77d…` 5,585,532 B (size-neutral against rung 55's), entry elf `0e7c59e6…`
6,766,300 B, payload `stage90-qcdt.img` `2dcba8bb…` 8,605,696 B, `stage90.img` `c55b1ee4…`
6,084,608 B; readiness 5 of 5, park 11 of 11; **the entry-group page move did NOT fire this rung**
(`STAGE90_XNU_SEAM_LR` stays `0x8004d2dc`, rung 53's value).

## What the rung is, and what it costs

Rung 55's third CMD17 landed the first partition's superblock sector in the image's own
`st_read_block` and `st_part_parse` answered *is there a filesystem here* with two magic comparisons
(word 0 against `0xF2F52010`, word 14's low half against `0xEF53`). This rung decodes what the
**same 512 bytes** say about the filesystem's **shape** — the block size, the inode geometry, the
revision that selects which superblock layout the fields live in, and the feature word a mount
refuses on.

**It adds no read, no store, no window, no megabyte, no register and no byte of the medium.** It is a
**second reader of one buffer**, `st_part_geom`, called from `st_cmd_path` immediately after
`st_part_parse`. The build clause `xnu_entry_856` is what makes that a property of the linked image
rather than a sentence in a comment: `st_part_geom` makes 29 call(s), **all** to `entry_live_write`,
with **0** controller-window references, and is called **once**, after `st_part_parse` and after the
third `st_read_single_block`.

Rung 55's own clause (`xnu_entry_855`) still bounds `st_part_parse`, whose body this rung does not
touch — a body that grows by cells does not grow by calls, and the separability is what a clause can
check.

## The rung is about one word that means two things

`struct ext4_super_block` (`ext4.h`) and `struct f2fs_super_block` (`f2fs_fs.h:59`) are different
layouts, and the same 512 bytes are one or the other. Superblock byte 96 = **word 24** is the word
that carries the whole rung:

| Word | ext2/3/4 (`struct ext4_super_block`) | f2fs (`struct f2fs_super_block`) |
| --- | --- | --- |
| 0 | `s_inodes_count` | `magic` = `0xF2F52010` |
| 14 | `s_magic` = `0xEF53`, **low half** (`__le16`) | (inside `uuid`) |
| 24 | `s_feature_incompat` — the word `ext4_feature_set_ok` refuses a mount on | `root_ino` — fixed to **3** by `sanity_check_raw_super` |
| 25 | (zero) | `node_ino` — fixed to **1** |
| 26 | (zero) | `meta_ino` — fixed to **2** |

So the decode **derives** the family from the two words rung 55 already compared — it does not
inherit another body's verdict, because a cell published by another body is exactly what makes a
value's origin unreadable (`mi4-one-value-two-definitions`, at the FIELD level) — and **`_pg_decoded`
gates every family-specific cell**: it reads 1 only when a magic was found, and a
`_pt_fs_found = 0` buffer publishes `_pg_decoded = 0` with **nothing else**, rather than fields read
out of a word that is not the field they are named for.

## The cells

`_pg_called`, `_pg_done`; `_pg_which` (0 none, 1 ext2/3/4, 2 f2fs); `_pg_w0` and `_pg_w24` — the two
words the branches turn on, **published raw**, so a reader sees the shared byte without taking either
name on trust; `_pg_decoded` — the guard.

Under `_pg_which == 1`:

- `_pg_ext_inodes_count` (word 0, `s_inodes_count`), `_pg_ext_blocks_lo` (word 1);
- `_pg_ext_first_data_block` (word 5) — the vendor's own `ext4_group_first_block_no`
  (`ext4.h:1666-1670`) **adds** it to every group's first block, so a geometry without it is wrong by
  one block on a 1 KiB filesystem;
- `_pg_ext_inodes_per_group` (word 10, a **divisor** per `EXT4_INODES_PER_GROUP`, `ext4.h:339`) beside
  `_pg_ext_ipg_ok` (`!= 0`);
- `_pg_ext_rev_level` (word 19) beside `_pg_ext_rev_dynamic` (`rev >= EXT4_DYNAMIC_REV` = 1,
  `ext4.h:1372`) — the branch that selects the variable inode size and the feature sets, and the
  branch this image must not take blind;
- `_pg_ext_log_block_size` (word 6, the shift `1024 << s_log_block_size`), published as `0xFF` with
  `_pg_ext_blk_ok = 0` when it lies outside `[0, 6]`;
- `_pg_ext_feature_incompat` (word 24, the `ext4_feature_set_ok` subject).

Under `_pg_which == 2`:

- `_pg_f2fs_log_blocksize` (word 4);
- `_pg_f2fs_root_ino` / `_pg_f2fs_node_ino` / `_pg_f2fs_meta_ino` (words 24/25/26) beside
  `_pg_f2fs_root_ok`, which reads 1 only when they are **3 / 1 / 2** — the vendor's own triple
  (`sanity_check_raw_super`, `super.c:1052-1054`). This is a **validation against the vendor's
  constants**, not a range test this image invented.

**The f2fs name fields are not read, and the reason is a size**: `volume_name` is
`__le16[MAX_VOLUME_NAME]` with `MAX_VOLUME_NAME = 512` beside `VERSION_LEN = 256`
(`f2fs_fs.h:53-54`) — two thousand bytes of it in a 4096-byte struct — so `segment0_blkaddr` and
`main_blkaddr` (bytes 72/92) are not four-byte fields inside this sector's 512 bytes, and this rung
takes the header half one sector holds.

`s_max_mnt_count` is deliberately **not** published: this tree's `ext4.h` has no such macro, and
`fs/ext4` normalizes `s_max_mnt_count <= 0` anyway — the citation would be the e2fsprogs header and
not this one.

## A build clause was repaired before it would accept a correct body

The **same class** as rung 55's own repair, and found the same way — by running the clause.

The first draft of `xnu_entry_856` grepped `st_part_geom`'s linked disassembly for the **absolute
immediates** `#96`, `#100`, `#104` and refused a **correct** body. The compiler reaches the buffer's
words as `ldr rX, [rBase, #off]` against a base it materializes with a `movw`/`movt` pair, so a
superblock **byte** offset appears as **no immediate in the linked image at all** — the same
wrong-artifact class rung 55's clause was repaired for, and the subject of
`mi4-a-claim-in-a-comment-is-not-a-check`: *an assertion whose subject is text the artifact never
contains is a check of something else*.

The repair binds the geometry reads **relative to word 14**, whose load is identifiable because it
feeds the ext2/3/4 `s_magic` compare (`ldr …, #off` immediately followed by `movw rX, #61267`). Word
24 is **40** bytes past word 14, word 25 is **44**, word 26 is **48** — and the clause refuses unless
all three offsets are present in the body, so a body that read one of the three at the wrong word (a
decode of a neighbour, with its guard selecting the wrong namespace) is a build refusal.

## What it does not do

- **It moves no byte of the medium and touches no device register rung 51 did not.** The medium is
  not touched at all; the card stays in TRAN with its block length exactly as rung 55 left it.
- `STAGE90_XNU_PWR_WAIT_TICKS=384000` is inherited from rung 9 unchanged.
- `fastboot boot` only; nothing flashed; nothing written to storage.

## The record

- The park block is `# 856 -` in `records/revert-set.txt`, set `armed-storage-63c6d77d`.
- **NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the
  park is a RECORD and not a queue. THE PRESS IS THE OPERATOR'S.**
- **THE GOAL IS NOT MET** — a geometry is not a mount. No filesystem is mounted; no ISO 9660, no
  ext4, no f2fs; **TWRP-to-storage stays withheld.**