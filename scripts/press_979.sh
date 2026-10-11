#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE.  979 = 977 (armed-d13-d693864f) with the entry.ld
# PAGE-ALIGNMENT fix (see doc/experiment-979).  The 977 press was recovered and it is a REAL XNU boot
# that panicked in `ipc_bootstrap`: pc = `lock_set_init`+0, lr = `ipc_bootstrap`+0x194, fsr=0x0f
# (permission fault, prefetch).  ROOT CAUSE: the entry image's `.text` ended at `__entry_text_end` =
# 0x805699a0 (NOT page-aligned: `. = ALIGN(32)`), while the linker put the real `.data` output section
# on the next page, 0x8056a000.  `pmap_init` (`osfmk/arm/pmap.c:3620`) starts its data-NX loop at
# `sdata = sectDATAB = __entry_data_start = 0x805699a0`; its FIRST iteration, `pmap_pte(0x805699a0)`,
# resolved to the last TEXT page `[0x80569000, 0x8056a000)` -- holding `lock_set_init` -- and set
# `L2_NX_BIT` on it, making it non-executable.  THE FIX (commit 2fd98af): `entry.ld` pads `.text` to a
# page (`. = ALIGN(0x1000)` inside the section, was `ALIGN(32)`) so `__entry_text_end ==
# __entry_data_start == 0x8056a000` = the real `.data` page.  Verified by value: nm shows both equal
# 0x8056a000, `__TEXT` vmsize 0x56a000 (was 0x5699a0); host_entry_macho_check OK; make check 0.
#
# THE ARM is `armed-d13-97d6a511` = 977 with the ONE entry.ld change.  Every switch is 977's (window
# 0x1e400000 = 484 MiB, RESIDENT=1, USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1,
# SMEM_PROBE=1, IDLE_NO_SLEEP=0).  The ENTRY BIN MOVED (d693864f -> 97d6a511).  stage90-build-config.txt
# is 6c2b6038 (unchanged).  THIS IS THE FIRST PRESS OF A BOOTABLE ENTRY LINE WHOSE LAST-TEXT-PAGE NX
# DEFECT IS FIXED -- so if the fix is right, XNU proceeds PAST `ipc_bootstrap` into the rest of
# `kernel_bootstrap` (the task/scheduler/VM init and then toward the root-mount + launchd exec).
#
# WHAT THIS PROVES / HOW TO READ IT:
#   No `Kernel prefetch abort` at `lock_set_init` => the last-text-page NX defect is retired; the boot
#   moves to the NEXT frontier (read where it stops next).  BSD root: names the card's HFSX volume and
#   launchd execs past its __TEXT => the 484 MiB window (975) is enough and iOS userspace runs.
#   FALSIFICATION: it panics at the SAME pc again => the fix did not take (rebuild/verify again).
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

EXPECT_ARM=armed-d13-97d6a511

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
USB_OUT="out/stage90/captures/usb-console-979-$(date -u +%Y%m%d-%H%M%S).txt"
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