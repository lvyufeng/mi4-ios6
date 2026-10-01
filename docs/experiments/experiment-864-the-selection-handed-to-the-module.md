# 864 — THE SELECTION HANDED TO THE MODULE, AND THE MEDIUM THAT SERVES ONE SECTOR

**A build and a park, and nothing else; PARKED, NOT PRESSED, NOT ARMED.**

Arm `armed-storage-7189e9b`, `STAGE90_XNU_STORAGE_PROBE=58` (**VALUE 58 = ORDINAL RUNG 59**) with
**`STAGE90_XNU_MOUNT=1`** — the mount switch `__wrap_mdevlookup` is behind. Entry bin `7189e9b7…`
5,585,564 B, entry elf `5740e649…` 6,767,508 B, payload `stage90-qcdt.img` `15ac0e3d…` 8,605,696 B,
`stage90.img` `b43d7e05…` 6,084,608 B; readiness 5 of 5, park 11 of 11, `make check` exit 0; **the
entry-group page move did NOT fire this rung** (`STAGE90_XNU_SEAM_LR` stays `0x8004d2dc` at
`entry_trace.c:2743`, `EXIT_POP_LR_LITERAL=0x8004d2dc` at `scripts/run_and_capture.sh:364`).

## What the rung asks

Rung 57 (859/858) walked the whole GPT entry array and read the large partition's superblock; rung 58
(863) asked that selection what it is called. **Neither of them moved a byte into anything a mounting
OS can address.** That is the gap 862 named when it registered a device: the device's `bdevsw`
`strategy` slot pointed at `eno_strat`, a body that refuses.

This rung is the coupling 862 built and could not fill: **the selection is handed to the module, and
the module's strategy serves that one sector and refuses everything else.**

**The root device does NOT move.** The RAM-disk-backed device 862 kept exec-able stays root; the
staged partition is an **addition at its own dev_t**, registered by `entry_root_media_stage` as disk
1. Nothing is mounted by this rung.

## The entry point, in five statements, and the order is the safety property

`entry_root_media_mount_disk` (`src/entry/entry_storage.c`, guarded `#if STAGE90_XNU_STORAGE_PROBE
>= 58`, called from `__wrap_mdevlookup` in `entry_trace.c` on the mount arm alone):

| # | statement | why it is here and not elsewhere |
| --- | --- | --- |
| 1 | `entry_storage_probe()` | **idempotent** (`g_storage_probed`), so this entry point is callable from the mount path with no second device bring-up — an idled boot already has the carries and the buffer; a boot that has not takes this call as the first idle exit's own act |
| 2 | `st_sel_publish()` | re-publishes `_gpt_data_sector`, `_gpt_best_extent`, `_gpt_scan_sector` under the **same names rung 57's reader already collects**, plus `_sel_sb_sector` and `_sel_valid` — a second **consumer** of those carries, not a second definition of them |
| 3 | `entry_storage_selected_pages()` | **its zero is a refusal, taken before any device register is touched** |
| 4 | `st_read_selected()` | rung 47's own pair, argument for argument |
| 5 | `entry_root_media_stage(...)` | the handover — the bytes are **copied** |

## The refusal, and why it is the load-bearing half

`entry_storage_selected_pages()` answers **0** when the walk selected nothing (`st_gpt_best_extent`
still `ST_GPT_SCAN_NONE`) or when the card's capacity was never read (`st_ext_sec_count` zero). On a
zero, `entry_root_media_mount_disk` publishes `xnu_live_storage_sel_bind_refused = 1` and returns
`-1` **without touching a register**.

That is rung 57's `_dp_part_is_first` discipline one level up: **an accessor that cannot know must be
readable as having DECLINED, never as having ANSWERED.** There is no fallback constant.

## The read is the same calls the rungs below make

`st_read_selected()` is `st_data_reset(); st_read_single_block(1u);` — the same one byte store and
the same body through the same door. The `1u` is what `entry_storage_probe`'s own chain passes (the
enable word the probe armed with); a different number here would be a second window discipline this
ladder does not have.

`st_read_lba` is written by `st_sector_for_read` **inside the read body**, so
`xnu_live_storage_sel_arg_sector` is necessarily the sector of the read that just ran: one boot, one
device action, one reading.

