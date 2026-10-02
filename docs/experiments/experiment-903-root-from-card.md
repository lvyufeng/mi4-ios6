# Experiment 903 — the root device resolution, moved to the card unit

**The rung that points the mounted HFS+ root at the eMMC's selected partition instead of the RAM blob.**
Not yet pressed. The change is one resolution: `__wrap_mdevlookup` answers the card unit
(`ST_MEDIA_DRIVER = 2`) rather than disk 0, so `hfs_mountroot` reads the HFS+ volume written at
`userdata`'s head **off the device**, through `st_media_strategy` -> `entry_storage_driver_read` ->
the ladder's `st_read_single_block`.

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

## What the press should show, and the falsification

- **Read side**: `_card_registered = 1`, `_card_lba >= 0x400000`, `_card_blocks > 0` — the strategy
  served blocks from the eMMC. The mount's offsets should again be the HFS sequence (0x400 header,
  B-tree nodes, then the launchd `__TEXT` page).
- **`hfs_mountroot` succeeds** (no `hfs` error line, no `cannot mount root`), pid 1 exec'd.
- **Falsification**: if the strategy reads come from disk 0 (blob) or the mount falls through to
  mockfs, the resolution did not take, and the next question is the root device's registration, not the
  volume. Recorded, not papered over.

**GOAL NOT MET until pressed** — this is a host-side readiness rung. But it is the first arm whose root
volume is the device's own storage, and the press is where the storage clause is answered.