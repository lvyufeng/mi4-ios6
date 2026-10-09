#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: 968 is the rung that serves WRITES to the real iOS 7.1.2 rootfs - the half the goal's storage
# clause was still missing after 965c mounted it read-only. It mounts the same real 896 MiB HFSX volume
# (`/mnt/data/ios7-payload/v2/ios7/rootfs.hfs`) READ-WRITE over a RAM copy-on-write shadow, so iOS
# userspace's writes to `/private/var` are served without ever touching the base medium.
#
#   - THE MEDIUM IS 965c's: `tools/build_root_volumes.sh --real` copies the real rootfs to
#     `out/stage90/xnu_card_hfs.img`; this script writes THAT to userdata's head (`seek=0`; the card
#     reads LBA 0x400000 = partition byte 0). The COW's base IS that image.
#   - THE ARM differs from 965c by THREE switches: `STAGE90_XNU_HFS_ROOT_RW=1` (hfs_mountroot clears
#     MNT_RDONLY), `STAGE90_XNU_CARD_COW=1` (the RAM shadow serves the card unit's B_WRITE), and
#     `STAGE90_XNU_HDD_WRITE=0` (the base is NEVER written). Also `FULL_EXTENT=1` (965c's 64-bit fix).
#
# WHAT THIS PROVES: on this arm, `hfs_mountroot` parses and mounts the REAL iOS 7.1.2 HFSX rootfs
# READ-WRITE, and a write from the fixture's userspace is served by the RAM shadow - with the base
# medium byte-exact afterwards. The runner's summary now prints the COW block (`xnu_live_rootmedia_cow_*`):
#   `_cow_wr_blocks` present with NO `_cow_refused`  => the shadow served every write (WRITABLE ROOT MET).
#   `_cow_refused` present                           => a write hit a page the 2 MiB arena could not hold
#                                                       (ENOSPC/EIO, never a silent drop) - the arena is
#                                                       too small for this run, and the mount is NOT writable
#                                                       end to end. That is the honest failure reading.
#
# **THE BASE IS NEVER WRITTEN, so this arm carries no brick risk to the card and is fully reversible** -
# a power cycle reverts every write (`VOLATILE=1`, as the HD2 lab marks it). Unlike 905's HDD_WRITE, this
# does NOT modify the volume; step 2 only re-writes the card HEAD (which the operator already owns), and
# XNU's own writes land in RAM.
#
# THE ARM IS A NON-RETURNING (RESIDENT) RUNG (`POST_END_TICKS=0`): budget for a black screen + a
# power-cycle capture, not a clean return (`mi4-xnu-reboot-path-cannot-reset`).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
# The write touches ONLY userdata's head, and only the head the operator already provisioned.
set -euo pipefail
cd "$(dirname "$0")/.."

# The 968 arm = the 965c switch set plus HFS_ROOT_RW=1 + CARD_COW=1 (HDD_WRITE stays 0). Its hash is set
# by build_entry.sh from the built image; run `tools/resolve_arm_set.sh out/stage90` (or read
# xnu_arm_entry-config.txt) to confirm it before pressing. Update this if the arm is rebuilt.
EXPECT_ARM=armed-d13-7107b998

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2b. write it to userdata's head (partition offset 0) =="
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b push out/stage90/xnu_card_hfs.img /data/local/tmp/xnu_card_hfs.img
adb -s 4a2fe00b shell "dd if=/data/local/tmp/xnu_card_hfs.img of=/dev/block/mmcblk0p25 bs=1M seek=0 conv=fsync"
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
scripts/preflight_boot_check.sh --allow-xnu-entry
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"