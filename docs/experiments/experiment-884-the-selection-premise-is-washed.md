# 884 — THE SELECTION PREMISE IS WASHED: THE DEVICE'S OWN GPT, AND WHAT RUNG 57 WILL PICK

**A read-only device measurement: no boot, no press, no write, no `fastboot`, no arm spent.** `adb` is
up (Android, per 860) and the read is `dd` from `/dev/block/mmcblk0` — the same read-only device class
880 §3 used. The rung-58 arm `armed-storage-5936b246` stays parked and unpressed. **THE PRESS IS THE
OPERATOR'S; THE GOAL IS NOT MET.**

In one paragraph: 867 §3.1 **blocked** building the eMMC-driver rung because its correctness is the
storage ladder's *selection*, and rungs 57–60 are parked and unpressed — "a fifth unwashed premise on
four." That premise is a fact about **the device's real GPT**, which is readable host-side with `adb`
and no press. This step reads it and runs rung 57's own selector over it. **The premise is now washed:**
the selector keeps entry 24 = **`userdata`** (first LBA **4194304**, extent 26,583,007 sectors), exactly
as 863 claimed from prose; rung 56's two pressed cells are **device-verified** by the same table (entry 0
= `sbl1`, `first_lba = 34 = 0x22`, `last_lba = 4095 = 0xfff`); and the selected partition's superblock —
the sector rung 57's arm would land and **no press has yet read** — is **ext4** (`s_magic = 0xEF53`,
4096-byte blocks). So 880's constraint is confirmed by measurement, not inference: **the selected medium
is ext4; the filesystem this repository ports is HFS+; they do not match.**

## 1. What was read, and why it is safe

```
dd if=/dev/block/mmcblk0 of=/data/local/tmp/gpt.bin bs=512 count=34   # LBA 0..33
adb pull /data/local/tmp/gpt.bin
```

LBA 0 is the protective MBR, LBA 1 the GPT header, LBA 2..8 the 28-entry array (7 sectors). The read is
**read-only on the medium**, opens no controller window (the SoC block layer serves it), and does not
touch `fastboot` reachability — it is strictly less than 880 §3's `blkid`. The header decoded:

```
EFI PART   MyLBA=1   AlternateLBA=30777343   usable 34..30777310
PartitionEntryLBA=2   NumberOfPartitionEntries=28   SizeOfPartitionEntry=128
```

`28 * 128 / 512 = 7` array sectors — **exactly** the `st_gpt_array_nsec` rung 57 computes
(`entry_storage.c`), which is the first independent check that the header the ladder parses and the
header on the disk are the same shape.

## 2. Rung 57's selector, run over the device's own table

The selector keeps the entry with the largest `EndingLBA - StartingLBA` (`entry_storage.c:8012`). Over
the measured table that is **entry 24**:

| idx | first_lba | last_lba | extent (sectors) | GPT name | type GUID |
| --: | --------: | -------: | ---------------: | --- | --- |
| 0 | 34 | 4095 | 4062 | `sbl1` | DEA0BA2C-CBDD-4805-B4F9-F428251C3E98 |
| 18 | 393216 | 458751 | 65536 | `boot` | 20117F86-E985-4357-B9EE-374BC1D8487D |
| 22 | 786432 | 3407871 | 2621440 | `system` | EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 |
| **24** | **4194304** | **30777310** | **26583007** | **`userdata`** | **EBD0A0A2-B9E5-4433-87C0-68B6B72699C7** |

(The full 28-row table is in `docs/reference/local-device-findings.md`, "On-disk GPT geometry".)

**What this washes.** 867 §3.1's block rests on the selection being unwashed. Three of the four parked
rungs' premise cells are now device-measured rather than pending a press:

- **rung 57's selection** — the selector picks `userdata`, `st_gpt_data_sector = 4194304`. **863's claim,
  device-verified.**
