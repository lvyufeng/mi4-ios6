#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: 905d is 905c REBUILT with the run's own self-ending RESTORED. 905c was built with
# `STAGE90_XNU_POST_END_TICKS` UNSET in its build environment, so `build_entry.sh` took its own default
# of 0 and compiled the run's ONLY working return OUT of the image - the device black-screened and the
# RAM-console log died with the power. That is `mi4-build-variant-comes-from-an-env-default`: a switch
# left off an env carries a DIFFERENT experiment's value, and no hash in the tree can see it.
#
# 905c's WRITE FIX WAS NOT THE FAULT. It advanced the boot FURTHER than any earlier arm - into the idle
# loop (`slot_post` / `post_elapsed`) - which is exactly where a missing ending shows up as a hang.
# Every returning arm (902/903/904) carries `POST_END_TICKS=115200000` (6000 ms at 19200 ticks/ms); it
# is the only net that returns this device (PS_HOLD and the hardware watchdog do not reset it - see
# mi4-xnu-reboot-path-cannot-reset and mi4-the-run-ends-at-entry-epilogue).
#
# 905d is 905c plus that ONE number and nothing else: the 24 arm keys, the C1 write VERDICT
# (`xnu_live_storage_wr_int_data_end` / `_wr_data_err`, read after the programming wait) and the
# recorded rw-root switch (`STAGE90_XNU_HFS_ROOT_RW=1`, checked against the linked `hfs_mountroot` in
# both directions) are unchanged, and the seam (`STAGE90_XNU_SEAM_LR = 0x8004e2dc`) did not move.
#
# Three steps: (1) confirm the device is back, (2) rewrite the CLEAN HFS+ card image to userdata's head
# (the last rw attempt may have left the volume DIRTY, and a re-press off a dirty volume falls back to
# read-only), (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot.
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
echo "expected:  b321db0d45436b8dd27d68ef0c3dafedf2f6ea5bfe70af7335e68d888468c410"

echo "== 3. the press (gate + runner, fastboot boot only) =="
scripts/preflight_boot_check.sh --allow-xnu-entry
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-0e6eb4b4