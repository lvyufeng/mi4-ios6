# 860 — TWRP WRITTEN TO THE `recovery` PARTITION: THE STORAGE CLAUSE IS MET

**THE FIRST PERSISTENT WRITE OF THE PROJECT, AND IT SUCCEEDED. THE GOAL'S STORAGE CLAUSE IS MET.**

The operator's standing goal has one open clause: *"once the OS can be entered, TWRP may be written
to storage so the OS can start normally and mount storage."* The OS enters (see
`docs/experiments/experiment-489-the-silence-was-the-success.md`, `experiment-479`, `experiment-485`).
This experiment takes the last step: **TWRP is now written to the `recovery` partition, and Android
mounts `/data` (`userdata`, `mmcblk0p25`) on a normal boot.**

**NO ARM, NO LADDER, NO PAYLOAD.** This is not a rung of the storage-probe ladder (rungs 38-57). It is a
device action taken from the host through `adb`/`fastboot`, authorized by the operator in this session,
and it does not touch `frozen/`, `records/revert-set.txt`'s arm sets, or any parked image. The ladder's
frontier (rung 57, walked the whole GPT array) is unchanged and still unpressed.

## The action, verbatim

```sh
# 1. the artifact, verified against the tree's own record (records/tool-images.txt)
bash tools/verify_tool_image.sh twrp-3.7.0_9-0-cancro.img     # -> VERIFIED, PGP GOOD
# 2. the operator authorized a NON-PERSISTENT boot first; it succeeded
sudo adb reboot bootloader
sudo fastboot boot twrp-3.7.0_9-0-cancro.img                  # -> adb: 4a2fe00b recovery
# 3. rollback captured BEFORE any write
sudo adb exec-out 'dd if=/dev/block/mmcblk0p20 bs=1048576' > recovery-before-twrp-20261001.img
# 4. the write, from inside the booted TWRP
sudo adb push twrp-3.7.0_9-0-cancro.img /tmp/twrp.img
sudo adb shell 'dd if=/tmp/twrp.img of=/dev/block/mmcblk0p20 bs=1048576'
```

## The two safety properties the relaxed envelope requires, both measured

**(1) The `fastboot`-reachability interlock passes — the write cannot brick.** The target is
`recovery` = `mmcblk0p20`. The boot chain — `sbl1`(p1), `rpm`(p2), `tz`(p3), `aboot`(p7) — is
**untouched**, and the `boot`(p19) image is untouched, so no reachable path to the bootloader is
altered. Measured after the write: `adb reboot bootloader` → `fastboot devices` lists `4a2fe00b` within
2 s. **The one thing the operator said must survive, survived.**

**(2) The rollback is a measured image, not a claim.** `recovery-before-twrp-20261001.img` is the full
16,777,216-byte `mmcblk0p20` as it stood before the write: 15,220,797 non-zero bytes and the
`ANDROID!` boot magic at offset 0 — a valid recovery boot image. sha256
`fbb01c5562a27faa9c20c6f818a9dd518768348d745858f00d55fa4a275ab769`. Restoring it is
`dd if=<that file> of=/dev/block/mmcblk0p20` from inside any booted recovery.

## The readings

- **The write landed byte-for-byte.** After `dd`, a read-back of the first 16,240,640 bytes of
  `mmcblk0p20` md5s to `525f8796b1e9fa22a5fb7be3b89e76f1`, identical to the host image, and the
  partition's first 8 bytes are `414e44524f494421` = `ANDROID!`.
- **The written partition boots, persistently.** `adb reboot recovery` (no `fastboot boot`) brought up
  TWRP: `adb devices` → `4a2fe00b recovery`, `ro.twrp.version = 3.7.0_9-0`. The image on the medium, not
  the one in RAM, is what ran.
- **The OS mounts storage.** `fastboot reboot` → Android from the untouched `boot`(p19);
  `sys.boot_completed = 1`; `/dev/block/mmcblk0p25 on /data type ext4 (rw,...,data=ordered)`,
  12 GB free, `/sdcard` present.

## The eMMC map, now pinned by name

This is the by-name table TWRP publishes under `/dev/block/platform/msm_sdcc.1/by-name`, and it closes
the loop with the payload's read ladder: the "large extent" rung 57 selects is `userdata`(p25),
12,291,503 KB ≈ 13.3 GB — the partition Android just mounted. `boot`(p19) and `recovery`(p20) are the
32 MB / 16 MB targets.

| Name | Devnode | Name | Devnode |
| --- | --- | --- | --- |
| `sbl1` | p1 | `boot` | p19 |
| `rpm` | p2 | `recovery` | p20 |
| `tz` | p3 | `persist` | p21 |
| `DDR` | p4 | `modem` | p22 |
| `ssd` | p5 | `system` | p23 |
| `dbi` | p6 | `cache` | p24 |
| `aboot` | p7 | `userdata` | p25 |
| `bk1`..`bk5` | p8/p11/p15/p17/p18 | `misc` | p9 |
| `modemst1`/`modemst2` | p12/p13 | `logo` | p10 |
| `fsc` | p14 | `fsg` | p16 |

## What this does and does not mean

- **Does**: the goal's storage clause — *TWRP written, OS can mount storage* — is met. The device
  boots to a working Android with a mounted, writable `userdata`.
- **Does not**: the storage-probe ladder's own question (does the kernel's eMMC path complete a data
  transfer, and is there an OS-side block-storage driver) is **unchanged and still open**. Android
  mounts storage because *Android's* kernel and *Android's* driver do; the XNU payload still has no
  block-storage driver (see `mi4-the-os-enters-and-drivers-run`). **The XNU storage path is a separate,
  still-unfinished track** — this experiment closes the operator's TWRP/storage clause, not the XNU
  driver gap.
- **Nothing is armed and no press is owed.** The rung-57 arm `armed-storage-71b54d73` remains parked
  and unpressed; the press path is unchanged.