- **rung 56's decode (already pressed)** — entry 0 = `sbl1`, `first_lba = 34` (`0x22`), `last_lba = 4095`
  (`0xfff`), matching `_ge_first_lba = 0x22` / `_ge_last_lba = 0xfff` / `_ge_index = 0` **byte for byte**.
  This is the first time those cells are corroborated by an independent host read.
- **`st_gpt_array_nsec = 7`** — the header's own arithmetic, matching the disk.

## 3. The sector no press has landed, and what it holds

Rung 57's arm ends by reading `st_gpt_data_sector + ST_FS_SB_SECTOR_OFF` = `4194304 + 2` = **LBA
4194306** — the selected partition's superblock sector. Reading it:

```
LBA 4194304: w0=0x00000000  w1=0x00000000      (partition boot block, zeroed)
LBA 4194306: w0=0x000cc000  w1=0x0032b3fb      (superblock sector)
  at partition byte 1024:  s_magic = 0xEF53  (ext2/3/4),  s_log_block_size = 2 (4096 B)
                           s_inodes_count = 835584,  s_blocks_count = 3322875,  s_rev_level = 1
```

**The selected partition carries ext4.** Rung 56 read partition 1's superblock sector and found ARM code
(`_pt_w0 = 0xe1a04614`); this is the sector rung 57's arm would read, and it holds a real superblock —
the reading rung 57's press is for, now known in advance. And **no partition in the table carries an HFS
type GUID**: the only filesystems present are ext4 (`EBD0A0A2…` = the Linux-filesystem GUID, on
`persist`/`modem`/`system`/`cache`/`userdata`) and the Android-specific GUIDs.

## 4. What this changes, and what it does not

**It lifts 867 §3.1's block for the LBA half.** The driver rung's correctness premise — *which LBA does
the mount path ask for* — is now a measured device fact, not a pending press. The driver rung (§3 of
867) can be built without stacking an unwashed premise on rungs 57–60, because 57's selection is known
and the downstream rungs (58/59/60) are downstream of a **confirmed** base.

**It does not make the mount reachable, and it confirms why.** Even a driver rung that issues a real
CMD17 per block would serve **ext4** bytes to a root walk whose only ported filesystem row is **HFS+**
(868–879). §2's `hfs_mountroot` would not recognize them and the walk falls through to mockfs — the same
place it is now. **This is 880's constraint, now a measurement rather than an inference.**

**AND IT SETTLES THE ORDER OF THE DESTRUCTIVE STEP — do it LAST.** The relaxed envelope (2026-10-01)
permits repartitioning, so the route to a real mount is to make the selected medium **HFS+** (§3 of 880,
route 1). But **reformatting `userdata` now would destroy ~13 GB for nothing**: the HFS+ root row is
**inert when the port is off** (879), the port is not linked into the kernel image, and the driver rung
is not built — so no arm in this repository could mount the HFS+ volume it created. **The driver rung and
the linked port must be built and pressed first; the reformat is the step that comes after them, and it
is the operator's.**

## 5. The honest bound

- This is a **read of the medium through the SoC's block layer** (`adb`), **not** through the ladder's
  SDHCI path. It measures what is *there*, not that the ladder's driver reads it correctly. The ladder's
  own read of these sectors is still a press (rung 57's arm).
- The GPT index is **not** the kernel partition number: GPT index `k` = `mmcblk0p(k+1)`, so `userdata`
  is index 24 = `p25`. `st_gpt_data_sector` is a **sector address**, not a partition index.
- No byte of the medium moved; no arm was spent; nothing was armed; no `fastboot`. **THE GOAL IS NOT
  MET** — the medium's filesystem (ext4) and the port's (HFS+) still do not match, and no XNU driver
  reads the card yet.

## 6. Safety

`adb` read-only via `dd`; no boot, no press, no flash, no write to the device or its partitions; nothing
sent over `fastboot`; `33e80afe` irrelevant (no fastboot). The rung-58 arm remains parked and unpressed.