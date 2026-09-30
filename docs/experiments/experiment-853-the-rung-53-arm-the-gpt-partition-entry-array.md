# 853 — the rung-53 arm: the GPT partition entry array read at LBA 2, and the first partition decoded

**A BUILD AND A PARK.** Arm `armed-storage-e8597e3e`, `STAGE90_XNU_STORAGE_PROBE=52` (**VALUE 52 =
ORDINAL RUNG 53**), entry bin `e8597e3e…` 5,585,532 B, entry elf `878a5e2c…` 6,776,176 B, payload
`stage90-qcdt.img` `d70ea17d…` 8,605,696 B, `stage90.img` `2cfd07aa…` 6,084,608 B; readiness 5 of 5,
park 11 of 11; **the entry-group page move fired this time** (`STAGE90_XNU_SEAM_LR` `0x8004c2dc` →
`0x8004d2dc`). **PARKED, NOT PRESSED, NOT ARMED.**

---

## 1. It is the rung-52 press's own frontier, and only the sector moves

The rung-52 press (852) decoded the GPT header and measured **`_gp_entry_lba = _gp_next_sector = 2`** —
the 128-byte partition **entry array lives at SECTOR 2** — beside its own `_gp_arg_sector = 1`, so the
header's `PartitionEntryLBA` and the sector that rung read disagreed **on purpose** (`_gp_next_is_arg =
0`). **Rung 53 makes them equal**: the read's sector variable `st_read_lba` moves **1 → 2**, exactly the
same one-number move rung 51 made (0 → 1). Nothing else about the read changes — the command, the flags,
the block length CMD16 set, and the reset-before-read order are rung 51's byte for byte.

`st_gpt_entry_parse`, called **once** by `st_cmd_path` after `bl <st_gpt_parse>` (which itself runs after
`bl <st_read_single_block>`), is the whole new body. It DECODES the 512 bytes the sector-2 read landed —
the third reader of the family (`st_mbr_parse` at rung 49, `st_gpt_parse` at rung 52) — and sends no
command, opens no window and touches no register.

## 2. The bound that is the whole safety of this rung

The GPT header declares **`NumberOfPartitionEntries = 28`** entries of 128 bytes each: a **3,584-byte
array spanning SEVEN sectors**. But this rung's CMD17 reads **one 512-byte sector**, landing in
`st_read_block[ST_EXT_CSD_WORDS]` = **128 words**. So the loop must step only over the entries that
physically fit:

| cell | value | meaning |
| --- | --- | --- |
| `_ge_nentries_decl` | **28** | the header's own `NumberOfPartitionEntries` (the whole array) |
| `_ge_stride_words` | **32** | `SizeOfPartitionEntry / 4` = 128 bytes |
| `_ge_entries_in_sector` | **4** | `ST_EXT_CSD_WORDS / stride` = `128 / 32` |
| **`_ge_nentries_used`** | **4** | **`min(28, 4)` — the loop bound actually used** |

**A reader who walked all 28 would index `st_read_block[27*32 + 10]` = word 874 of a 128-word array** and
decode a partition extent out of adjacent statics, with every cell still reading as a plausible
partition. **A reader who walked a fixed four (as the MBR has) would be right by luck on this disk and
wrong on the next.** Neither is acceptable, so the walk is bounded by the *smaller* of the header's
declared count and the sector's capacity, and the capacity is published as a cell of its own so the
distinction between 28 and 4 is a reading and not a sentence ([[mi4-one-value-two-definitions]]).

## 3. The claims that are build refusals, not sentences

New clause **`xnu_entry_852`** reads `st_gpt_entry_parse`'s **linked** disassembly and refuses unless:

- **every `bl` in its body is to `entry_live_write`** — 32 of 32 — so the body makes no device access, and
  **no `0xf9824xxx` controller-window reference appears at all** (count required to be 0);
- **the sector-capacity divide is present** (a `udiv` or `lsr`-class instruction), because a body that
  dropped the `min(declared, capacity)` bound would compile, publish, and read past the 128-word buffer;
- it is **called exactly once** by `st_cmd_path` and that call site is **after** the header decode
  (`bl <st_gpt_parse>`), so it consumes the header's carried shape rather than reading a buffer the header
  no longer occupies.

And **`xnu_entry_849`**'s value-52 half reads the instruction that fills `st_read_lba` out of the linked
image (found by the same `[base, #off]` the argument load used, resolved to the `st_read_lba` symbol) and
**refuses unless its immediate is 2** — so *"the sector variable holds 2 and is stored before it is read"*
is a build refusal, and rung 51's 1 and rung 53's 2 cannot be confused (they are one immediate on one
instruction, with no cell distinguishing them by shape).

## 4. Read the rows

- **`_rd_lba = _rd_arg = 2`** with **`_rd_arg_lba_agree = 1`** — **THE COMMAND ADDRESSES SECTOR 2**, the
  128-byte partition entry array, and the transfer is a clean 512-byte read out of TRANSFER state
  (`_rd_complete = 1`, `_rd_err = 0`, `_rd_state = 4`, `_rd_words_gated = 0x80`).
- **`_ge_arg_is_arr = 1`** — **the cell that flips this rung**: it holds the read's sector (`_rd_lba`)
  against the header's OWN `PartitionEntryLBA` (`_ge_arr_sector`) and reads 1 only when the two
  independent derivations agree. It is **0 at value 51 and 1 at value 52**; a build that stored the wrong
  number would publish 0 here with every other cell reading as success.
- **`_ge_found = 1` beside `_ge_blank = 0`** — **a non-empty partition at entry `_ge_index`**, and
  **`_ge_first_lba`/`_ge_last_lba`** (words 8/10) are its `StartingLBA`/`EndingLBA` — **the numbers a
  filesystem mount needs**. `_ge_part_sector = _ge_first_lba` carries that first LBA as the next arm's
  CMD17 argument.
- **`_ge_type_w0..w3`** — the entry's 16-byte type GUID. All-zero names an unused slot, so
  `_ge_blank = 1` with `_ge_found = 0` is a disk whose entry 0 is empty — a different medium from one
  whose walk never ran, and the reason the blank flag is pre-registered.
- **`_ge_hdr_ok`** — **the check that the buffer is an entry array and NOT the header**: it re-tests the
  `EFI PART` signature, which must **NOT** appear. **0 on a well-formed entry array is the correct
  direction**; a `1` would be a sector-2 read that did not move, and `_ge_arg_is_arr = 0` beside
  `_ge_arr_sector = 2` would name the inversion.
- **`_ge_arr_in_range = 1`** and **`_ge_part_in_range = 1`** — the array's sector and the partition's first
  LBA are both inside the 30,777,344-sector card, so the next arm's read is addressable.

## 5. Safety

**It moves no byte of the medium and touches no device register it did not touch before.** CMD17 reads, so
the card stays in TRANSFER state with its block length and its CSD/EXT_CSD exactly as rungs 42–52 left
them; **only the sector NUMBER moves 1 → 2**. The entry image grew (5,585,532 → **5,585,532** bytes; the
new body is compiled in) and **the entry-group page move fired this time**: `STAGE90_XNU_SEAM_LR` moved
`0x8004c2dc` → **`0x8004d2dc`** (+0x1000 exactly). The address is pinned in TWO entry-side files
(`entry_trace.c`'s `STAGE90_XNU_SEAM_LR` and `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`); both were
edited, and **the build's own clause caught the move, not a run**
([[mi4-entry-group-page-move-pins-two-copies]]). Rung 52's header decode was a **straight line** and did
not move it; rung 53's entry **walk is a loop** and did. `STAGE90_XNU_PWR_WAIT_TICKS=384000` is inherited
from rung 9 unchanged — 20 ms at this device's own 19,200,000 Hz. `fastboot boot` only; nothing flashed;
nothing written to storage.

**NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the park is a
RECORD and not a queue. THE PRESS IS THE OPERATOR'S. THE GOAL IS NOT MET** — no filesystem superblock read
yet, no filesystem, no mount; TWRP-to-storage stays withheld.