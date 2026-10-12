#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE.  986 = 985's switch set REBUILT with the MLCKWAIT
# WAITERS-BIT FIX, so the embedded entry image agrees with the source that fixes the stall 985's press hit.
#
# WHY 985 IS NOT THE ONE TO PRESS.  985's press (2026-10-12, arm armed-d13-c36a3e06, commit cfe99f4)
# ARMED and FIRED the D13 clock - tick_arm_rc=1, tick_line_rc=1, xnu_live_tick_irq 1->2->4->8->16 - and
# the boot thread's 2 s deadline sleep (lck_mtx_sleep_deadline @0x8001ccb0) now RETURNS. But the boot
# still stops in IOKitInitializeTime (no cal_enter, no ubc_init): after the wait returns the SAME thread
# enters `mlckwait` (0x80497510) and never comes back.
#
# THE FIX (tools/stage_d13_hw_lock_986.sh + build_entry.sh by-value clause): `mlckwait` must set bit 1
# (ARM_LCK_WAITERS) before it sleeps, or the owner's unlock takes lck_mtx_unlock's FAST path
# ((word&3)==0) and never calls thread_wakeup_one - a LOST WAKEUP. The linked image instead had
# `0x80497504: e3833000  orr r3, r3, #0`. The correct `orr r3, r3, #3` is gated behind
# `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` (a 4-line fork addition; upstream Apple is
# plain #0), and this ELF build's assembler gets no board macro. 986 makes the `#3` UNCONDITIONAL
# (hw_lock.s is D13-only). VERIFIED BY VALUE: the linked xnu_arm_entry.elf at 0x80497504 is e3833003.
#
# THE ARM is `armed-d13-2171f0d1`.  Every switch is 985's (window 0x1e400000 = 484 MiB, RESIDENT=1,
# USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1, HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/
# CARD_COW=1, HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0). The ENTRY BIN MOVED (c36a3e06 -> 2171f0d1).
#
# WHAT THIS PROVES / HOW TO READ IT (the discriminating experiment):
#   The boot moves PAST `mlckwait` (a new stop, or further: `xnu_live_cal_enter`, `ubc_init`) =>
#   the lost wakeup is CONFIRMED as 985's frontier and the mutex fix took.
#   FALSIFICATION: it still parks at `mlckwait` => the mechanism is not the (or not the only) cause and
#   the deadlock hypothesis wins; the next rung is who holds the mutex.
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

EXPECT_ARM=armed-d13-2171f0d1

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
# 986 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-986-$(date -u +%Y%m%d-%H%M%S).txt"
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