# 855 — the rung-54 arm: two reads in one boot — the header captured at sector 1, the GPT partition entry array decoded at sector 2

**A BUILD AND A PARK.** Arm `armed-storage-d301bcb5`, `STAGE90_XNU_STORAGE_PROBE=53` (**VALUE 53 =
ORDINAL RUNG 54**), entry bin `d301bcb5…` 5,585,532 B, entry elf `3c816aa3…` 6,766,240 B, payload
`stage90-qcdt.img` `77e36074…` 8,605,696 B, `stage90.img` `db736eae…` 6,084,608 B; readiness 5 of 5,
park 11 of 11. **The entry-group page move did NOT fire this rung** (`STAGE90_XNU_SEAM_LR` stays
`0x8004d2dc`, rung 53's value). **PARKED, NOT PRESSED, NOT ARMED.**

---

## 1. It is the rung-53 press's own frontier, and the frontier is the ORDER

The rung-53 press (854) landed sector 2 cleanly — `_rd_lba = _rd_arg = 2`, `_rd_complete = 1`,
`_rd_err = 0`, `_rd_state = 4` — so **the sector move works**. But `st_gpt_parse` — rung 52's header
decode — *still runs after the read*, and rung 53 moved the read's sector to 2, so the header decode
now decodes the **entry array** as if it were a header and fills its carries from sector 2's (zero)
words 18/20/21: `_gp_sig_ok = 0`, `_gp_entry_lba = _gp_nentries = _gp_entry_size = 0`. The entry
decode then consumes a shape the header decode had just zeroed: `_ge_nentries_used = min(0, 128) = 0`
(**the walk never ran**), `_ge_found = 0`, and **`_ge_arg_is_arr = 0`** (the read's sector `_rd_lba = 2`
against the header's own `PartitionEntryLBA` `_ge_arr_sector = 0`).

**Rung 53 read ONE sector, so the buffer could never hold the header AND the array.** Rung 54 reads
**both**, in the order that makes each decode see the right bytes. It is the same one-number discipline
every rung since 50 has used, applied to a **second CMD17** ([[mi4-a-lower-rungs-side-effect-poisoned-the-rung-above]],
the **buffer** form: not a durable device side effect but a buffer the step above overwrote).

## 2. The order is the rung

`st_cmd_path` calls, in sequence:

1. `st_read_single_block` — the **FIRST** CMD17, sector 1;
2. `st_mbr_parse`;
3. **`st_gpt_parse`** — the header is decoded **now**, while the buffer is *known* to hold sector 1;
4. `st_data_reset`;
5. `st_read_single_block` — the **SECOND** CMD17, sector 2;
6. **`st_gpt_entry_parse`** — the entry array is walked out of the buffer the second read filled.

The sector comes from `st_sector_for_read(st_read_count)`, a **`noinline`** helper whose two immediates
are `1` (`ST_GPT_HEADER_SECTOR`) and `2` (`ST_GPT_ENTRY_ARRAY_SECTOR`). The ordinal is the
**pre-increment** counter, bumped just before each command, so the FIRST read sends the header's sector
and the SECOND the array's.

**The helper is `noinline` on purpose, and that choice is a repair of a real failure.** A `const` array
indexed by the ordinal compiled the sector load into a literal-pool base register the device-access
classifier could not resolve (`NODECL-8049-96:ldr`), failing an unrelated order assertion; and a plain
two-arm `if` was tail-merged by the compiler, with the header arm laid **out-of-line at a higher
address** than the array arm — inverting execution order versus the disassembly's line order. The
helper keeps the body a **single store** (so the rung-51/52 order clauses still apply) and makes the two
sector literals immediates in one named body the build clause can read by value.

## 3. The claims that are build refusals, not sentences

**`xnu_entry_849`'s value-53 half** resolves `st_sector_for_read` via `sym_addr`/`sym_size`, dumps its
linked body, greps for the immediates `#1` and `#2`, and **refuses unless both are present**; it also
requires `st_read_single_block` to call the helper **exactly once**. The same clause:

- requires `st_read_single_block` to be called **twice** by `st_cmd_path` (`blk43_calls == 2`; **ONE is
  rung 53 again**);
- requires `st_data_reset` to be called **twice** (`dr47_calls == 2`; **one reset cannot precede two
  reads**);
- requires `st_read_count` to be in the image (`blk43_img_want` widened for value 53).

**`xnu_entry_852`** still requires `st_gpt_entry_parse`'s body to call nothing but `entry_live_write`,
to touch no controller window, to hold the sector-capacity divide, and to be called **once after
`bl <st_gpt_parse>`**.

So *"the header is captured at sector 1, the array is read at sector 2, and both reads happen"* is a
**build refusal**, not a sentence ([[mi4-a-claim-in-a-comment-is-not-a-check]]).

## 4. Read the rows

| cell | reading |
| --- | --- |
| **`_rd_ordinal`** | **1 then 2** — WHICH read this is, published just before each command |
| `_rd_lba = _rd_arg` | `1` on the first read, `2` on the second; `_rd_arg_lba_agree = 1` |
| **`_gp_hdr_sig_ok = 1`** | the header decode re-tests `EFI PART` **on the buffer it decodes** |
| **`_gp_hdr_sector = 1`** | **the sector that buffer was read from** — the rung-53 defect as a cell |
| `_gp_sig_ok = 1`, `_gp_entry_lba = 2` | the header is a header again and names the array's sector |
| **`_ge_arg_is_arr = 1`** | **the array read's sector** against the header's OWN `PartitionEntryLBA` |
| **`_ge_found = 1`, `_ge_blank = 0`** | **a non-empty partition at entry `_ge_index`** |
| `_ge_first_lba` / `_ge_last_lba` | entry words 8/10 — **the extent a mount needs** |
| `_ge_type_w0..w3` | the entry's 16-byte type GUID (all-zero names an unused slot) |
| `_ge_nentries_decl = 28`, `_ge_stride_words = 32` | the array's own shape |
| `_ge_entries_in_sector = 4`, `_ge_nentries_used = 4` | **`min(28, 4)` — the bound the loop uses** |
| `_ge_arr_in_range = 1`, `_ge_part_in_range = 1` | the next arm's read is addressable |

**`_gp_hdr_sector` is the cell the correction needed.** It holds the sector the buffer was read from
beside `_gp_hdr_sig_ok`, so a header decode that ran on sector 2 reads `_gp_hdr_sig_ok = 0` with
`_gp_hdr_sector = 2` — exactly the rung-53 failure, named as a reading rather than a sentence.

## 5. The bound that is the whole safety of the run

The GPT header declares **`NumberOfPartitionEntries = 28`** entries of 128 bytes each: a **3,584-byte
array spanning SEVEN sectors**. But the read lands **one 512-byte sector** (128 words), so the loop is
bounded by the *smaller* of the declared count and the sector's capacity — `_ge_entries_in_sector =
128/32 = 4`, `_ge_nentries_used = min(28, 4) = 4`. **A reader who walked all 28 would index
`st_read_block[874]` of a 128-word array and decode a partition extent out of adjacent statics**, with
every cell still reading as a plausible partition. The capacity is published as a cell of its own so
the distinction between 28 and 4 is a reading ([[mi4-one-value-two-definitions]]).

## 6. Safety

**It moves no byte of the medium and touches no device register it did not touch before.** CMD17 reads,
so the card stays in TRANSFER state with its block length and its CSD/EXT_CSD exactly as rungs 42–53
left them; the command, the flags and the whole rung-51 cell set are byte for byte — **only a SECOND
read is added, at the header's own sector first**. The entry bin is **5,585,532 bytes — size-neutral
against rung 53's** (content different, `d301bcb5…`), because the new material replaced
`st_gpt_entry_parse`'s snapshot machinery: **in rung 54 `st_gpt_parse` runs *between* the two reads, so
the buffer still holds the header and no snapshot is needed.** The seam did not move
(`STAGE90_XNU_SEAM_LR = 0x8004d2dc`). `STAGE90_XNU_PWR_WAIT_TICKS=384000` is inherited from rung 9
unchanged — 20 ms at this device's own 19,200,000 Hz. `fastboot boot` only; nothing flashed; nothing
written to storage.

**NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the park is
a RECORD and not a queue. THE PRESS IS THE OPERATOR'S. THE GOAL IS NOT MET** — the partition entry's
own bytes are still not walked into a filesystem, no filesystem, no mount; TWRP-to-storage stays
withheld.