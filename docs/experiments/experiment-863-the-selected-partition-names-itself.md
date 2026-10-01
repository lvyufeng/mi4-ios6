# 863 — THE SELECTED PARTITION NAMES ITSELF, AND RUNG 57'S OWN WITNESS IS REPAIRED

**A build and a park, and nothing else; PARKED, NOT PRESSED, NOT ARMED.**

Arm `armed-storage-b00b87bb`, `STAGE90_XNU_STORAGE_PROBE=57` (**VALUE 57 = ORDINAL RUNG 58**),
entry bin `b00b87bb…` 5,585,564 B, entry elf `764ad976…` 6,767,120 B, payload
`stage90-qcdt.img` `61b4adae…` 8,605,696 B, `stage90.img` `0a1089b6…` 6,084,608 B; readiness
5 of 5, park 11 of 11, `make check` exit 0; **the entry-group page move did NOT fire this rung**
(`STAGE90_XNU_SEAM_LR` stays `0x8004d2dc`, rung 53's value).

`STAGE90_XNU_MOUNT` is back at its **default 0** — this is a **storage-ladder rung**, not a stack on
862's unpressed mount arm. 862 was parked and never pressed; the two tracks are separate and this
arm does not build on top of it.

## What the rung asks, and what it repairs

Rung 57 (859/858) walked the whole GPT entry array and **selected** the entry whose extent
`EndingLBA - StartingLBA` is largest, then read that partition's superblock. The 858 press proved the
chain works and that the **first** partition is not the filesystem.

By name on this archive's own findings the largest extent is `userdata` (p25, ext4) while the first
partition is `sbl1` (p1) — two **different** partitions. That difference is load-bearing, because
`st_part_parse`'s witness cells (`_pt_next_is_arg`, `_pt_part_sector`, `_pt_sb_sector`,
`_pt_part_in_range`) still compared the superblock road against **`st_gpt_part_sector` — the FIRST
partition** — while rung 57's selector reads **`st_gpt_data_sector` — the SELECTED one**. So a
**correct** rung-57 read logs `_pt_next_is_arg = 0`. That is
[`mi4-one-value-two-definitions`] again: one quantity ("the partition") with two readings, the code
using one while meaning the other. **The rung's two halves are (1) ask the medium what it called the
selected partition, and (2) publish a witness for the selected one beside the old first-partition one,
in the same capture.**

## Two carries, filled in the branch that already carried the selection

| carry | what | where |
| --- | --- | --- |
| `st_gpt_data_type[4]` | the selected entry's PartitionTypeGUID, words 0..3 = bytes 0..15 | written inside `if (extent > st_gpt_best_extent)` |
| `st_gpt_data_name[4]` | its PartitionName, words 14..17 = bytes 56..71 | same branch |

`ST_GPT_E_NAME_W0 = 14u` with `_Static_assert((ST_GPT_E_NAME_W0 * 4u) == 56u, …)`. Because both are
written in the **same branch** that already stored `st_gpt_data_sector`, the selection rule is
**untouched** and the carries can only ever describe the entry the selector will read.

## The name folds by CHARACTER, and a build caught it folding by WORD

The GPT name is 36 `__le16` characters; on this archive the labels are ASCII stored so that each
`char c` is the `__le16` value `c` (low byte = ASCII, high byte = 0). Word 0 therefore holds
characters 0 and 1, word 1 holds 2 and 3, and so on.

The **first** version of the fold read only `st_gpt_data_name[0]` and took its `>> 16` byte as
character 2. On the word stream that packs characters **0, 2, 4, 6** — `"uedt"`, not `"user"` — and
silently drops every second character. **That is the same defect class inside a decode**, and it was
caught by the new build clause, not by review: the clause reads the **shape** of the fold and refused
a body that did not do what the fold's own comment said.

The repaired fold, byte by byte:

```c
n0 = st_gpt_data_name[0];
p0 = (n0 & 0xFFu) | (((n0 >> 8) & 0xFFu) << 8) |
     ((st_gpt_data_name[1] & 0xFFu) << 16) | (((st_gpt_data_name[1] >> 8) & 0xFFu) << 24);
p1 = (st_gpt_data_name[2] & 0xFFu) | (((st_gpt_data_name[2] >> 8) & 0xFFu) << 8) |
     ((st_gpt_data_name[3] & 0xFFu) << 16) | (((st_gpt_data_name[3] >> 8) & 0xFFu) << 24);
```

`_dp_name_p0` = characters 0..3, `_dp_name_p1` = characters 4..7. The raw words are published too
(`_dp_name_w0..w3`), so a reader is not asked to trust the fold: the packing is checkable against the
words beside it.

The linked code for this packing is `uxth` (halfword zero-extend) plus `and #255` / `and #65280`
plus `orr …, lsl #16` — GCC does **not** emit `and #255` for every term. **The clause accepts
either spelling**, which is why the packing came to be inspected at all.

## The body reads no device and sends no command

