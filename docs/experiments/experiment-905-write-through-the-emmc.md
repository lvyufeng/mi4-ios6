# Experiment 905 — writing through the eMMC: the CMD24 write path, an rw root, and the programming wait

**PRESSED 2026-10-02 (arm `armed-storage-9d2dd2e4`, spent). Rung B of the 「坐实 + 写通存储」 pair.**
The first rung that can hand the device's own storage a byte, mount the HFS+ root read-write, and have
the fixture create a file.

**The press did NOT return.** The device hung and needs a power press; the RAM-console log dies with
the power, so **this arm's own readings are not in hand**. What follows separates what is *established*
from what is *unread*, because the two are not the same and only one of them is a result.

## What rung B adds

Three code changes plus a scripted device write.

### B1 — a CMD24 write path in the ladder and the strategy

- **`src/entry/entry_storage.c`** — `st_write_single_block` is `st_read_single_block` with the
  direction reversed in exactly three places: the command is **CMD24** (`ST_CMD_OP_WRITE_BLOCK` 24) at
  the same LBA argument; `TRANSFER_MODE` is `ST_SDHCI_TRNS_WRITE_1BLK` (`0x0002`, the read bit cleared)
  where the read sends `TRNS_READ_1BLK` (`0x0012`); and the PIO loop waits on `SPACE_AVAILABLE`
  (PRESENT_STATE bit 10) writing `SDHCI_BUFFER` where the read waits on `DATA_AVAILABLE` (bit 11)
  reading it. `entry_storage_driver_write(uint32_t lba, const uint32_t *w)` is the door; the caller's
  block is copied into `st_write_block` before the command so the FIFO fills from a kernel `.bss`
  array. The two-direction split is a `_Static_assert` pair (`TRNS_WRITE_1BLK == TRNS_BLK_CNT_EN`, and
  `SPACE_AVAILABLE != DATA_AVAILABLE`), not a comment.
- **`src/platform/stage90_root_media.c`** — the card unit's `st_media_strategy` gains a `B_WRITE`
  branch that maps the buf, loops per 512-byte block calling `entry_storage_driver_write`, sets resid
  and calls `buf_biodone`. The `EROFS` refusal narrows to `unit != ST_MEDIA_DRIVER`, so the card unit
  accepts writes and every other unit keeps refusing — which is what makes `DKIOCISWRITABLE = 1` honest
  instead of a claim.

### B2 — the HFS+ root mounts read-write

`tools/hfs_patch_root_rw.py` inserts, under `STAGE90_HFS_ROOT_RW`, one guarded
`vfs_clearflags(mp, (u_int64_t)MNT_RDONLY);` immediately before the `hfs_mountfs(rvp, mp, NULL, 0,
context)` call in `hfs_mountroot` — the one place between `vfs_rootmountalloc_internal`'s hard-coded
`mp->mnt_flag = MNT_RDONLY | MNT_ROOTFS` (`vfs_subr.c:989`) and `hfs_mountfs` latching `HFS_READ_ONLY`
from `vfs_isrdonly(mp)` (`hfs_vfsops.c:1312`). `vfs_subr.c` is **not** touched — clearing the flag
there would move every mount in the kernel. `tools/stage_hfs.sh` applies the patch; `check_hfs_staged.sh`
re-derives its effect (`make check` prints *"905 read-write root clear guarded"*).

### B3 — the fixture creates a file

`src/entry/entry_ramdisk.s`'s pid-1 program gains `open("/newfile", O_CREAT|O_WRONLY, 0644)` →
`write(fd, buf, 4)` → the existing fork/exit/wait tail. The card volume
`src/entry/blob/xnu_arm_entry_root_hfs.img` (sha `b321db0d…`) carries this fixture and is written at
`userdata`'s head.

## The defect the first press found — and the fix this arm carries

The first 905 arm (`armed-storage-bb2269bf`) pressed earlier and — unusually for this project — **its
log survived** (run exit 0, no brick). Reading it against rung 904's is decisive:

**Of the boot's 62 reads, 61 are byte-identical to 904's. The one that differs is the first command
after the write.** `xnu_live_storage_rd_lba = 0x00400110` (the launchd `__TEXT` page, volume block
0x110) returned:

| cell | 904 | 905 (bb2269bf) |
|---|---|---|
| `rd_complete` | 1 | **0** |
| `rd_err` | 0 | **0x00010000** (SDHCI CMD TIMEOUT) |
| `rd_status_after` | 0x1 | 0x18000 |
| `rd_w0` | `0xfeedface` | `0xff3f00b0` |
| `rd_w1..w3`, `w127` | the Mach-O | `0xffffffff` |

**The medium is intact** — `adb dd` reads `feedface` at byte `0x22000` on `/dev/block/mmcblk0p25`. So
this is not a corrupted block: **the card was still programming the block the write had just sent,
held DAT0 busy, and refused the next command.** Reads LBA+1..+7, issued after programming finished,
succeeded — the number that names the cause. The write body handed the card the last FIFO word and
**returned without waiting for the transfer to complete**, so the next CMD17 (the launchd page — the
very exec path that succeeded in 903/904) timed out, and `load_init_program` failed with ENOEXEC.

