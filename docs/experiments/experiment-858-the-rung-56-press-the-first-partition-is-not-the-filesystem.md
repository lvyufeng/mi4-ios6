# 858 — THE RUNG-56 PRESS: THE WHOLE GPT CHAIN WORKS, AND THE FIRST PARTITION IS NOT THE FILESYSTEM

**THE PRESS WAS TAKEN AND `armed-storage-63c6d77d` IS SPENT.** One gate, one runner, `fastboot boot`
only, nothing flashed, `33e80afe` absent from both device lists hand-checked immediately before the
gate; **the device returned** (runner exit 0) and `adb` lists `4a2fe00b` as `device` again. There is
no brick: `xnu_entry_failures = 0`, `abort_entries = 0`.

Capture `out/stage90/captures/rung56-fsgeom-20261001-010335-last_kmsg.txt` 663,149 B `db569502…`.
Arm `armed-storage-63c6d77d` (`STAGE90_XNU_STORAGE_PROBE=55`, **VALUE 55 = ORDINAL RUNG 56**).

**This one press settles THREE parked rungs at once** — 54 (does a GPT partition exist), 55 (does
the partition carry a filesystem superblock), 56 (what is the superblock's geometry) — because a
cumulative ladder compiles every lower body in. Rungs 54, 55 and 56 are now all answered; they are
no longer parked-and-unpressed.

## The whole chain works

Three `st_read_single_block` calls, ordinals 1/2/3, and for each one the sector the driver wrote to
the argument register equals the sector the decode expected (`_rd_arg_lba_agree = 1`):

| ordinal | `_rd_lba` | `_rd_arg_wrote` | what it landed |
| --- | --- | --- | --- |
| 1 | `1` | `1` | the GPT header |
| 2 | `2` | `2` | the partition entry array |
| 3 | `0x24` (36) | `0x24` | partition 1's superblock sector |

Every read is clean: `_rd_complete = 1`, `_rd_err = 0`, `_rd_state = 4` (TRAN), `_rd_words_gated =
0x80`, `_rd_illegal = 0`. **The chain `CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 →
CMD16 → CMD17 ×3` is complete and lands the addresses the decode computes.**

The GPT header (LBA 1): `_gp_sig_ok = 1`, `_gp_w0 = 0x20494645` (`"EFI "`), `_gp_w1 = 0x54524150`
(`"PART"`), `_gp_hdr_sig_ok = 1` with `_gp_hdr_sector = 1`, `_gp_my_lba = 1`,
`_gp_entry_lba = _gp_next_sector = 2`, `_gp_entry_size = 0x80` (128), `_gp_nentries = 0x1c` (28),
usable range `0x22`…`0x1d59fde`.

## The result: the first partition is ~2 MB and carries no filesystem

The entry array (LBA 2) yields entry 0 — `_ge_found = 1`, `_ge_blank = 0`, `_ge_index = 0`:

- type GUID words `0xdea0ba2c 0x4805cbdd 0x28f4f9b4 0x983e1c25` — **not** an EFI-system
  (`c12a7328-…`) or Linux-filesystem (`0fc63daf-…`) type GUID;
- `_ge_first_lba = 0x22` (34), `_ge_last_lba = 0xfff` (4095) — a **0x1000-sector extent, about
  2 MB**, the kind of partition that holds a firmware or vendor blob, not a filesystem.

The third read addressed `_ge_part_sector + 2 = 0x24`, and the partition decode confirms it landed
where it was aimed: `_pt_arg_sector = _pt_sb_sector = 0x24`, `_pt_next_is_arg = 1`. But the bytes
are **not** a superblock:

- `_pt_w0 = _pt_f2fs_magic = 0xe1a04614` — not `0xF2F52010`; `_pt_f2fs_ok = 0`;
- `_pt_ext_magic = 0x0000f0f0` (word 14's low half) — not `0xEF53`; `_pt_ext_ok = 0`;
- **`_pt_fs_found = 0`**, and the two negative signature checks hold (`_pt_hdr_ok = 0`,
  `_pt_mbr_ok = 0`).

`0xe1a04614` is ARM code (`mov r4, lr, lsr #12`), and word 14 `0x0000f0f0` is an `0xf0f0…` filler
byte pattern — this is a code/blob sector, not a filesystem superblock.

**The rung's premise — "the first partition carries the filesystem" — is false, and falsifiably
so.** `_ge_found = 1` says the walk found a partition; `_pt_fs_found = 0` says that partition is not
a filesystem. This is the same shape as rung 49 and rung 53: a true reading of the thing the rung
*aimed* at, and an informative "there is something else here".

## The geometry decode held its guard, correctly

`_pg_called = 1`, and **`_pg_decoded = 0` with `_pg_which = 0`** — which is exactly the design: on a
buffer where no magic was found, the guard publishes `0` and **no family cell is vended**. The two
raw words are still there for a reader — `_pg_w0 = 0xe1a04614`, `_pg_w24 = 0x97b425ed` — so the
shared byte is visible without either name being taken on trust. The word-24 dual meaning never had
to be resolved, because there was no filesystem to resolve it for.

## The frontier

The GPT declares `NumberOfPartitionEntries = 28`, but this rung reads ONE sector of the array, so
only `_ge_entries_in_sector = 4` entries fit and the walk is bounded to `_ge_nentries_used = 4`. On
this disk **entry 0 is the small vendor partition**. The filesystem-bearing partition — the one an
OS would mount, necessarily the large extent — is at a **higher entry index that one array sector
does not reach**.

So the next rung is not another magic hunt; it is **walking the whole entry array** (7 sectors at
128 bytes/entry × 28 entries) and selecting the entry whose extent is large — the data partition —
then reading *its* superblock. That is a read-count change with no new device mechanism, the same
shape rungs 53→54 had.

## The record

- **`armed-storage-63c6d77d` is SPENT.** Below it, `armed-storage-e8597e3e` (rung 53) was already
  spent; rungs 54/55/56 were parked and are now answered by this one press.
- **NO BYTE OF THE MEDIUM MOVED** — three CMD17 reads, `fastboot boot` only, nothing flashed.
- **THE GOAL IS NOT MET** — the first partition is not a filesystem, no filesystem is mounted, no
  TWRP was written. But the storage frontier moved from *"is there a filesystem here"* (rung 55's
  question, answered **no** for partition 1) to *"which partition is the filesystem"* (rung 57's).