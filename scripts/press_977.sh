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
# THE ARM is `armed-d13-d693864f` = 975 (armed-window-1091566c, entry bin 73475747) with the D13
# residence rung's WATCHDOG PET LINKED (experiment-977).  975's press went dark because a RESIDENT=1
# D13 image armed the SoC watchdog and could not feed it - the pet was inside the 4570-only idle block,
# so the run reset mid-boot before the USB ladder reached __wrap_machine_idle.  The pet now compiles
# under `#if STAGE90_XNU_RESIDENT` alone and is called from __wrap_machine_idle, so THIS press is the
# first resident D13 run that can keep itself alive AND stream its console to EP1-IN.  The ENTRY BIN
# MOVED (73475747 -> d693864f), so the name comes from the entry bin (armed-d13-*), not the qcdt.
# stage90-build-config.txt is 6c2b6038 (unchanged).
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

EXPECT_ARM=armed-d13-d693864f

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
# 975 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg (a resident arm has no
# ending - build_entry.sh refuses POST_END_* when RESIDENT=1), so the runner's power-cycle capture reads
# nothing.  That is exactly why 975's 2026-10-10 press went dark.  The arm already carries the full USB
# ladder (USB_PROBE/DEV/ENUM/STREAM=1), which streams the RAM console to EP1-IN (18d1:0910) from
# __wrap_machine_idle.  So start the HOST reader FIRST, with --wait, and it attaches the moment the
# payload enumerates and captures the console LIVE - resident or faulted, either way readable.
# tools/check_usb_host_reader.py guarantees the reader's EP/VID/PID match src/entry/entry_usb_enum.h.
USB_OUT="out/stage90/captures/usb-console-977-$(date -u +%Y%m%d-%H%M%S).txt"
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