**`xnu_live_storage_sel_held` is the cell that says the bytes are the right ones.** It is rung 57's
own witness re-read here: 1 only when `st_read_lba == st_gpt_data_sector + ST_FS_SB_SECTOR_OFF`,
i.e. only when the read landed the **selected** partition's superblock rather than the header's or the
first partition's. The address the strategy will answer from is stated in the same capture as the
bytes.

`xnu_live_storage_sel_cap_sectors` reads **1**, and it is not a placeholder: one sector is the size of
the medium this image holds.

## The module moves bytes now

`st_media_strategy` (`src/platform/stage90_root_media.c`, at `0x80287b90`) is the slot 862 installed
`eno_strat` in. It calls `minor(buf_device(bp))` and then refuses, **in order**:

| refusal | meaning |
| --- | --- |
| `ENXIO` when `st_medium_staged == 0` | "**there is no medium**" — the mount path was reached without a handover |
| `EROFS` when `blkno` is past the one staged sector | "the filesystem asked for a block this image cannot serve" — **not** "the disk is full" |
| `EINVAL` for a blocksize or unit it does not know | the request is not one this device is shaped for |

Only then: `buf_map`, `bcopy`, `buf_unmap`, `buf_setresid`, `buf_biodone`.

`xnu_live_rootmedia_served` and `xnu_live_rootmedia_refused` are published **on every call**, not at
the end — a run that dies between them must still say which it was.
`_strategy_dev`/`_strategy_blkno`/`_strategy_count`/`_strategy_read` say which request was the one
served.

**The device's own geometry is overwritten, and that is the arm rather than an artifact**:
`st_media_blocksize[1] = 512`, `st_media_blockcount[1] = 1`, `st_media_flags[1] = ST_MEDIA_MDINITED |
ST_MEDIA_STAGED`, and `DKIOCGETMEMDEVINFO` redirected to the staged array. `DKIOCGETBLOCKCOUNT`
answering **1** is what stops a filesystem from being told it can address a disk it cannot read, and
`mi_base = (uintptr_t)st_medium_virt >> 12` is the number `mdevstrategy`'s `mdBase << 12` arithmetic
and `mockfs`'s `pager_map_to_phys_contiguous` both work in.

**The bytes are handed over, not shared.** `entry_root_media_stage` copies the sector's 128 words into
`st_medium_virt` before it returns, so nothing outside `entry_storage.c` ever holds a pointer into
`st_read_block` — which matters because a mounting OS may reach this device long after another read
has reused that buffer. `_stage_base`, `_stage_sector`, `_stage_pages`, `_stage_blocks`, `_stage_w0`
and `_stage_w1` put the sector's first two words in the log beside the sector they came from, so a
reader is not asked to trust the copy; a registration that could not find a major leaves `_staged` at
0 with `_stage_err` carrying the failure.

## The build clause is the reading

`xnu_entry_864` reads the **linked** image and asserts:

- `entry_root_media_mount_disk` (at `0x80013e04`) makes **exactly four** calls —
  `[entry_storage_probe st_sel_publish st_read_selected entry_root_media_stage]` — and no other
  callee;
- `st_read_selected` (at `0x8000df38`) calls `st_data_reset` **1** and `st_read_single_block` **1**;
- `st_media_strategy` calls all of `buf_device buf_blkno buf_count buf_flags buf_map buf_unmap
  buf_setresid buf_seterror buf_biodone` and `bcopy`, and calls **neither** `st_read_single_block`
  **nor** `entry_root_media_stage`.

The property being bought is that **the strategy answers from its own copy or not at all, and cannot
reach back into the ladder**.

## The bug this rung's own build found, one rung down

