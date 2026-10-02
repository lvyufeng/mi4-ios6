#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: 905c repairs 905b. 905b added a bounded DOING_WRITE|DAT_LINE_ACTIVE programming wait after the
# CMD24 PIO loop, but its press never returned (the device went dark and the RAM-console log died with
# the power), so the wait was never read. An adversarial verification of the fix confirmed the diagnosis
# and kept the wait, and added two things this arm carries: (1) the write body now publishes a VERDICT
# (`xnu_live_storage_wr_int_data_end` / `_wr_data_err`, read after the wait) so a card that ended the
# write in a DATA error is not recorded as a success; (2) `STAGE90_XNU_HFS_ROOT_RW` is a RECORDED arm
# key, REQUIRED with HDD_WRITE=1, and the linked image is checked in BOTH directions (hfs_mountroot
# calls vfs_clearflags iff the record says 1) - so an arm whose record promises a writable root cannot
# ship a pool whose hfs_mountroot had the clear compiled out.
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
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-d6e2fbe6