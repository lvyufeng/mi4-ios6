#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: 906 is 905d (`armed-storage-0e6eb4b4`) rebuilt with ONE fixture change and nothing else. 905d proved
# the WRITE PATH - the volume header changed on the eMMC and persisted across the run, the card accepted
# every block, `xnu_live_storage_wr_complete=1` with `_wr_err=0` and `_wr_data_err=0`. But the new file's
# NAME never appeared: the fixture went straight from `write` to the park (via 508's `b park`), and HFS+
# writes a B-tree node with `bdwrite_internal(bp, 1)` - it marks the node dirty in the buffer cache and
# writes it *later*, on `fsync`/`close`. So the catalog record the create made died with the run, and only
# the blocks HFS syncs on its own (the volume header, the lazily-created private directory) survived.
#
# 906's fixture adds `fsync(0)` (syscall 95) right after the write: it reaches `hfs_vnop_fsync` ->
# `hfs_fsync(vp, waitfor, 0, ...)`, whose `hfs_metasync(hfsmp, cp->c_hint, ...)` is the `VNOP_BWRITE` of
# that very dirty node (`hfs_vnops.c:2380`). The fd is the literal 0 because that is what `open` returned
# in 905's run (`xnu_live_open_fd = 0`). The program went 88 -> 91 words; NO arm switch changed.
#
# THREE PARTS MOVED, and all three must be the 906 build: the entry image (`f347d060`), the payload
# (f347d060 embedded), and - the one easy to forget - the CARD IMAGE. `/sbin/launchd` on the card IS this
# same fixture, so `xnu_card_hfs.img` had to be rebuilt from the new `xnu_arm_entry_ramdisk.o`; its sha
# moved `b321db0d` -> `d9d2ade4`. A press with the OLD card image would exec the OLD launchd (no fsync).
#
# Three steps: (1) confirm the device is back, (2) rewrite the CLEAN HFS+ card image to userdata's head
# (`seek=0` - the card reads LBA 0x400000 = partition byte 0), (3) press. Steps 2 and 3 run the project's
# own gate + runner - never a raw fastboot.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. rewrite the clean card image to userdata's head (partition offset 0) =="
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b push out/stage90/xnu_card_hfs.img /data/local/tmp/xnu_card_hfs.img
adb -s 4a2fe00b shell "dd if=/data/local/tmp/xnu_card_hfs.img of=/dev/block/mmcblk0p25 bs=512 seek=0 conv=fsync"
echo "read-back:"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=512 count=1024 2>/dev/null" | sha256sum
echo "expected:  d9d2ade4e5b0b3086cec2005567a7df83493579a05067d65ac7dde6c3e6a4bb6"

echo "== 3. the press (gate + runner, fastboot boot only) =="
scripts/preflight_boot_check.sh --allow-xnu-entry
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-f347d060