`xnu_entry_856` (rung 56's geometry clause) refused a **correct** body on this rung's first build.
The true root cause, found by disassembling `st_part_geom` in both images and comparing base-relative
deltas (identical: `0 4 20 24 40 56 76 96×5 100 104`), was the anchor's regex:

```
/ldr[[:space:]]+r[0-9]+, \[r[0-9]+, #[0-9]+\]/
```

**`ldr[[:space:]]` cannot match `ldrh`.** This rung's `.bss` perturbation made GCC read the 16-bit
`s_magic` with `ldrh r2, [r4, #124]` instead of word-14's `ldr r3, [r4, #620]`, so the anchor silently
fell back to **word 0's** `ldr r5, [r4, #68]` and every check built on it refused.

The repair is `ldr[bhs]?` with every check **bound to the anchor's own base register**. The
intermediate edit (subtracting a min-offset origin) was wrong and was replaced — the layout had not
moved at all.

This is `mi4-linked-code-order-is-not-source-order`'s lesson one level in: **read the BINDING, not a
layout, and let the pattern cover the width the compiler actually chose.**

A second defect was mechanical: `md_unexpected=$(... | grep -vE ...)` under `set -euo pipefail` killed
the build script silently when `grep -vE` found no unmatched symbols (exit 1), diagnosed with `bash
-x`. Fixed by wrapping the pipeline in `{ ... ; } || true`.

## Reading the rows

| cell | reading |
| --- | --- |
| `_sel_valid` | 1 only when the walk selected an entry |
| `_sel_bind_refused` | 1 = the accessor declined **before** any register was touched |
| `_sel_arg_sector`, `_sel_arg_ordinal` | where the read went |
| `_sel_held` | **1 = the read landed the SELECTED partition's superblock** |
| `_sel_cap_sectors` | 1 — the medium this image holds |
| `_sel_w0`, `_sel_w1` | the first two words of that sector |
| `_stage_sector`, `_stage_pages`, `_stage_blocks`, `_stage_base` | what was handed over, and where it landed |
| `_stage_w0`, `_stage_w1` | the staged copy's first two words |
| `_rootmedia_served`, `_rootmedia_refused` | the two counters, published on every call |
| `_strategy_dev`, `_strategy_blkno`, `_strategy_count`, `_strategy_read` | which request was served |

**The row that matters is `_sel_held = 1` with `_sel_bind_refused = 0`.** That is the medium the
strategy answers from being the selected partition's superblock — the only configuration in which the
bytes behind the device are the filesystem's and not the header's.

## Nothing moved that must not move

- Payload switch record byte-identical to every rung since 23 (`stage90-build-config.txt`
  `6c2b6038…`, **MEASURED**). Only `STAGE90_XNU_ENTRY=1` is a payload switch and it is unchanged, so
  this **entry** switch moves the entry image only. The payload artifacts do move (`stage90-qcdt.img`
  `61b4adae…` → `15ac0e3d…`, same size) because the entry image is embedded in them.
- Build counts hold: `blk43_calls`/`dr47_calls` = 3, `ge_calls` = 1.
- Selector base binding intact: `st_sector_for_read` at `0x8000cff8` loading base `0x805601e4` at
  offsets 0/4/8.
- The entry-group page move did **not** fire: `STAGE90_XNU_SEAM_LR` stays `0x8004d2dc` at
  `entry_trace.c:2743` and `EXIT_POP_LR_LITERAL` at `run_and_capture.sh:364`.
- `STAGE90_XNU_PWR_WAIT_TICKS=384000`, inherited from rung 9, unchanged — this rung moves no wait and
  no bound.
- **No byte of the medium moves, no device register this ladder has not already read is touched, and
  no command the ladder below does not already send is sent.** `fastboot boot` only, never flash.

## Status

**PARKED** at `out/stage90/frozen/armed-storage-7189e9b/` (11 members, plain `cp -r`), recorded in
`records/revert-set.txt` as `set=armed-storage-7189e9b …`. Readiness **5 of 5**,
`tools/check_set_name_rule.sh` exit 0, `tools/verify_revert_set.sh --set=armed-storage-7189e9b`
**11 ok / 0 failed**, `make check` exit 0.

**THE PRESS IS THE OPERATOR'S.** The park is a RECORD, not a queue; `fastboot boot` only, never flash.

**THE GOAL IS NOT MET.** A device that can *serve* one sector is not a mounted filesystem: the root
device is still the RAM-disk-backed one 862 kept exec-able, nothing is mounted, and
**TWRP-to-storage stays withheld**.