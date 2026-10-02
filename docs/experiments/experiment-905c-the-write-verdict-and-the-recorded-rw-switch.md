# Experiment 905c — the write verdict, and the rw switch bound to a record

**Arm `armed-storage-d6e2fbe6` (entry bin sha `d6e2fbe6…`, payload `b7bb6d08…`). BUILT AND PARKED, NOT
PRESSED.** The follow-up to 905b (`armed-storage-9d2dd2e4`), whose press **did not return** — the device
went dark and the RAM-console log died with the power, so the programming wait that 905b added was never
read. This arm does not re-litigate that wait. It carries the two other changes an adversarial
verification of 905b surfaced, and it is the arm to press once the device is back.

## Why this arm exists

905b added a bounded `DOING_WRITE|DAT_LINE_ACTIVE` poll after the CMD24 PIO loop, on a diagnosis read
out of the *previous* arm `bb2269bf`'s surviving log: a CMD24 that returns right after the last FIFO
word leaves the card programming the block (DAT0 held busy), and the next command — the launchd page at
LBA `0x400110` — CMD-TIMED OUT (`rd_complete=0`, `rd_err=0x00010000`). Of the boot's 62 reads, exactly
one differed from rung 904's, and it was the first command after the write. The wait is the correct
predicate (the vendor's `sdhci_finish_data` waits on the data-end interrupt; both busy bits were
measured stuck at `0x01e80106` 46 s after the write) and it is bounded at the vendor's own 1.25 s
(`ST_EXT_DATA_TICK_BUDGET`), so a card that never releases cannot hang the boot.

A 60-agent adversarial verification of 905b's fix **kept the diagnosis and the wait** and raised two
things it could not dismiss:

