#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: the 905 arm repaired the CMD24 write path with a programming wait. Before re-pressing we must
# (1) confirm the device is back on adb, (2) rewrite the CLEAN HFS+ card image to userdata's head
# (the previous aborted rw mount left the volume DIRTY, so a re-press off it would fall back to
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
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-9d2dd2e4
