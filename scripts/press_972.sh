#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE.  The 971 press (2026-10-10) is the FIRST D13 press PAST
# arm_vm_init: capture 971-armed-d13-7c230cf9-20261010-last_kmsg.txt (4530 lines) carries
# 'arm_vm_init: switching translation-tables now...' and THEN 'arm_vm_init: setting up segment
# information...' - keys AFTER the switch, for the first time.  The boot then STOPPED DEAD on
#   a symbol this image does not provide was called   stub_hit=_disable_preemption   caller=0x8002c140
# (printf), and returned clean (17 s, no brick, 'No errors detected').  Device is healthy.
#
# THE CAUSE (doc 972).  D13 osfmk/arm/cpu_data.h:145 `#define disable_preemption _disable_preemption`
# makes D13's C call `_disable_preemption` (1 underscore); machine_routines_asm.s:171 EnterARM also
# yields `_disable_preemption`; but tools/assemble_arm_layer.sh's de-underscore step STRIPPED it to
# `disable_preemption`, so the C reference resolved against the entry's entry_stub_hit stub.
#
# THE FIX (tools/assemble_arm_layer.sh only): the de-underscore step preserves the tree's own
# `#define X _X` underscore names (derived; inert on 4570).  By value in the linked image,
# machine_routines_asm.o now defines `_disable_preemption` @0x8001ab60 (the real asm).
#
# WHAT THIS PROVES / HOW TO READ IT:
#   the boot moves PAST _disable_preemption => a further missing symbol or a clean boot; the next
#   stub_hit NAMES itself in the same line, so the ladder advances one rung per press.
#   STILL STOPS at _disable_preemption => the fix is not in the reached object; recorded, not papered over.
#
# THE ARM is `armed-d13-0184b928` (entry bin 0184b928; payload switch record 6c2b6038 UNCHANGED - the
# fix is a build-tool edit).  Entry image 6331476 bytes; the press sends stage90-qcdt.img 6a4d0c23...
# (9351168 bytes).  Medium: 968's real iOS 7.1.2 HFSX rootfs over the CARD COW; HDD_WRITE=0, so the
# BASE IS NEVER WRITTEN - fully reversible.  RESIDENT rung (POST_END_TICKS=0): budget for a black
# screen + a power-cycle capture, not a clean return (mi4-xnu-reboot-path-cannot-reset).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-d13-0184b928

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2a. stage the REAL 896 MiB iOS 7.1.2 rootfs as the card image (the COW base) =="
tools/build_root_volumes.sh --real

echo "== 2b. write it to userdata's head (partition offset 0) =="
# **STREAMED, not pushed.**  The 970h attempt pushed the 896 MiB image to /data/local/tmp and hit
# "No space left on device": /data on this device is a 512 MB tmpfs, not the userdata partition.
# p25 (userdata, 13610499584 bytes) is directly writable as root and unmounted, so dd the image in
# over adb stdin.  This is the 965c/968 COW base write and it is the one that worked.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
scripts/preflight_boot_check.sh --allow-xnu-entry
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"