#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 994 = 993's switch set VERBATIM + STAGE90_XNU_IOS7_TRAPS=1:
# the iOS 7 VM wire-width fix for traps 10/12/14 in the POOL (`osfmk/ipc/mach_kernelrpc.c` +
# `osfmk/arm/ios7_compat.h`) and the -3 SWI timebase wiring in the ASSEMBLY layer
# (`osfmk/arm/traps_lo.s`).
#
# WHY 993 IS NOT THE ONE TO PRESS. 993's press (2026-10-12, arm armed-d13-1d5cf20e, commit 160650b,
# evidence /tmp/cancro-last_kmsg.txt) CONFIRMED the commpage alias AND the high-vector L2 zeroing - the
# 991 `0xffff4090 ... Translation fault` burst is GONE and there is NO panic - and then HALTED inside
# dyld:
#     launchd[1]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2be7e0c4 ...   (236x)
#
# THE NEW FRONTIER, read BY VALUE:
#   0x2be7e0c4 - slide 0x7d000 = 0x2be010c4 = _dyld_fatal_error (fe de ff e7 at file 0x10c4, i.e.
#   `udf #0xdefe`), whose ONLY caller is dyld::halt. dyld::halt is reached from dyld::throwf's
#   out-of-memory path: __simple_salloc's 4 KiB vm_allocate(..., VM_FLAGS_ANYWHERE) (or the following
#   strdup) returned NULL, so the real error string was destroyed before it could print.
#
# THE ROOT CAUSE: the trap-10 wire-width defect. The tree's own struct ios7lab_vm_allocate_wire
# (mach_kernelrpc.c:50-54) is FIVE 32-bit words - { target, addr, size_low, size_high, flags } - the
# iOS 7 ARMv7 wire. The kernel's mach_vm_size_t is 32 bits on this legacy ABI, so a FOUR-word read makes
# args->flags read the size's HIGH half (0) instead of the real flags word, and dyld's vm_allocate looks
# like a failure.
#
# THE FIX (tools/stage_d13_ios7_traps.sh stages it, gated on the SAME switch):
#   (a) POOL - mach_kernelrpc.c's file-wide `#if IOS7_COMPAT_PLATFORM` (:39) opens under the switch and
#       the three _kernelrpc_* VM trap bodies (10/12/14) route through their real-width iOS 7 wire
#       adapters (5/5/7 words) when ios7_traps_enabled() - the narrow selector added to ios7_compat.h -
#       answers 1 (syscall_sw.c needs NO edit - traps 10/12/14 already carry arg counts 5/5/7 and on
#       __arm__ the mungers are ignored; traps 15/24/25/41/42 stay kern_invalid);
#   (b) ASSEMBLY - traps_lo.s's :116 gate closes the `cmn r12, #3` / `swi_trap_tb: movs pc, lr` fast path
#       and :162/:264 open `ios7lab_timebase: blx _mach_absolute_time; b swi_exit64`, so a -3 SWI
#       returns the real 64-bit ns clock.
# VERIFIED BY VALUE: tools/check_d13_ios7_traps.py --expect on asserts, off the linked image's own
# disassembly, that the `cmn r12,#3` / `beq swi_trap_tb` pair is ABSENT, that a `cmn r11,#3` branches to
# the mach_absolute_time call, and that this call is immediately followed by `b swi_exit64`.
#
# THE ARM is `armed-d13-<sha8 of out/stage90/xnu_arm_entry.bin>`. Every switch is 993's, verbatim, plus
# STAGE90_XNU_IOS7_TRAPS=1 (window 0x1e400000 = 484 MiB, RESIDENT=1, USB ladder, MEM_TOTAL/CARD_TOTAL=1,
# HFSX root from card, CARD_COW=1, HDD_WRITE=0, KEXTB_LOGIN=1, SP_ALIGN=1, VMHINT_GUARD=1,
# COMMPAGE_ALIAS=1). The ENTRY BIN MOVED (1d5cf20e -> the new sha8).
#
# WHAT THIS PROVES / HOW TO READ IT (pre-registered):
#   `IOS7LAB vm_allocate64 size=... flags=1 result=0 address=...` markers (the trap-10 wire now reads the
#   real flags word - `flags=1` is VM_FLAGS_ANYWHERE, where the pre-994 read produced 0), the trap-12/-14
#   equivalents (`IOS7LAB vm_deallocate64` / `IOS7LAB vm_protect64`) reading their real width, the OOM
#   gone, and EITHER the real halt message appears (`dyld: <msg>`) OR the boot PROCEEDS PAST dyld.
#   FALSIFICATION: the `udf` at 0x2be7e0c4 (`_dyld_fatal_error`) RECURS - the wire still misreads (the
#   switch did not reach the pool) or the OOM has a different cause.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible. RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture, not a clean return
# (mi4-xnu-reboot-path-cannot-reset). The arm carries the full USB ladder (EP1-IN), so start the host
# reader FIRST and it captures the console LIVE - resident or faulted, either way readable.
#
# Three steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the confirmed 993 and
# regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 994 (scripts/build_994.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 994 (scripts/build_994.sh) first."; exit 1; }

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
# **STREAMED, not pushed.** /data on this device is a 512 MB tmpfs, not the userdata partition, so the
# image is dd'd in over adb stdin to the unmounted userdata block device.
adb -s 4a2fe00b root || true; sleep 1
adb -s 4a2fe00b shell "dd of=/dev/block/mmcblk0p25 bs=1M conv=fsync" < out/stage90/xnu_card_hfs.img
echo "read-back (the card's first 8 MiB):"
adb -s 4a2fe00b shell "dd if=/dev/block/mmcblk0p25 bs=1M count=8 2>/dev/null" | sha256sum
echo "expected (first 8 MiB of the built card):"
head -c 8388608 out/stage90/xnu_card_hfs.img | sha256sum

echo "== 3. the press (gate + runner, fastboot boot only) =="
# 994 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-994-$(date -u +%Y%m%d-%H%M%S).txt"
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
