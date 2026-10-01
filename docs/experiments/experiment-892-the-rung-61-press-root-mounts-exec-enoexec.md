# 892 — the rung-61 press: the root mounts and `load_init_program` runs; the exec returns ENOEXEC

**A PRESS, made autonomously under the standing instruction 「以后不要再问我 直接自动继续，直到os能正常启动」.**
**ONE gate exit 0; ONE runner exit 0**; `fastboot boot` only; nothing flashed; `33e80afe` absent from
both device lists before the gate. The phone returned. Arm `armed-storage-5c440167` (rung 61, the card
unit) is **SPENT**. Capture `out/stage90/captures/rung61-card-20261001-165245-last_kmsg.txt`
**671,086 B `af03ba0b…`**.

## 1. The card unit works — the strategy reads the ladder at `userdata`

| cell | value | reading |
| --- | --- | --- |
| cell (full key `xnu_live_rootmedia_…`) | value | reading |
| --- | --- | --- |
| `card_registered` | `1` | the THIRD disk (`ST_MEDIA_DRIVER`) was registered at its own dev_t |
| `card_lba` | `0x00400000` | the LBA the strategy asked the ladder for = **`userdata`'s base** (890) |
| `card_blocks` | `0x01959fde` | the whole extent — 26,566,622 blocks |
| `card_bytes` | `0x2b3fbc00` | 725,712,896 bytes served |
| `card_dev` | `0x04000002` | major 4, unit 2 — the third disk's own `dev_t` |
| `card_refused` | `0` | not one read refused |

`st_media_strategy` computed `entry_storage_selected_lba() + blkno` and called
`entry_storage_driver_read(lba)` per block. **The join 887 named as per-unit is closed in the unit it
named, on hardware.** And the selection it reads from is 890's: `userdata`, carrying ext4.

## 2. And the boot reaches `load_init_program` — a new frontier

```
Added memory device md0/rmd0 (02000000/0D000000) at 0000000080521000 for 0000000000002000
BSD root: md0, major 5, minor 0
VM_TEST_DEVICE_PAGER_TRANSPOSE: PASS
load_init_program: attempting to load /usr/local/sbin/launchd.development
load_init_program: failed loading /usr/local/sbin/launchd.development: errno 2
load_init_program: attempting to load /sbin/launchd
load_init_program: failed loading /sbin/launchd: errno 8
```

**The root mount succeeded and XNU's own `load_init_program` ran** — it looked for
`/usr/local/sbin/launchd.development` (ENOENT, 2 — absent) and then `/sbin/launchd` (**ENOEXEC, 8** —
present but not a valid executable). This is further than any prior run: the rung-58 press (890) reached
userland via the mockfs fixture, but this boot reaches the point where the OS tries to exec a real
`launchd` off its root.

The userland fixture phase reads **0 calls** on this boot (`open`/`read`/`getpid`/`exit` all absent),
because the machine stops here — but the goal's own floor (504/520/533's syscalls) is not a regression;
this boot simply went past it to the exec, and the exec failed.

## 3. Safety

- **No panic at all**: the log ends `No errors detected`. `xnu_entry_failures=0`,
  `xnu_entry_abort_entries=0` — **no brick**.
- `fastboot boot` only; nothing flashed; no byte of the medium written (the card unit is read-only
  CMD17 reads).
- Seam unmoved; the payload record byte-identical to rung 23 (`6c2b6038`).

## 4. Which medium serves the root — and why the exec fails

The card unit and the *root* are **two different disks**, and the log says which is which:

```
rootmedia_major=0x05  rootmedia_dev=0x05000000  rootmedia_blocks=0x00000010   # md0: 16 blocks = 512 KiB
rootmedia_hfs=0x00000001
rootmedia_strategy_medium=0x00000000
rootmedia_strategy_sector=0x00400002  rootmedia_stage_blocks=0x00000001
```

- **The card is major 4, unit 2** (`card_dev=0x04000002`) — it registered and read `userdata`'s
  extent (`card_lba=0x400000`, `card_blocks=0x1959fde`, `card_refused=0`), but it is **not** the
  mounted root.
- **The mounted root is `md0`, major 5** (`rootmedia_dev=0x05000000`) with **`rootmedia_blocks=0x10`
  = 16 blocks = 512 KiB** — the size of the **in-payload HFS+ volume** (`build_hfs_root_image.sh`;
  889 §4). `rootmedia_hfs=1` confirms the HFS+ port ran.

So the boot **did not mount `userdata`/ext4 and did not mount the card** — it mounted the small
in-payload HFS+ medium. The card read is a *side effect* of this arm (the door opened, the strategy
read the ladder once), not the source of the root.

**The frontier is therefore inside the 512 KiB HFS+ medium**: `load_init_program` reads
`/sbin/launchd` off it and gets a file whose bytes are not a valid Mach-O for the kernel
(**ENOEXEC, 8**). The medium is exactly the fixture that 889 §4 says carries "`/sbin/launchd` = the
same Mach-O `entry_ramdisk.s` carries" — so the next question is host-side and cheap: **does the
512 KiB HFS+ volume's `/sbin/launchd` hold the intended Mach-O bytes, and at the offset the HFS+
reader returns?** Either the volume was built with the wrong (or truncated) Mach-O, or the HFS+
reader's file-data path returns the wrong blocks. That is a **build/code** question — no device
needed for the first pass.

**THE GOAL IS NOT MET** — the OS executes no `launchd`; but for the first time the boot reaches the exec
of a real root-filesystem init, and the card unit reads the device.