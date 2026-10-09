#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: 965c is the rung that serves the REAL decrypted iOS 7.1.2 rootfs to XNU's own mount path. Two things
# stood in the way, both host-side and both fixed here:
#
#   (a) THE MEDIUM WAS THE WRONG VOLUME. 965b made the pressed card-root arm (`ROOT_FROM_CARD=1`) mount an
#       HFSX card image, but that image was the 512 KiB mkfs FIXTURE. The goal needs the REAL volume:
#       `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` (896 MiB, HFSX 0x4858 v5, 7142 files) - the actual,
#       decrypted iOS 7.1.2 root. `tools/build_root_volumes.sh --real` copies it to `out/stage90/xnu_card_hfs.img`
#       and verifies the family; this script writes THAT to userdata's head.
#
#   (b) THE STRATEGY'S BOUND WAS 32-BIT. `st_medium_disk_bytes(ST_MEDIA_DRIVER)` returns
#       `(unsigned)(selected_count * 512)`; userdata is 13,610,499,072 B, so the bound was 725,597,184 B
#       (692 MiB) - LESS than the 896 MiB rootfs. A mount over that bound reads EOF for the volume's top
#       ~204 MiB and cannot parse its catalog. `STAGE90_XNU_FULL_EXTENT=1` computes the bound in 64 bits
#       (`st_medium_card_full_bytes()`), so the strategy addresses the whole partition. This is the entry
#       arm the press boots (its hash is the one `--expect-arm` names below) - NOT the 965b arm, whose
#       32-bit bound would silently truncate the real volume.
#
# WHAT THIS PROVES: on a card-root arm, `hfs_mountroot` parses and mounts the REAL 896 MiB iOS 7.1.2 HFSX
# rootfs - the goal's own root volume, off the device's own storage, through XNU's own HFS driver and the
# ladder's read path. If it mounts, the HFSX branch (`hfs_vfsutils.c:339-345`), the multi-extent catalog
# (first extent block 9220), and the 64-bit bound are all exercised. If it fails ABOVE 692 MiB, the
# FULL_EXTENT arm did not take - the falsifier the preflight's size clause also guards.
#
# THE ARM IS A NON-RETURNING (RESIDENT) RUNG: it carries `POST_END_TICKS=0`, so budget for a black screen +
# a power-cycle capture, not a clean return (`mi4-xnu-reboot-path-cannot-reset`).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head
# (`seek=0` - the card reads LBA 0x400000 = partition byte 0), (3) press. Steps 2 and 3 run the project's
# own gate + runner - never a raw fastboot, and NEVER a flash. The write touches ONLY userdata's head.
set -euo pipefail
cd "$(dirname "$0")/.."

# The 965c arm = the 965b switch set plus STAGE90_XNU_FULL_EXTENT=1. Its hash is set by build_entry.sh from
# the built image; run `tools/resolve_arm_set.sh out/stage90` (or read xnu_arm_entry-config.txt) to confirm
# it before pressing. Update this if the arm is rebuilt with any switch changed.
EXPECT_ARM=armed-d13-e28361f9

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image =="
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