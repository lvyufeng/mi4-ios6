#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE, after waking it (hold Power ~10-15s).
#
# WHY: 970 is the rung that tests whether the ENTRY WINDOW was the wall. 969 measured that the real iOS
# 7.1.2 `/sbin/launchd` links libSystem/libbsm out of the 301 MiB dyld shared cache, while XNU managed
# only the 16 MiB window - and concluded iOS could not run without the whole-kernel 915-B pmap port.
# 970 reads `arm_vm_init.c:422 avail_end = gPhysBase + gMemSize`: the window is not just an address, it
# is the ALLOCATOR's physical-RAM end, anchored at gPhysBase=0x80000000 inside the real high bank
# [0x80000000, 0xDE500000). So one switch, STAGE90_XNU_ENTRY_WINDOW = 0x1e400000 (484 MiB), gives XNU
# ~470 MiB free instead of ~6 MiB - enough for the shared cache - with NO pmap port.
#
#   - THE MEDIUM IS 968's (965c's real rootfs): `tools/build_root_volumes.sh --real` copies the real iOS
#     7.1.2 rootfs to `out/stage90/xnu_card_hfs.img`; this script writes THAT to userdata's head
#     (`seek=0`; the card reads LBA 0x400000 = partition byte 0). It is the COW's base.
#   - THE ARM is 968's (`armed-d13-7107b998`'s switch set) with ONE key changed:
#     `STAGE90_XNU_ENTRY_WINDOW 0x01000000 -> 0x1e400000`. The window reaches XNU ONLY through the
#     payload's generated header, so the entry bin is byte-identical to 968's and the set name comes
#     from the qcdt (armed-window-c74bde1d).
#   - `0x1e400000` and not a wider window: D13's managed map is FIXED-BASE with no clamp
#     (`[0xC0000000, 0xC0000000 + gMemSize)`), so a window that reaches the entry's RAM-CONSOLE VA
#     0xde500000 occupies the L1 slot `entry_section_install` must install into, and the install is
#     refused -> silent boot, no log. Above the console the window also swallows the GIC/USB/WDT/SMCC/
#     GCC/TLMM MMIO and the SMEM alias at 0xe0000000. Real ceiling = 0xde500000 - 0xC0000000 =
#     0x1e500000; build_entry.sh refuses any window >= that, and 0x1e400000 (484 MiB) is the safe max.
#
# WHAT THIS PROVES: if the premise holds, real iOS userspace (launchd, then SpringBoard) can map its
# shared cache and iOS RUNS - no 915-B port. The runner reports `xnu_entry_args_memSize` (= 0x1e400000),
# `BSD root:` naming the card's HFSX volume, and the COW block. FALSIFICATION: XNU panics before idle
# (the 998 MiB map faults) or launchd still cannot map the cache - then the window's SIZE was not the
# wall and 915-B is back on the table. Either way this press is the cheap next measurement.
#
# **THE BASE IS NEVER WRITTEN (`CARD_COW=1`, `HDD_WRITE=0`), so this arm carries no brick risk to the
# card and is fully reversible** - a power cycle reverts every write (`VOLATILE=1`). Step 2 only re-writes
# the card HEAD (which the operator already owns); XNU's own writes land in RAM.
#
# THE ARM IS A NON-RETURNING (RESIDENT) RUNG (`POST_END_TICKS=0`): budget for a black screen + a
# power-cycle capture, not a clean return (`mi4-xnu-reboot-path-cannot-reset`).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

# The 970 arm = 968's switch set with STAGE90_XNU_ENTRY_WINDOW=0x1e400000. Its name is set from the qcdt
# (the entry bin did not move); confirm with `tools/resolve_arm_set.sh out/stage90` before pressing.
EXPECT_ARM=armed-window-c74bde1d

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