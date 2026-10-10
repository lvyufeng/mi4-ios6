#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE.  975 = THE 484 MiB ENTRY WINDOW ON THE WORKING ENTRY LINE
# (see doc 975).  969 measured that the real iOS 7.1.2 /sbin/launchd links libSystem/libbsm out of the
# 301 MiB dyld shared cache, while XNU managed only the 16 MiB window - so iOS could not run without the
# whole-kernel 915-B pmap port.  970 designed the one-switch escape (STAGE90_XNU_ENTRY_WINDOW=0x1e400000,
# 484 MiB) and pressed it - but ONLY on entry bin 7107b998, WHICH NEVER BOOTED (970 6c: that arm and its
# 16 MiB control failed IDENTICALLY).  So the window was never tested on a working entry.
#
# NOW the entry line boots (971), the board PE is linked (973), and its console is observable (974).
# 975 is the missing measurement: the SAME window, on the line that boots.
#
# THE ARM is `armed-window-1091566c` = 974 (armed-d13-73475747) with STAGE90_XNU_ENTRY_WINDOW
# 0x01000000 -> 0x1e400000.  The window reaches XNU ONLY through the payload's generated header
# (xnu_entry_jump.c:150 a->memSize = STAGE90_XNU_ENTRY_SIZE), so the ENTRY BIN IS UNCHANGED (73475747)
# and the name comes from the qcdt.  stage90-build-config.txt is 6c2b6038 (unchanged).
#
# CEILING: build_entry.sh refuses any D13 window >= 0x1e500000 = RAM_CONSOLE_BASE(0xde500000) -
# MANAGED_BASE(0xC0000000).  0x1e400000 (484 MiB) is the safe max: the console and the MMIO above it
# (GIC/USB/WDT/SMCC/GCC/TLMM, SMEM 0xe0000000) stay intact, and free after topOfKernelData is ~470 MiB,
# enough for the 301 MiB cache.  Verified by value: stage90.elf carries ONE e3a08579 (mov r8,#0x1e400000).
#
# WHAT THIS PROVES / HOW TO READ IT:
#   xnu_entry_args_memSize = 0x1e400000  => the window rung ran (XNU was handed 484 MiB).
#   BSD root: names the card's HFSX volume, launchd execs past its __TEXT, userspace survives => THE
#   WINDOW WAS THE WALL and iOS runs with NO 915-B port.
#   FALSIFICATION: XNU panics early (the 484 MiB _start map faults) => the window's SIZE was not the
#   wall and 915-B is back on the table.  Either way, a cheap next measurement.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible.  RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture, not a clean return
# (mi4-xnu-reboot-path-cannot-reset).  If it does not return, 974's console fix lands the board PE text
# in the captured RAM console, and the arm carries the full USB ladder (EP1-IN).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-window-1091566c

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2b. write it to userdata's head (partition offset 0) =="
# **STREAMED, not pushed.**  /data on this device is a 512 MB tmpfs, not the userdata partition, so the
# image is dd'd in over adb stdin to the unmounted userdata block device.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
scripts/preflight_boot_check.sh --allow-xnu-entry
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"