1. **The write body published no verdict.** It recorded `_wr_complete` and `_wr_err` from the *command*
   (CMD24's R1), then the programming wait, then restored the window. It never read the data-phase
   error bits out of `INT_STATUS` after the transfer. A card that ended the write in a DATA timeout, a
   CRC error, or a bad end-bit would therefore be published as a **success** — the read half has had
   exactly this cell since rung 42 (`_rd_int_status_end`, `_rd_int_data_err`) and the write half did
   not. This is the defect where a rejected write is indistinguishable from an accepted one.
2. **`STAGE90_XNU_HFS_ROOT_RW` was bound to no record.** The rw-root clear (`tools/hfs_patch_root_rw.py`'s
   guarded `vfs_clearflags(mp, MNT_RDONLY)` in `hfs_mountroot`) is compiled into the **pool** under
   `STAGE90_XNU_HFS_ROOT_RW`, a `build_xnu_arm_kernel.sh` env default that is **unset in a normal build**.
   Nothing required it with `HDD_WRITE=1`, and no record named it. So an arm could record `HDD_WRITE=1` —
   promising a writable root, `st_write_single_block` and all — while the pooled `hfs_vfsops.o` was built
   with the clear compiled **out**: the root mounts `MNT_RDONLY`, `st_media_strategy`'s `B_WRITE` branch
   is **never reached**, and the arm's own subject is absent from the image it ships. That is
   `mi4-off-option-two-spellings` — the macro a file sees is a property of the component that compiled
   it, not of the arm that links it — in its 905 form, and the exact shape of `xnu_entry_905`'s own
   one-way-only check, which proved the *door* was linked but never asked whether the *clear* was.

## What this arm changes

### C1 — the write publishes a verdict (read-only; stores nothing, issues no command)

In `st_write_single_block` (`src/entry/entry_storage.c`), **after** the programming wait — so the bits
are the whole transfer's and not the PIO loop's intermediate sample — three cells:
`xnu_live_storage_wr_int_status_final`, `xnu_live_storage_wr_int_data_end`
(`SDHCI_INT_DATA_END`, the vendor's own completion, `sdhci.h:122`) and `xnu_live_storage_wr_data_err`
(`DATA_TIMEOUT|DATA_CRC|DATA_END_BIT`). The read body publishes the same two cells
(`_rd_int_status_end`, `_rd_int_data_err`); this is the write half. A `_wr_data_err != 0` on the press
is a write the card rejected, which every earlier arm would have logged as clean.

### C2 — the rw switch is a recorded arm key, REQUIRED by the write arm, checked against the link

- **`build_entry.sh`** resolves `STAGE90_XNU_HFS_ROOT_RW` from the same env name the kernel build reads,
  **refuses `HDD_WRITE=1` without it**, and adds it to `ENTRY_ARM_KEYS` (the record now carries **24**
  arm keys), the record reader and the record writer.
- **A linked-image clause, both directions.** After the link, `hfs_mountroot`'s body is disassembled and
  searched for `bl … <vfs_clearflags>`: **record says 1 but the body does not call it → refusal** (the
  arm promises a writable root the image cannot deliver); **record says 0 but the body DOES call it →
  refusal** (an image that mounts rw while its record says read-only is indistinguishable from one that
  writes the root). Read **by value**, out of the linked disassembly, per
  `mi4-linked-code-order-is-not-source-order` — not a grep of the source.
- **`preflight_boot_check.sh`** gains the key with the same artifact-grounded N/A rule the other three
  arm keys use, so a pre-905 park (whose record cannot carry the key) is still pressable.

**Nothing else moved.** The seam (`STAGE90_XNU_SEAM_LR = 0x8004e2dc`) did **not** move: the C1 cells are
three reads inside a function already linked (the entry-group file changed size by zero pages), and the
build's `xnu_entry_535` clause confirms it. The payload's switch set is unchanged, so `stage90.bin` is
byte-identical to 905b's.

## Verification

- **Built, linked, parked.** Entry bin `d6e2fbe6…`; both new clauses fire green
  (`xnu_entry_905: the rw-root clear is in the linked image - hfs_mountroot (at 0x802bd040) CALLS
  vfs_clearflags before hfs_mountfs`); `xnu_entry_678` reads **24** arm keys; 0 FAILs. `make check`
  exit 0 (including `check_hfs_staged`'s *"905 read-write root clear guarded"*).
- **Parked** at `out/stage90/frozen/armed-storage-d6e2fbe6/` (11 members), recorded in
  `records/revert-set.txt`; `tools/verify_revert_set.sh … --set=armed-storage-d6e2fbe6` — **11 file(s)
  matched**; the set-name rule passes.
- **Gate accepts the tree:** `verify_press_ready.sh` — the four tree checks **ok**; the only FAIL is
  `the press would be caught`, which fails because the device is **not connected** (the operator is away),
  not because of the tree.
- **Not pressed.** Nothing here was run against hardware.

## How to press (when back at the device)

`scripts/press_905c.sh` — wakes-check, rewrites the **clean** card image to `userdata`'s head
(`seek=0`, partition offset 0), then runs the project's gate + runner with
`--expect-arm=armed-storage-d6e2fbe6`. The medium must be clean because an aborted rw mount dirties the
HFS+ volume (`kHFSVolumeUnmountedBit` clears on the first rw mount), and the next boot would then fall
back to read-only (see `docs/experiments/experiment-905-write-through-the-emmc.md`).

**What the press must show.** `xnu_live_storage_wr_prog_*` (the wait ran — `_wr_prog_polls >= 1`,
`_wr_prog_timeout` 0 if the card released in time) **and** `xnu_live_storage_wr_int_data_end = 1` with
`xnu_live_storage_wr_data_err = 0` (the write was accepted, the cell this arm adds) **and** a
`B_WRITE` strategy call at LBA `0x400000+`, **and** after a power cycle `/newfile` reads back.
`_wr_data_err != 0` is the falsification that the write path is right — a write the card rejected — and
it is the reading no earlier arm could produce.

**GOAL: not yet established (write side).** The rung is built and parked, not proven.