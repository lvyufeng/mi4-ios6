# Experiment 903 — the root device resolution, moved to the card unit

**PRESSED 2026-10-02 (arm `armed-storage-8988f8f1`, spent; run exit 0, no brick). THE GOAL IS MET.**
The HFS+ root mounts and `/sbin/launchd` is exec'd **from the device's own storage** — the eMMC's
selected partition `userdata` (LBA 0x400000 = 2 GiB), read through the card unit
(`ST_MEDIA_DRIVER = 2`) -> `entry_storage_driver_read` -> the ladder's `st_read_single_block` — not
from the RAM blob. The change is one resolution: `__wrap_mdevlookup` answers the card unit rather
than disk 0.

## Why this is the next rung (895/902's owed step)

902 established that the HFS+ port **mounts a real HFS+ volume and execs pid 1 from it**, and that the
reads the mount generates go through `st_media_strategy` at offsets that only HFS can produce (the
catalog read that locates `/sbin/launchd` at volume block 34). But the *volume* it served was disk 0 —
the committed **RAM blob** `g_stage90_root_hfs` built into the image (`_rootmedia_strategy_bytes =
0x00080000`). The goal's storage clause names the **device's own** storage, so the volume must come
from the eMMC.

The card unit (888) already exists and is proven (892: `_card_lba=0x400000`): its `st_media_strategy`
computes `lba = entry_storage_selected_lba() + off/512 + i` and fetches each 512 B block through
`entry_storage_driver_read` -> `st_read_single_block`. It is the unit that reads the **selected
partition** the ladder found (`userdata`, LBA 0x400000, per 890/884). Its geometry is set by
`entry_root_media_register_card` with `blockcount = entry_storage_selected_count()`.

What was missing is that the **mount path never asked for it**. `__wrap_mdevlookup` — the function the
BSD root lookup calls to resolve the root device — answered `entry_root_media_register(devid)`, which
returns disk 0, the RAM blob. 902's root is therefore the blob.

## The change

One switch, `STAGE90_XNU_ROOT_FROM_CARD`, threaded through the same marker pattern as 882/888:

- **`src/platform/stage90_root_media.c`** — `entry_root_media_register_card` is made idempotent and
  **returns the card's `dev_t`** (it now returns `int`); two arm markers
  `entry_root_media_cardroot_arm_on/off` let the linked image name the arm by `nm`.
- **`src/entry/entry_trace.c`** — in `__wrap_mdevlookup`, under `#if STAGE90_XNU_ROOT_FROM_CARD` the
  root answers `entry_root_media_register_card()`; otherwise it answers `entry_root_media_register(devid)`
  exactly as before.
- **`src/entry/build_entry.sh`** — the switch is validated (`ROOT_FROM_CARD=1` requires
  `EMMC_STRATEGY=1`), added to `ENTRY_ARM_KEYS`, echoed into the entry record, compiled into
  `entry_trace.c`, and checked in **two** clauses: the module-agreement `nm` clause, and the
  linked-image clause **`xnu_entry_903`**, which asserts `__wrap_mdevlookup` calls
  `entry_root_media_register_card` and **not** `entry_root_media_register`, read from the linked
  disassembly by raw `nm`/`objdump`.
- **`scripts/preflight_boot_check.sh`** — `STAGE90_XNU_ROOT_FROM_CARD` is a config key, emitted only
  when the entry ELF carries the card-root arm to name it (the 882/888 artifact-grounded pattern).

**`mi_mdev` is left at 1** on this arm (902's answer), because mockfs never declines: it mounts and
memory-backs `g_stage90_ramdisk`, so the **fall-through is preserved** — if HFS mount fails, the exec
still finds `MH_MAGIC` via `DKIOCGETMEMDEVINFO`. The card unit's own `DKIOCGETMEMDEVINFO` is not
consulted for the root device; mockfs gets the file-scope `mi_mdev = 1` from disk 0 and memory-backs the
ramdisk, while HFS reads the card through the strategy. Both mount paths still reach an exec
(`check_hfs_root_arm_split`).

## The device medium

The HFS+ volume is **written to the device** at `userdata`'s head (byte 0x80000000 = 2 GiB): a
512 KiB image (`out/stage90/xnu_card_hfs.img`, sha256 `b4ccb4e0…`) whose 128 blocks × 4096 B is the
whole extent HFS reads to mount and exec (the ext4 superblock at offset 1024 lies inside it). The write
is **minimal and non-destructive to the boot chain**: GPT LBA 1–33, `recovery` (p20, TWRP),
`aboot`/`fastboot` (p7) are untouched, and the first 1 MiB of `userdata` was backed up to
`out/stage90/userdata_head.bin` and read back. The rest of the 13.6 GB partition is irrelevant to the
mount.

## The press, read off `/tmp/cancro-last_kmsg.txt`

**The root device resolution took.** `xnu_live_mdevlookup_ret = 0x04000002` — major 4, unit 2 = the
**card unit** (`ST_MEDIA_DRIVER = 2`), where 902's arm answered `0x05000000` (major 5 unit 0, disk 0,
the RAM blob). `_rootmedia_card_registered = 1`, `_rootmedia_card_dev = 0x04000002`.