**And the fixture's own behaviour is explained by the same failure.** In `bb2269bf`'s log only **one**
write is served (`xnu_live_rootmedia_card_wr_first_lba = 0x00400002` — the HFS+ volume header), not the
catalogue/extent writes a file creation needs, and `/newfile` is absent. The reason is not that the
write path could not be reached: an HFS+ volume carries `kHFSVolumeUnmountedBit` while clean and clears
it on the first rw mount. Once `hfs_mountroot` took the rw path (`vfs_clearflags` ran) and then **died
on the timed-out read**, the volume was left **dirty on the medium**, and the next boot's `hfs_mountfs`
— seeing the dirty bit on a non-journaled volume — fell through to read-only. So the write path's first
failure cascaded into "the rw mount never afterwards succeeds".

### The fix

A bounded **programming wait** after the PIO loop, mirroring the controller's own completion
(`SDHCI_INT_DATA_END`):

```c
prog_polls = 0u;
t0_prog = (uint32_t)stage90_cntvct_read();
for (;;) {
    prog_ps = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
    prog_polls++;
    if ((prog_ps & (ST_SDHCI_DOING_WRITE | ST_SDHCI_DAT_LINE_ACTIVE)) == 0u)
        break;
    if ((uint32_t)stage90_cntvct_read() - t0_prog >= ST_EXT_DATA_TICK_BUDGET)
        break;
}
```

The wait is on **`DOING_WRITE` (PRESENT_STATE bit 8) AND `DAT_LINE_ACTIVE` (bit 2)** — not
`DAT_LINE_ACTIVE` alone. `DOING_WRITE` closes a race: the line can read momentarily clear in the instant
after the last FIFO word while the card has not yet asserted its program-busy. The first press's own
`st_data_reset` is the evidence that both bits were stuck (`_dr_ps_before = 0x01e80106`, bit 8 and bit 2
set 46 s after the write). The bound is the vendor's data bound (1.25 s, `ST_EXT_DATA_TICK_BUDGET`), so
a card that never releases cannot hang the boot; the wait is published (`_wr_prog_polls`, `_wr_prog_ticks`,
`_wr_prog_ps_end`, `_wr_prog_timeout`) so a press reads whether it was needed and where it ended.

**The eighth move of `STAGE90_XNU_SEAM_LR` → `0x8004e2dc`.** The poll is a loop in `entry_storage.c`, a
file of the entry group, so it crossed a page and the build refused (`the exit's call to FlushPoU_Dcache
is at 2147803864 and returns to 2147803868, while ... STAGE90_XNU_SEAM_LR is 0x8004d2dc`). Both copies
(`entry_trace.c` and `scripts/run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`) were re-derived.

## Verification

- **Built and linked.** Entry bin `9d2dd2e4…` (6,498,612 B), payload `b7bb6d08…`. `xnu_entry_905`
  passes (*"the write door is in the linked image - `entry_storage_driver_write` (T, 0x80012750) is
  called from `st_media_strategy` (at 0x80288d80)"*), `xnu_entry_535` confirms the seam at `0x8004e2dc`,
  **0 FAILs**, `make check` exit 0. `verify_press_ready.sh` 5/5.
- **The medium.** The device `userdata` head was written with the 905 card image (`b321db0d…`, carrying
  the `/newfile` fixture) at partition offset 0 and read-back-verified. A backup of the 903 media head
  (`b4ccb4e0…`) is at `out/stage90/userdata_head_905pre.bin`.
- **The press.** Bytes `b7bb6d08` sent, unchanged across the send, under `--expect-arm`. **Run exit 2:
  the device did not return within 180 s** and is still dark on `adb` and `fastboot` (the last host-log
  event for its port is the `fastboot boot` disconnect). **This arm's outcome is unread** — the
  programming wait, the rw mount, and the fixture's `/newfile` are built and linked but not observed.

## What is established, and what is not

- **Established (from `bb2269bf`'s surviving log):** the write path issues a real CMD24 and the card
  accepts it (`wr_complete=1`, `wr_err=0`, all 128 words gated); a write with no completion wait
  corrupts the next command (the launchd read `rd_err=0x00010000`), which is the exec regression; the
  medium is not corrupted; and an aborted rw mount dirties the volume so the next rw attempt falls back.
- **Not read:** whether this arm's programming wait fixes it, whether the rw mount now holds, and
  whether `/newfile` is created and persists. Those need the device back and a re-press.

## Next

1. **Power-press the device** (the operator's; hold Power ~10–15 s). A warm return lets the watcher
   (`/tmp/watch_905b.sh`) capture `/proc/last_kmsg`; a cold press loses it.
2. Because the medium is now **dirty** (see the cascade above), a re-press off it will fall back to
   read-only. **Re-write the clean card image to `userdata`'s head** before re-pressing, so the rw
   attempt starts from `kHFSVolumeUnmountedBit` set.
3. If the write still leaves the medium dirty across the write, add an explicit `fsync`/unmount on the
   XNU side, or verify the HFS+ dirty-bit handling on the ported `hfs_mountfs`.