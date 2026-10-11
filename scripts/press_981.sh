#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE.  981 = 980's switch set REBUILT on the fixed kernel
# sources, carrying TWO kernel fixes 980's embedded image predates.
#
# EDIT A (pe_msm8974.c, msm8974_get_timebase). It returned NANOSECONDS, but the generic RTC layer
# converts AGAIN: rtclock_timer_scale_convert (osfmk/arm/rtclock.c:82-93) computes
# `ticks = get_timebase() * NSEC_PER_SEC / tscFreq`. So mach_absolute_time came out 52.0833x too
# large, and every scheduler/idle deadline derived from it was pushed past the SoC watchdog's ~25 s
# reset - which is exactly why the 980 run reached IOKit, idled, and was reset with NO idle wake (its
# last live record is `xnu_live_idle_seq=1`, no `xnu_live_wdt_*`). Now returns RAW CNTVCT ticks, like
# the apq8060 peer; VERIFIED BY VALUE: the linked ELF is `mrrc p15,0,r0,r1,c14; bx lr`.
#
# EDIT B (bsd_init.c:405). `bsd_init_kprintf` was a no-op; now `kprintf("bsd_init: " x)`, lighting
# ~48 markers through bsd_init (vfs_mountroot, VFS_ROOT, bsd_utaskbootstrap, launchd) so the next stop
# is legible instead of silent. 58 `bsd_init:` strings are in the linked ELF.
#
# WHY 980 IS NOT THE ONE TO PRESS. Both edits change POOL/platform objects the entry image links, so
# the source is newer than 980's embedded image, and preflight_boot_check.sh correctly refuses 980's
# live bytes as STALE. 981 is 980's switch set rebuilt so the source and the image agree.
#
# THE ARM is `armed-d13-7fbd7009`. Every switch is 980's (window 0x1e400000 = 484 MiB, RESIDENT=1,
# USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1, HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/
# CARD_COW=1, HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0). The ENTRY BIN MOVED (fa301bd7 -> 7fbd7009).
#
# WHAT THIS PROVES / HOW TO READ IT:
#   The idle deadline now wakes before the 25 s watchdog => an idle wake appears (not a bare reset),
#   and the boot continues past IOKit instead of being reset there (EDIT A's whole point).
#   `bsd_init:` markers appear in the console, naming the frontier through bsd_init (EDIT B).
#   BSD root: names the card's HFSX volume and launchd execs past its __TEXT => iOS userspace is up.
#   FALSIFICATION: it is reset again with no idle wake => EDIT A did not take; it panics at
#   rtclock_init => the timebase publication regressed. Either way the next stop names the frontier.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible. RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture, not a clean return
# (mi4-xnu-reboot-path-cannot-reset). The arm carries the full USB ladder (EP1-IN), so start the host
# reader FIRST and it captures the console LIVE - resident or faulted, either way readable.
#
# Three steps: (1) confirm the device is back, (2) build/write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

EXPECT_ARM=armed-d13-7fbd7009

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
# 979 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg (a resident arm has no
# ending - build_entry.sh refuses POST_END_* when RESIDENT=1), so the runner's power-cycle capture reads
# nothing.  The arm already carries the full USB ladder (USB_PROBE/DEV/ENUM/STREAM=1), which streams the
# RAM console to EP1-IN (18d1:0910) from __wrap_machine_idle.  So start the HOST reader FIRST, with
# --wait, and it attaches the moment the payload enumerates and captures the console LIVE - resident or
# faulted, either way readable.  tools/check_usb_host_reader.py guarantees the reader's EP/VID/PID match
# src/entry/entry_usb_enum.h.
USB_OUT="out/stage90/captures/usb-console-981-$(date -u +%Y%m%d-%H%M%S).txt"
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