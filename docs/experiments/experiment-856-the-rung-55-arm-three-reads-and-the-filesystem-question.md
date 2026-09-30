# 856 — THE RUNG-55 ARM: THREE READS IN ONE BOOT, AND THE PARTITION'S OWN SUPERBLOCK

**A build and a park, and nothing else; PARKED, NOT PRESSED, NOT ARMED.**

Arm `armed-storage-afbb4849`, `STAGE90_XNU_STORAGE_PROBE=54` (**VALUE 54 = ORDINAL RUNG 55**),
entry bin `afbb4849…` 5,585,532 B (size-neutral against rung 54's), entry elf `b89aac22…`
6,766,272 B, payload `stage90-qcdt.img` `a54ef499…` 8,605,696 B, `stage90.img` `63b1a478…`
6,084,608 B; readiness 5 of 5, park 11 of 11; **the entry-group page move did NOT fire this rung**
(`STAGE90_XNU_SEAM_LR` stays `0x8004d2dc`, rung 53's value), so the seam pair in `entry_trace.c`
and `run_and_capture.sh` is unchanged.

## The frontier this rung is about

The rung-54 arm's frontier is `_ge_part_sector` — the first partition's `StartingLBA`, carried out
of the GPT partition entry array (entry words 8/10) — and the goal names what hangs off exactly that
address: *is there a filesystem here*.

Both filesystems this device could carry put their superblock at **partition byte 1024 = sector 2**.
Verified in the vendor tree rather than assumed:

| Filesystem | Struct | Magic | Where in the superblock |
| --- | --- | --- | --- |
| ext2/3/4 | `struct ext4_super_block`, `ext4.h:1009` | `0xEF53` = `EXT4_SUPER_MAGIC`, `magic.h:23` | `__le16 s_magic` at superblock byte 56 (`ext4.h:1025`) = **word 14**, low half |
| f2fs | `struct f2fs_super_block`, `f2fs_fs.h:59` | `0xF2F52010` = `F2FS_SUPER_MAGIC`, `f2fs.h:43` | the struct's **first field** = superblock byte 0 = **word 0** |

So the third read addresses `st_gpt_part_sector + ST_FS_SB_SECTOR_OFF`, with
`ST_FS_SB_SECTOR_OFF = 1024/512 = 2` (`_Static_assert`ed to 2), and the magic lands **in bounds at
the buffer's own first words**. **A read at `st_gpt_part_sector` alone would land partition byte 0,
where no filesystem puts a magic** — `_pt_fs_found` would read 0 for a filesystem that is there.
This is also why the first design of this rung — reading word 256 of the buffer for a superblock at
partition byte 1024 but buffer offset 1024 — was wrong and was replaced: `st_read_block` is 128
words (`ST_EXT_CSD_WORDS = ST_EXT_CSD_LEN / 4`), so `st_read_block[256]` is **out of bounds** and
reads adjacent statics ([[mi4-stand-in-size-is-not-value]]).

## Why the arm is a deliberate SUPERSET

Rung 54 is **PARKED and not pressed**, so `_ge_part_sector` has never been read on hardware. A
rung-55 arm cannot pin its third read to a constant no press has produced — that would be a build
clause asserting a value nothing measured, which is
[[mi4-a-claim-in-a-comment-is-not-a-check]]'s subject: *a clause whose expected value is unmeasured
cannot be satisfied by anything*.

So the third read consumes the **living carry** `st_gpt_part_sector` that `st_gpt_entry_parse`
fills **in this same boot**, and the arm is a strict superset of rung 54's shape rather than a
one-constant step from it. The cost is one extra read in a boot that already reads twice; the
benefit is that no number in the arm is a guess.

## The order, which is the whole rung

`st_cmd_path` calls, in sequence:

1. `st_data_reset` + `st_read_single_block` — the **FIRST** CMD17, sector 1 (`ST_GPT_HEADER_SECTOR`);
2. `st_mbr_parse` — the protective-MBR walk on the same sector (rung 48's decode, kept);
3. `st_gpt_parse` — the header is decoded **NOW**, while the buffer is *known* to hold sector 1, and
   its three shape fields (`NumberOfPartitionEntries`, `SizeOfPartitionEntry`,
   `PartitionEntryLBA`) are **carried out of that buffer** before the next read overwrites it;
4. `st_data_reset` + `st_read_single_block` — the **SECOND** CMD17, sector 2 (`ST_GPT_ENTRY_ARRAY_SECTOR`);
5. `st_gpt_entry_parse` — the entry array is walked and the first non-empty entry's `StartingLBA` is
   carried in `st_gpt_part_sector`;
6. `st_data_reset` + `st_read_single_block` — the **THIRD** CMD17, sector
   `st_gpt_part_sector + ST_FS_SB_SECTOR_OFF`;
7. `st_part_parse` — the filesystem decode.

**The third read MUST sit between the entry decode (which fills the carry) and the filesystem
decode (which consumes it)**, and `xnu_entry_855` refuses any other order: the decode above the
entry decode would consume `st_gpt_part_sector` while it is still 0 (the read would land sector 2,
re-reading the array), and a decode above the third read would decode a buffer the read never
filled.

Every read re-issues `st_data_reset` first: rung 44's press measured `DATA_INHIBIT` refusing CMD17
in the same boot it was set, and the transfer above sets it again. `blk43_calls` and `dr47_calls`
are asserted **3** — ONE is rung 53, TWO is rung 54, and one reset cannot precede three reads.

## The sector selector, and a build clause that read the wrong artifact

`st_sector_for_read(ordinal)` is a `noinline, noclone` helper with **three** arms:

```c
if (ordinal >= 1u) {
    if (ordinal >= 2u)
        return st_gpt_part_sector + ST_FS_SB_SECTOR_OFF;
    return (uint32_t)ST_GPT_ENTRY_ARRAY_SECTOR;
}
return (uint32_t)ST_GPT_HEADER_SECTOR;
```

It stays `noinline` for the two measured reasons the rung-54 clause's own comment names: a `const`
array indexed by a runtime ordinal compiled a literal-pool base register the device-access
classifier could not resolve (`NODECL-8049-96:ldr`), and a plain two-arm `if` was **tail-merged with
the header arm laid out-of-line at a higher address**, so a clause reading the linked disassembly
for store ORDER reported the branches in the opposite order to execution. A `noinline` body with
both constants as plain immediates is a property of one named function rather than of a layout.

**And `xnu_entry_849`'s value-54 half had to be repaired before it would accept a correct body.** Its
first draft grepped the selector's disassembly for the TEXT `<st_gpt_part_sector>` and refused when
the count was zero. That is the shape a relocatable reference takes in an **unlinked** object — in
the **linked** image the carry is reached by a `movw`/`movt` pair naming the *absolute address*
`0x805601a4`, and the symbol name `<st_gpt_part_sector>` is **present in no disassembly at all**. The
grep read 0 on a **correct** body and refused the build.

The repair decodes the pair the compiler actually emitted and holds it against the address `nm`
gives the symbol — **by value, not by name**:

```
blk43_sel_lo / blk43_sel_hi  = the movw/movt immediates in the selector body
blk43_sel_lo_exp / _hi_exp   = st_gpt_part_sector & 0xFFFF / (>>16) & 0xFFFF
```

and a separate assertion requires an `add r?, r?, #2` in the body, so a selector that returns
`st_gpt_part_sector` **without** the superblock offset is refused as well. The lesson is the one
`mi4-a-claim-in-a-comment-is-not-a-check` records: an assertion whose subject is **text the artifact
never contains** is not a weak check, it is a check of something else — and it was caught by running
it, not by reading it.

## The filesystem decode

`st_part_parse` (`#if STAGE90_XNU_STORAGE_PROBE >= 54`) is a body that calls **nothing but
`entry_live_write`**, touches **no controller-window register**, and reads only the buffer the third
read filled. It publishes:

- `_pt_called` — the decode ran;
- `_pt_hdr_ok`, `_pt_mbr_ok` — the **negative signature checks**: the third read's buffer must hold
  NEITHER `EFI PART` nor the MBR signature (`0xAA55` at bytes 510-511), because a superblock sector
  is neither. A **positive** `_pt_hdr_ok` is the rung-53 defect's shape arriving in the third read;
- `_pt_w0` — the buffer's raw word 0, so the comparison below is checkable from outside;
- `_pt_f2fs_magic` / `_pt_f2fs_ok` — word 0 against `F2FS_SUPER_MAGIC` = `0xF2F52010`;
- `_pt_ext_magic` (word 14 masked to 16 bits, because `s_magic` is a `__le16` and not a whole word) /
  `_pt_ext_ok` — against `0xEF53`;
- **`_pt_fs_found`** — 1 iff either magic fires. **This is the cell the goal asks for.**
- `_pt_part_sector` / `_pt_sb_sector` / `_pt_arr_sector` — the partition's first LBA, the superblock
  sector reached, the array's own sector;
- `_pt_arg_sector` / `_pt_next_is_arg` — the sector the third read **actually addressed** against the
  sector the table named. This is the one cell neither the build nor the decode can settle before a
  press, and the same tension cell rungs 49→51, 52→53 and 53→54 each carried;
- `_pt_part_in_range` / `_pt_cap` — the partition's sector is inside the card before any read of it
  is trusted.

## The build refusals, not sentences

- `xnu_entry_849`'s value-54 half: the selector's carry **address by value** and the `+2` offset.
- `xnu_entry_849`: `st_read_single_block` and `st_data_reset` each called **THREE** times by
  `st_cmd_path`.
- `xnu_entry_855`: `st_part_parse`'s linked body calls only `entry_live_write`, references no
  `0xf9824xxx` window, materializes **both** magics (ext4's `#61267` required, f2fs's `#8208` /
  `#62197`), is called **exactly once**, **after** `st_gpt_entry_parse`, with the **last**
  `st_read_single_block` **between** them.

## What it does not do

- **It moves no byte of the medium and touches no device register rung 51 did not.** CMD17 reads,
  three times; the card stays in TRAN with its block length and CSD/EXT_CSD exactly as rungs 42-54
  left them.
- `STAGE90_XNU_PWR_WAIT_TICKS=384000` is inherited from rung 9 unchanged — 20 ms at this device's own
  19,200,000 Hz.
- `fastboot boot` only; nothing flashed; nothing written to storage.

## The record

- The park block is `# 856 -` in `records/revert-set.txt`, set `armed-storage-afbb4849`.
- **NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the
  park is a RECORD and not a queue. THE PRESS IS THE OPERATOR'S.**
- **THE GOAL IS NOT MET** — a superblock magic is not a mount and not a filesystem read. No
  filesystem is mounted, no ISO 9660, no ext4, no f2fs; **TWRP-to-storage stays withheld.**