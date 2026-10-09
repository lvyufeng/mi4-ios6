#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: 965b is the rung that makes the PRESSED arm's root medium HFSX. 965a (arm `armed-d13-b9c224c0`)
# flipped the COMMITTED blob `src/entry/blob/xnu_arm_entry_root_hfs.img` to HFSX and committed it - but
# that arm carries STAGE90_XNU_ROOT_FROM_CARD=1, so `__wrap_mdevlookup` answers the CARD unit
# (`src/entry/entry_trace.c:5131`) and disk 0 (the blob) is NEVER registered. The filesystem actually
# mounted is a SEPARATE image, `out/stage90/xnu_card_hfs.img`, written here to userdata's head. 965a left
# that image HFS+ v4 - so the press would have mounted HFS+ while the tree's fixture was HFSX, i.e. 965a
# changed an inert byte array on this arm. 965b rebuilds the CARD image as HFSX (`tools/build_root_volumes.sh`)
# and writes THAT. The entry image the press boots is UNCHANGED (b9c224c0); only the medium on the device moves.
#
# WHAT THIS PROVES: on a card-root arm, `hfs_mountroot` parses an HFSX volume (`0x4858 v5`) for the first
# time - the driver branch `bsd/hfs/hfs_vfsutils.c:339-345` no prior arm reached. If the HFSX parse path is
# broken, it fails here, at the proven 512 KiB size, with the medium change the only variable.
#
# THE ARM IS A NON-RETURNING (RESIDENT) RUNG: b9c224c0 carries `POST_END_TICKS=0` / `POST_END_RUN=0`, so
# budget for a black screen + a power-cycle capture, not a clean return (`mi4-xnu-reboot-path-cannot-reset`).
#
# Three steps: (1) confirm the device is back, (2) rebuild the HFSX card image and write it to userdata's
# head (`seek=0` - the card reads LBA 0x400000 = partition byte 0), (3) press. Steps 2 and 3 run the
# project's own gate + runner - never a raw fastboot, and NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2a. rebuild the HFSX card image (and verify the committed blob agrees) =="
tools/build_root_volumes.sh

echo "== 2b. write it to userdata's head (partition offset 0) =="
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b push out/stage90/xnu_card_hfs.img /data/local/tmp/xnu_card_hfs.img
adb -s 4a2fe00b shell "dd if=/data/local/tmp/xnu_card_hfs.img of=/dev/block/mmcblk0p25 bs=512 seek=0 conv=fsync"
echo "read-back (the card's first 1024 sectors):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=512 count=1024 2>/dev/null" | sha256sum
echo "expected (built above):"
sha256sum out/stage90/xnu_card_hfs.img

echo "== 3. the press (gate + runner, fastboot boot only) =="
scripts/preflight_boot_check.sh --allow-xnu-entry
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-d13-b9c224c0