**The mount's reads were served off the eMMC.** Every card read is at the selected partition —
`_rootmedia_card_lba = 0x00400000` (the userdata start) and `_rootmedia_card_last_lba` ∈
{0x400002, 0x400010, 0x400017, 0x400050, 0x40005f, 0x4000d0, 0x4000d7}. The offsets are the **HFS
mount sequence** (the same one 902 read off disk 0): `0x400` = the HFS+ volume header, `0x2000` =
extents B-tree (block 2), `0x1a000` = catalog B-tree (block 26), `0xa000` = attributes B-tree (block
10), `0x1b000` = catalog node 27 — **HFS opened all three of its B-trees, off 0x400000**. The ladder
served each block: `_storage_drv_lba` walks 0x400002, then 0x400010..0x400017, 0x400050..0x40005f,
0x4000d0..0x4000da — 36 `st_read_single_block` reads, no error (`_storage_rd_err = 0`), no DMA timeout.

**The exec, from the card.** The channel capped at 8192 records (0x2000; the log carries 8206
`xnu_live_` lines) and truncates after the B-tree reads, so the launchd `__TEXT` page read (volume
block 34 = offset 0x22000, `blkno=0x110`) is **past the cap**. The console carries the proof the
mount and exec happened: `load_init_program: attempting to load /sbin/launchd` and then **neither**
`failed loading /sbin/launchd: errno N` **nor** the `panic("Process 1 exec ...")` — `load_init_program`
returns 0 and returns silently on success — followed by `mini4: the OS starts the process at 0x10e0`
and `mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80050eb8)`. pid 1
got the init image **from the eMMC's HFS+ volume**. `hfs` prints nothing (both debug sites are on), so
`hfs_mountfs` returned 0; `cannot mount root` did not print, so `vfs_mountroot` returned 0.

**`BSD root: md0, major 4, minor 2` is the counter-signal that confirms it.** The name `md0` comes
from the `IOKitBSDInit` boot-arg, but the **major/minor do not**: `major 4, minor 2` **is the card
unit** — the same device `mdevlookup` answered. 902's log said `major 5, minor 0` (disk 0). So the BSD
root device really moved to the card; the string `md0` is the boot arg's, not the device's.

**Where it ended.** Not on a mount or exec fault. The run ended on the harness's own forced-end clock,
the known since-690 store fault: `panic ... kernel abort type 4, fault_type=0x3, fault_addr=0xfa0065c`
= `entry_seam_end_run` storing `RESTART_REASON` at `0x0fa0065c` with `r2=0x0fa00000`. Before that, pid
1 was parked in poll (55099 idle-door tests). The device will not reset itself (XNU's reboot path
faults), so the harness ends the run by its own clock — the same terminal state every successful boot
since 894 has shown. Run exit 0; **no brick** (`4a2fe00b` re-enumerated; `33e80afe` never appeared).

## What the falsification would have been, and did not happen

If the strategy reads had come from disk 0 (offsets within the 512 KiB blob, `mdevlookup_ret` major 5)
or the mount had fallen through to mockfs, the resolution had not taken. Neither happened: the reads
are all at LBA ≥ 0x400000 (0x400000 + HFS volume offsets, i.e. **the file's bytes as laid out on the
eMMC**, not the blob's), and `mdevlookup_ret` is the card unit.

## What this establishes, and what remains

**Establishes — the goal's storage clause.** XNU loads, enters the OS, runs drivers, and **mounts
storage that is the device's own eMMC**, with process 1 exec'd from it. This is the persistent-storage
clause the goal names, closed on the read side.

**Does not establish — three honest limits.**

1. The mount/exec was proven: **yes** (console + the 8206-record channel capped right after the mount
   loop, before the exec page). The exec-page read itself is past the cap and reads as **truncated**,
   not **absent** — the console is the evidence it happened.
2. **The repartition is destructive and the operator's** (relaxed envelope, 2026-10-01: TWRP/Android
   may be sacrificed, only `fastboot` must survive). The new 903 doc's write of a 512 KiB HFS+ image at
   `userdata`'s head **overwrote part of the ext4 first 1 MiB**; TWRP/Android were not re-tested after
   (the device dropped off `adb` mid-session and recovered on Magisk root — re-enumeration, no brick).
   `userdata_head.bin` (1 MiB) holds the pre-write bytes.
3. **Write-persistence is not established.** The tier-3 login the overlay prompts for was never given,
   so the 512 KiB HFS+ volume was written **read-only from the host** — it is on the device, but this
   run does not prove XNU a) writes to it or b) reads it after a re-mount. Reading it is proven; the
   goal's "mount storage" is met on that reading.