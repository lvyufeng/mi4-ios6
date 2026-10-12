#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE.  984 = 983's switch set REBUILT with the GIC-WINDOW fix,
# so the embedded entry image agrees with the source that fixes the fault 982's press hit.
#
# WHY 982 IS NOT THE ONE TO PRESS.  982's press (2026-10-11) proved EDIT A worked - the PE handler
# ml_io_map'd GICC and read it through the VA, so the run went PAST 981's stop through PE_init_iokit,
# IOKitInitializeTime, the MSM8974Timer driver and into bsd_init.  It then hit the SAME defect on the
# OTHER half of the window:
#     xnu_live_sleh_seq=1 type=4 fsr=5 far=0xf9000104 pc=0x8000c874 lr=0x801ac87c
#   -> pc = entry_irq_enable_line+0x54, lr = MSM8974Timer::start
# The timer driver's own arming call (MSM8974Timer.cpp:1831, intid 40) reads GICD_ISENABLER0 through
# `gicd_read`, whose base was the RAW PHYSICAL 0xf9000000.  XNU's pmap does not translate that either:
# it maps only [physBase, physBase+gMemSize) and the managed window, and the entry image's 0xf9000000
# boot-L1 section is dropped once the kernel owns the tables.  The 981 class, one register over
# ([[mi4-one-value-two-definitions]]).
#
# THE FIX (entry_irq.c + entry_gic.h + pe_msm8974.c): the four GIC accessors add ONE base each -
# `g_irq_gic_dist_va` / `g_irq_gic_cpu_va` - zero while physBase == virtBase (every pre-XNU caller
# unchanged), set to `ml_io_map(phys) - PHYS` by the new `entry_irq_gic_map`, which the board PE's
# `msm8974_gic_map` calls once the kernel owns the tables.  The PE now maps BOTH windows (the
# distributor it did not map before) and hands the image the two VAs.  One window, one definition.
# VERIFIED BY VALUE in the linked ELF: `gicd_read` loads `g_irq_gic_dist_va` (add r3,#0xf9000000),
# NOT a bare 0xf9000000; `entry_irq_gic_map` @0x8000cef8; the two VAs in .bss.
#
# THE ARM is `armed-d13-c36a3e06`.  Every switch is 982's (window 0x1e400000 = 484 MiB, RESIDENT=1,
# USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1, HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/
# CARD_COW=1, HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0).  The ENTRY BIN MOVED (8cfb7ca8 -> 887d60ea).
#
# WHAT THIS PROVES / HOW TO READ IT:
#   No `sleh_abort` at far=0xf9000104 => the distributor is mapped and read through the VA too, and
#   the timer driver's own arming call no longer faults.
#   `bsd_init:` markers continue past 982's stop (`bsd_init: calling IOKitInitializeTime`); `BSD root:`
#   names the card's HFSX volume and launchd execs past its __TEXT => iOS userspace is up.
#   FALSIFICATION: it aborts at far=0xf9000104 again => the GIC-window edit did not take, or a THIRD
#   raw-constant site exists; the new pc/lr names it.  Either way the next stop names the frontier.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible.  RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture, not a clean return
# (mi4-xnu-reboot-path-cannot-reset).  The arm carries the full USB ladder (EP1-IN), so start the host
# reader FIRST and it captures the console LIVE - resident or faulted, either way readable.
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press.  Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-d13-c36a3e06

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the USB ladder (the reader only helps if it does) =="
grep -q '^STAGE90_XNU_USB_STREAM=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: this entry record has no STAGE90_XNU_USB_STREAM=1, so the arm streams nothing to"
  echo "EP1-IN and the host reader below would capture an empty file. Rebuild the arm with the ladder on."
  exit 1
}
echo "   (record carries USB_PROBE/DEV/ENUM/STREAM=1 - the console will stream to EP1-IN)"

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
# 983 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg (a resident arm has no
# ending - build_entry.sh refuses POST_END_* when RESIDENT=1), so the runner's power-cycle capture reads
# nothing.  The arm already carries the full USB ladder (USB_PROBE/DEV/ENUM/STREAM=1), which streams the
# RAM console to EP1-IN (18d1:0910) from __wrap_machine_idle.  So start the HOST reader FIRST, with
# --wait, and it attaches the moment the payload enumerates and captures the console LIVE - resident or
# faulted, either way readable.  tools/check_usb_host_reader.py guarantees the reader's EP/VID/PID match
# src/entry/entry_usb_enum.h.
USB_OUT="out/stage90/captures/usb-console-985-$(date -u +%Y%m%d-%H%M%S).txt"
tools/usb_console_read.py --out "$USB_OUT" --wait 600 --seconds 0 &
USB_PID=$!
echo "   (USB console reader armed: pid $USB_PID -> $USB_OUT)"
trap 'kill "$USB_PID" 2>/dev/null || true' EXIT

scripts/preflight_boot_check.sh --allow-xnu-entry
# The runner exits 2 when a RESIDENT rung does not come back - that is the EXPECTED outcome here, not a
# failure, so do not let `set -e` abort before the reader is stopped and its capture is reported.
set +e
scripts/run_and_capture.sh --allow-xnu-entry --expect-arm="$EXPECT_ARM"
RUN_RC=$?
set -e
echo "   (runner exit $RUN_RC; a resident rung not returning shows as 2)"

kill "$USB_PID" 2>/dev/null || true
echo "== USB console capture: $USB_OUT =="
if [ -s "$USB_OUT" ]; then echo "   bytes read: $(wc -c < "$USB_OUT")"; else
  echo "   (empty - the ladder did not reach the idle path, or the payload stopped before it)"
fi