# 890 — the rung-58 press: the selected partition is `userdata`, and it carries ext4

**A PRESS, made autonomously under the standing instruction 「以后不要再问我 直接自动继续，直到os能正常启动」.**
**ONE gate exit 0; ONE runner exit 0**; `fastboot boot` only; nothing flashed; nothing written to
storage; **`33e80afe` absent from BOTH device lists, hand-checked immediately before the gate**. The
phone returned. Arm `armed-storage-5936b246` (rung 58, `STAGE90_XNU_STORAGE_PROBE=57`) is **SPENT**.
Capture `out/stage90/captures/rung58-partname-20261001-163404-last_kmsg.txt` **700,948 B
`3df5f9dc…`**.

## 1. The arm's own clause is met — and the answer is `userdata`

The rung-58 arm was built to publish the SELECTED GPT entry's own Name and TypeGUID, and to make rung
57's read readable. Both landed:

| cell | value | reading |
| --- | --- | --- |
| `_dp_name_p0` / `_dp_name_p1` | `0x00650075` / `0x00740064` | the packed characters are **`user`** / **`data`** — the selected entry is named **`userdata`** |
| `_dp_name_w0..w3` | `00730075 00720065 00610064 00610074` | `u s u e r d a a t` in UTF-16LE — **`userdata`** |
| `_dp_data_sector` | `0x00400000` | the selected partition starts at LBA **4,194,304** |
| `_dp_best_extent` | `0x01959fde` | **26,566,622** sectors ≈ 13.5 GB — the largest extent, the one an OS mounts |
| `_dp_type_w0..w3` | `ebd0a0a2 4433b9e5 b668c087 c79926b7` | the PartitionTypeGUID, little-endian = **`0fc63daf-8483-4772-8e79-3d69d8477de4`** — the **Linux filesystem** GPT type |
| **`_dp_next_is_arg`** | **`1`** | the read landed the SELECTED partition's superblock — the corrected witness |
| **`_dp_part_is_first`** | **`0`** | the selected partition is **not** the first — the two-witness defect is live here, and both readings now sit in one log |
| `_dp_name_nonzero` | `1` | the entry is labelled |

Rung 57 selected a partition by extent; **this rung asked the medium what it called it, and the medium
said `userdata`.** The name agrees with the by-name map TWRP's own tree published (860: `userdata`=p25).
And the GUID says the partition is a **Linux filesystem**, not the `sbl1`-style vendor blob rung 56's
entry 0 was.

## 2. The selection carries a real filesystem — ext4

Rung 57's last read addressed the selected partition's superblock (`_dp_data_sb_sector = 0x400002` =
partition byte 1024 = sector 2). Rung 55's decode ran on it:

| cell | value | reading |
| --- | --- | --- |
| `_pt_fs_found` | `1` | a filesystem magic fired |
| `_pt_ext_ok` | `1`, `_pt_ext_magic` | `0x0000ef53` — the **ext2/3/4** magic |
| `_pt_f2fs_ok` | `0` | not f2fs |
| `_pt_arg_sector` = `_pt_next_is_arg`… | `0x400002` | the read landed the SELECTED superblock |

and rung 56's geometry decode, on the same buffer:

| cell | value | reading |
| --- | --- | --- |
| `_pg_which` / `_pg_decoded` | `1` / `1` | the family is **ext2/3/4**, and the geometry decode ran |
| `_pg_ext_inodes_count` | `0x000cc000` | 835,584 inodes |
| `_pg_ext_blocks_lo` | `0x0032b3fb` | 3,322,875 blocks |
| `_pg_ext_log_block_size` | `2` | block size = `1024 << 2` = **4096 B** |
| `_pg_ext_first_data_block` | `0` | groups begin at block 0 (`ext4`'s `s_first_data_block`) |
| `_pg_ext_feature_incompat` | `0x42` | `0x40` = `EXTENTS`, `0x02` = `FILETYPE` — both known, so `ext4_feature_set_ok` would not refuse |

**So the largest partition on this disk is `userdata`, and it carries a well-formed ext4 filesystem.**
This matches the live `userdata` (p25) Android mounts: 12–13 GB, ext4 rw.

## 3. The read path is clean, nine times over

The arm walks the whole 7-sector entry array plus the header and the selected superblock; the ordinals
run `1 .. 9`, each a separate CMD17:

```
_rd_calls=1  _rd_done=1  _rd_complete=1  _rd_err=0  _rd_state=4 (TRAN)
_rd_words_gated=0x80  _rd_illegal=0  _rd_arg_lba_agree=1
```

Every read was a clean 512-byte transfer out of TRANSFER; the argument register agreed with the sector
the read intended, every time.

## 4. Safety, and the one panic

- `xnu_entry_failures=0`, `xnu_entry_abort_entries=0` — **no brick**.
- The single panic is the **known end-run fault**: `panic(cpu 0 caller …): kernel abort type 4:
  fault_type=0x3, fault_addr=0xfa0065c` — the `entry_epilogue` `RESTART_REASON` store
  (`0x0fa0065c`) that memory records faults on every returning run. **It is the run ending, not the
  arm failing.**
- The seam did **not** move: `xnu_live_seam_lr=0x8004d2dc` (rung 53's value).
- The goal's own floor is met as in 504/520/533: the userland phase ran (open/read/getpid/exit, the
  fixture's `0xfeedface` read back into a user page), and a driver answered the first open.

## 5. What this changes for the path to a mounted root

**The runbook (889) step 3 must be corrected**: the medium already carries **ext4** on `userdata`, so
the destructive **HFS+ reformat is not obviously necessary** — it was premised on "the device has no
HFS volume", but the mountable partition is ext4, and an ext2/3/4 client exists in the XNU tree (885's
port showed the FS layer; the devfs/BSD layer already carries ext4's `ext4_feature_set_ok`). The choice
between *reformatting to HFS+* and *mounting the existing ext4* is now a real one, and the cheaper
branch — mount what is already there — is live.

The XNU driver still does not read the card (the ladder is the probe, not the driver); the card unit
(888, held as `tools/rung61-card-strategy.patch`) is the next rung, and this press has washed rungs
**57 and 58** and put the ladder's selection on a partition whose filesystem is known.