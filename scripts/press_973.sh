#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE.  The 972 press (2026-10-10) CONFIRMED the de-underscore
# fix (_disable_preemption no longer recurs) and the boot advanced ONE RUNG to a NEW missing symbol:
#   a symbol this image does not provide was called   stub_hit=PE_init_SocSupport_stub
#   caller=0x8048bd50 = PE_init_platform+0x2c         device returned clean, no brick.
#
# THE CAUSE (doc 973).  The 914 board platform expert (src/platform/darwin13/pe_msm8974.c) was
# COMPILED (into out/xnu_arm_obj_d13/pe_msm8974.o, a pool NO entry link list reads) but never LINKED.
# So PE_init_platform's call to PE_init_SocSupport reached the GENERATED stub (body `b entry_stub_hit`)
# instead of the Mi 4 PE (msm8974_putc / QTimer timebase / GIC init).
#
# THE FIX (a build-tool edit, no tree change).  build_xnu_arm_kernel.sh compiles pe_msm8974.c with
# -DBOARD_CONFIG_MSM8974=1 into $XNU_PLATFORM_OBJ_OUT; build_entry.sh NAMES it in LINK_OBJS and adds a
# by-value linked-image clause refusing the generated stub.  Verified by value in the linked ELF:
#   PE_init_SocSupport_stub T 0x801ae3ec  body: bl <PE_early_puts>   (NOT entry_stub_hit)
#   PE_init_SocSupport_msm8974 T 0x801ae2f0   msm8974_putc T 0x801ae290
#
# WHAT THIS PROVES / HOW TO READ IT:
#   the boot moves PAST PE_init_SocSupport => PE_init_platform reaches the Mi 4 PE methods (console/
#   QTimer timebase/GIC), and the next stub_hit (if any) NAMES itself one rung further up.
#   STILL STOPS at PE_init_SocSupport_stub => the real object is not the one linked; record, not paper.
#
# THE ARM is `armed-d13-12427611` (entry bin 12427611; payload switch record 6c2b6038 UNCHANGED - the
# fix is a link, not an arm switch).  Entry image 6331476 bytes; the press sends stage90-qcdt.img
# 7d12735f... (9351168 bytes).  Medium: 968's real iOS 7.1.2 HFSX rootfs over hfs_rmd0; HDD_WRITE=0,
# so the BASE IS NEVER WRITTEN - fully reversible.  RESIDENT rung (POST_END_TICKS=0): budget for a
# black screen + a power-cycle capture, not a clean return (mi4-xnu-reboot-path-cannot-reset).
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-d13-12427611

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