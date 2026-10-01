# 859 — THE RUNG-57 ARM: THE WHOLE GPT ENTRY ARRAY WALKED, AND THE LARGE PARTITION SELECTED

**A build and a park, and nothing else; PARKED, NOT PRESSED, NOT ARMED.**

Arm `armed-storage-71b54d73`, `STAGE90_XNU_STORAGE_PROBE=56` (**VALUE 56 = ORDINAL RUNG 57**),
entry bin `71b54d73…` 5,585,532 B (size-neutral against rung 56's), entry elf `d672ff8e…`
6,766,440 B, payload `stage90-qcdt.img` `5b513c30…` 8,605,696 B, `stage90.img` `55459ca8…`
6,084,608 B; readiness 5 of 5, park 11 of 11; **the entry-group page move did NOT fire this rung**
(`STAGE90_XNU_SEAM_LR` stays `0x8004d2dc`, rung 53's value).

## What the rung is, and why it is a selection rather than a magic hunt

The rung-56 press (858) settled three parked rungs at once. The whole GPT chain works — three clean
CMD17 transfers landing sectors 1, 2 and `0x24` — and the partition at **entry 0** is a ~2 MB extent
(`_ge_first_lba = 0x22`, `_ge_last_lba = 0xfff`) whose superblock sector holds **ARM code**
(`_pt_w0 = 0xe1a04614`, `_pt_fs_found = 0`). The header declares `NumberOfPartitionEntries = 28` of
`SizeOfPartitionEntry = 128` bytes = 3584 bytes = **seven 512-byte sectors**, and rung 54's single
array sector reaches only **four** of the 28 entries.

So this rung reads the **whole array**, one CMD17 per sector, decoding each sector's entries while
*that* sector is in the buffer — the same "decode while you know what the buffer holds" discipline
rung 54 established, applied once per sector — and carries the `StartingLBA` of the entry whose
extent `EndingLBA - StartingLBA` is **largest**. **The last read lands that partition's superblock,
not partition 1's.**

**Why by extent and not by name.** The Mi 4's GPT geometry is recorded **nowhere** in this archive —
`docs/reference/local-device-findings.md:72-98` gives name→devnode and sizes, and there is no
on-disk GPT geometry and no name→entry map. The data partition cannot be named from these sources.
Every OS-mountable filesystem partition is the large one; the small ones are firmware and vendor
blobs. The extent is the reading, and `_ge_best_extent` / `_ge_scan_sector` **publish the selection**
so it is checkable rather than inferred.

## The sector map is the ordinal

Rung 54's selector returned `1` for the header and `2` for the array; rung 55's third arm returned
`st_gpt_part_sector + ST_FS_SB_SECTOR_OFF`. Rung 57 replaces that with a **count**:

| ordinal | sector |
| --- | --- |
| `0` | `ST_GPT_HEADER_SECTOR` = 1 — the GPT header |
| `1 .. st_gpt_array_nsec` | `st_gpt_entry_lba + (ordinal - 1)` — the entry array, one sector per ordinal |
| past the bound | `st_gpt_data_sector + ST_FS_SB_SECTOR_OFF` — the **selected** partition's superblock |

`st_gpt_array_nsec` is `ceil(nentries * entry_size / ST_MMC_BLK_LEN)` (7 on this disk), computed in
`st_gpt_parse` from the header, and clamped by `ST_GPT_ARRAY_SCAN_MAX` = 32 — a ceiling on the
**header's arithmetic**, not on this disk's table, so a malformed header whose product exploded
cannot make the walk run off the end.

## The carries

- `st_gpt_data_sector` — the selected partition's `StartingLBA`, the value the fourth read addresses.
- `st_gpt_best_extent` — the largest `EndingLBA - StartingLBA` seen so far; the selection is by this.
- `st_gpt_scan_sector` — the array sector ordinal holding the selected entry (`arr + sec_idx`), so
  which sector won is readable and not inferred.
- `st_gpt_array_nsec` — the walk's length.

`st_gpt_entry_parse(sec_idx)` now takes the array-sector ordinal, computes
`base = sec_idx * cap`, `nent_used = min(nent - base, cap)`, and resets `st_gpt_part_sector` only
when `sec_idx == 0` — so rung 55's carry survives the later sectors even though the walk no longer
uses it as the data-partition selector.

Cell names: `_ge_data_sector`, `_ge_best_extent`, `_ge_scan_sector`, `_ge_data_in_range`,
`_ge_scan_idx`, and `_ge_arg_is_arr` (split into a **range** form at value 56 — the read's sector
inside `[arr, arr + nsec)` — versus the old **equality** form).

## The build counts hold because a `bl` count reads call sites

The walk is a **loop in `st_cmd_path`**, so the *textual* `bl` counts are unchanged: three
`st_read_single_block` calls (header / the array loop / the superblock), three `st_data_reset`, one
`st_gpt_entry_parse`, one `st_part_parse`, one `st_part_geom`. `blk43_calls` and `dr47_calls` stay at
**3**, `ge_calls` at **1** — a body that grew by a loop is still one call site.

## A build-clause defect was found and repaired, and the same class as rungs 55 and 56

The first cut wrote the bound inside the loop:

```c
for (ge_sec = 0u; ge_sec < st_gpt_array_nsec; ge_sec++) { ... }
```

The loop body's first instruction is a **branch target** (the back-edge lands there), so it ends a
linear region and the device-access classifier's `epoch` advances — which invalidates every base
materialized before it, **including the loop-invariant address of the bound that GCC had hoisted
above the loop**. The classifier therefore **refused a correct build** with a bare
`NODECL-8056-0:ldr`. Instrumenting the classifier in place showed exactly this: at the first
`ldr r3,[r4]` (the loop test) `hiep == loep == epoch`, `fresh = 1`; at the second (the back-edge
test) `epoch` had advanced past both, `fresh = 0`, and the address was enumerated instead of resolved.

**The repair is in the SOURCE**: the bound is read into a local once above the loop —

```c
ge_nsec = st_gpt_array_nsec;
for (ge_sec = 0u; ge_sec < ge_nsec; ge_sec++) { ... }
```

so the only `movw`/`movt` for that address is outside every loop region and the load inside the body
is against a register the compiler keeps across the whole loop. **This is a codegen artifact, not a
defect in the arm** — `mi4-a-claim-in-a-comment-is-not-a-check`, the same family as rung 55's
address-vs-text repair and rung 56's relative-offset repair.

The `st_cmd_path` image-access clause was then widened from **EMPTY** to **exactly that one word at
its own `nm` address**, classified `IMG:ldr` — so the property is the *identity* of the single access
rather than the absence of any. An enumeration of `IMG:ldr` alone would pass a body that read any
symbol anywhere; the address is read out of `nm` and held against the one entry the classifier
reports.

## The new clause `xnu_entry_849`'s value-56 arm

`nm`'s `st_gpt_array_nsec` (offset 0), `st_gpt_entry_lba` (offset 4) and `st_gpt_data_sector`
(offset 8) are **three consecutive `.bss` words**, and the compiler materializes the base **once** and
reaches all three by offset. The clause therefore reads the **base binding** and the three offsets and
holds the base against the `nm` address of `st_gpt_array_nsec` — proving the selector addresses *that
object* (**m828's rule: prove the binding, not the instruction shape**) — and independently checks the
`.bss` adjacency (4 and 8) so offsets 4 and 8 are known to name the array base and the data
partition's `StartingLBA` and not two arbitrary neighbours. It also asserts the `sub #1` (the
`(ordinal - 1)` of the array map) and `add #2` (`ST_FS_SB_SECTOR_OFF`) instructions.

**A selector that still loads `st_gpt_part_sector` here is rung 55 wearing rung 57's name** — one
read of one array sector, and the superblock of whatever partition happens to be first.

## What it does not do

- **It moves no byte of the medium and touches no device register rung 51 did not.** It is a
  read-**count** change on the same CMD17 path: four-plus CMD17 reads, no new command, no new
  register, no new window. The card stays in TRAN with its block length exactly as rung 56 left it.
- `STAGE90_XNU_PWR_WAIT_TICKS=384000` is inherited from rung 9 unchanged.
- `fastboot boot` only; nothing flashed; nothing written to storage.

## The record

- The park block is `# 859 -` in `records/revert-set.txt`, set `armed-storage-71b54d73` (the set's
  name is the sha256 prefix of `xnu_arm_entry.bin`, per `tools/check_set_name_rule.sh`).
- Two stale park directories from the aborted 856 branch were renamed: `armed-storage-afbb4849` (a
  rung-55 arm whose design rung 57 superseded) and `armed-storage-d301bcb5` (rung 54) are now
  `…-spent`, so no park directory name collides with a record set name (58 storage parks, 71 sets,
  the checks agree).
- **NO PRESS HAS BEEN MADE AND NONE IS OWED: this arm is PARKED and NOT ARMED-FOR-PRESS, and the
  park is a RECORD and not a queue. THE PRESS IS THE OPERATOR'S.**
- **THE GOAL IS NOT MET** — a selected partition is not a mounted filesystem. No filesystem is
  mounted; no ISO 9660, no ext4, no f2fs; **TWRP-to-storage stays withheld.** But the storage frontier
  moved from *"which partition is the filesystem"* (rung 57's own question) to *"does that partition's
  superblock read as a filesystem"* — answered by the press that reads this arm.

## The operator's envelope (2026-10-01)

The operator relaxed the storage envelope mid-session: repartitioning is **permitted**, TWRP and
Android may **both** be sacrificed, and the **only** thing that must survive is `fastboot`
reachability. That supersedes the old "no byte of the medium may move" rule **for the storage goal**,
and makes this read rung a **prerequisite** for any write rung: a write needs to know which sectors
hold `sbl1`/`aboot`/`rpm`/`tz` before it can touch anything. The press remains the operator's, and
every medium write must be pre-registered against "can `fastboot` still be entered if this lands
wrong".