`st_data_part` is `__attribute__((noinline, noclone))` so its body is read **by value**, not through
a layout (the tail-merge defect of 855, [`mi4-linked-code-order-is-not-source-order`]). The build
clause `xnu_entry_857` reads the **linked** image and requires:

- every `bl` in `st_data_part` is to `entry_live_write`;
- **zero** references to the controller window `0xf9824xxx`;
- the carries are reached **off one base register at offsets** — base-relative `ldr [rX, #off]`
  loads for `st_gpt_data_type`, `st_gpt_data_name`, the name's last word (`+12`), `st_read_lba`,
  `st_gpt_data_sector`, `st_gpt_best_extent` and `st_gpt_scan_sector`;
- offset-0 (`[rX]`) and offset-4 (`[rX, #4]`) loads from that base are **refused** — those are the
  walk's own bound and array base;
- the two carries are **adjacent** (`st_gpt_data_name` immediately follows `st_gpt_data_type`);
- the fold's structural shape (above);
- exactly **one** call to `st_data_part`, placed after `st_part_geom`.

**A measurement trap the clause walked into and out of.** The first cut hunted for the base through
a `movw`/`movt` pair taken with `head -1` — but the **first** such pair in the body is the
**key-string address**, not the base. The clause now counts `grep -c` matches for the base halves over
the **whole** body. That is [`mi4-measurement-defects`] one level inside a build clause.

## Reading the rows

| cell | reading |
| --- | --- |
| `_dp_called` | the body ran |
| `_dp_data_sector`, `_dp_best_extent`, `_dp_scan_sector` | rung 57's selection, re-published beside the identity |
| `_dp_data_sb_sector` | the **selected** partition's superblock — the address the last read landed |
| `_dp_type_w0..w3` | the entry's own machine-readable claim about what the partition is *for* |
| `_dp_name_w0..w3`, `_dp_name_p0`, `_dp_name_p1` | the human label, raw words plus the character fold |
| `_dp_name_nonzero` | the one bit a mount cares about — an unlabeled entry |
| `_dp_arg_sector` | `st_read_lba` — where the read actually went |
| `_dp_first_sb_sector` | the **first** partition's road, which `_pt_sb_sector` still names |
| `_dp_next_is_arg` | 1 only when the read landed the **selected** partition's superblock |
| `_dp_part_is_first` | 1 only when the two partitions share a `StartingLBA` |

**The row that matters is `_dp_next_is_arg = 1` with `_dp_part_is_first = 0`.** That is the correct
rung-57 read this rung makes readable: on it, `_pt_next_is_arg = 0` is explained as the
first-partition witness doing exactly what it was written to do, and both readings sit in **one** log
rather than one refuting the other.

**`_dp_part_is_first = 1` is the other reading, and it would be large**: it would say this disk's
largest extent *is* its first partition, the two-witness defect is inert here, and rung 56's
`_pt_fs_found = 0` needs another explanation. The two cells are published together precisely so those
two worlds are told apart by the log and not by this sentence.

A GUID→kind map is deliberately **not** carried: rung 56 measured entry 0's GUID as neither
EFI-system nor Linux-filesystem, so a map would be a promise this archive cannot keep.

## Nothing moved that must not move

- Payload switch record byte-identical to every rung since 23 (`6c2b6038…`, **MEASURED**). Only
  `STAGE90_XNU_ENTRY=1` is a payload switch and it is unchanged, so this **entry** switch moves the
  entry image only.
- Selector base binding intact: `st_sector_for_read` at `0x8000cff8` loads base `0x805601e4` at
  offsets 0/4/8.
- Build counts hold: `blk43_calls`/`dr47_calls` = 3, `ge_calls` = 1 — a textual `bl` count reads
  **call sites**, and the walk's loop is one site.
- The entry-group page move did **not** fire: `STAGE90_XNU_SEAM_LR` stays `0x8004d2dc` in
  `entry_trace.c` and `EXIT_POP_LR_LITERAL` in `run_and_capture.sh`.
- `STAGE90_XNU_PWR_WAIT_TICKS=384000`, inherited from rung 9, unchanged — this rung moves no wait,
  no bound, no byte of the medium, and touches no device register.

## Status

**PARKED** at `out/stage90/frozen/armed-storage-b00b87bb/` (11 members, plain `cp -r`), recorded in
`records/revert-set.txt` as `set=armed-storage-b00b87bb …` (11 member lines). Readiness **5 of 5**,
`tools/check_set_name_rule.sh` exit 0, `tools/verify_revert_set.sh --set=armed-storage-b00b87bb`
**11 ok / 0 failed**, `make check` exit 0.

**THE PRESS IS THE OPERATOR'S.** The park is a RECORD, not a queue; `fastboot boot` only, never
flash.

**THE GOAL IS NOT MET.** Naming the mountable partition is not a mounting a filesystem: no filesystem
is mounted, and **TWRP-to-storage stays withheld**.