#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 997 = 996's switch set VERBATIM (DYLD_WITNESS and
# SHREG_PROBE stay 1) + STAGE90_XNU_SHREG_WIRE=1: G3, the tree's OWN 32-byte iOS 7 shared-region wire
# (`bsd/vm/vm_unix.c`: three `#if IOS7_COMPAT_PLATFORM` lines gain the 994 spelling and the
# file-local `ios7lab_cache_abi()` body is gated to `return 1;`, so the 32 B branch at :1190-1220
# compiles in - the MACRO itself is NOT touched, the 994 "do not widen the macro" lesson).
#
# WHY. 996's press (arm armed-d13-cea0c1c2, commit 821fb40, log
# out/stage90/captures/cancro-last_kmsg-996-armed-d13-cea0c1c2.txt :4101-4105) MEASURED the wall by
# value: `IOS7SHREG check_np ret=12` (region exists+empty), `map_slide count=4 slide=0x3811000
# slide_start=0x3f9bd000 size=0x89000`, then `map_file kr=4` (KERN_INVALID_ARGUMENT) + `map_slide
# ret=22` (EINVAL) = `shared_region_copyin_mappings` computed `count * 5 << 2` against dyld 239's
# 32-byte-stride array (dyld 0x2be057ba `add.w r0, r4, r3, lsl #5`; initProt at [r8,#28]), so m[0].size
# read 0 and `vm_map_enter_mem_object`'s `initial_size == 0` check (osfmk/vm/vm_map.c:2344) returned
# EINVAL before any slide work. Same press: the witness read the FULL 102-char halt message and
# `cacheBase=0` - the shared cache was never mapped BECAUSE 438 died. 997 opens the wire.
#
# WHAT THIS PROVES / HOW TO READ IT - the pre-registration below is FROZEN (it is the block the
# builder and the lead agreed on; deviations are reported, not reinterpreted):
#
# 997 PRESS PRE-REGISTRATION (frozen; deviations reported, not reinterpreted)
# - Switch set: 996's verbatim + STAGE90_XNU_SHREG_WIRE=1 (echo in the config record).
# - THE POINT: up to 4x "IOS7LAB cache mapping <n> va=... size=... offset=... max=... init=..." BEFORE 438 fails again. The va fields answer SLID OR NOT (G1's geometry constants depend on this); max/init answer whether VM_PROT_SLIDE (0x20) appears (count=4 may include the slide-info mapping). Report each va verbatim; compare against raw 0x2C000000 model vs slide 0x3811000.
# - Failure signature, one of three, mutually distinguishable:
#   (a) PREDICTED - geometry blocks: "IOS7SHREG map_file kr=3" then "IOS7SHREG map_slide ret=12" (KERN_NO_SPACE -> ENOMEM). Alternative inner return kr=1 (KERN_INVALID_ADDRESS) -> ret=14 (EFAULT): report which.
#   (b) branch's own bound trips (addresses ALREADY SLID, +size crosses 0x40000000, vm_unix.c:1205-1209): NO map_file line at all, "IOS7SHREG map_slide ret=22". Report as (b) -- distinguishable from 996 (996 had map_file kr=4 WITH the line). If (b): stop and re-model before G1; do not widen anything.
#   (c) full success (kr=0, slide stage runs): the reading becomes _shared_region_slide's own outcome.
# - Constant: "IOS7SHREG check_np ret=12" (region exists+empty; ENOMEM precedes the 8-byte copyout); 996's probe lines stay (SHREG_PROBE remains on).
# - FALSIFICATION: zero "IOS7LAB cache mapping" lines == the define did not reach the compiled object (checker ON clauses also fail) -- do not press such an arm.
# - Regression guards: "IOS7LAB vm_allocate64 flags=1 result=0" still present (994 traps); VMHINT991 2x at exec.
#
# THE BASE IS NEVER WRITTEN (CARD_COW=1, HDD_WRITE=0) - fully reversible. RESIDENT rung
# (POST_END_TICKS=0): budget for a black screen + a power-cycle capture (mi4-xnu-reboot-path-cannot-
# reset). The arm carries the full USB ladder (EP1-IN): start the host reader FIRST, it captures the
# console LIVE either way.
#
# Three steps: (1) confirm the device is back, (2) write the REAL card image to userdata's head,
# (3) press. Steps 2 and 3 run the project's own gate + runner - never a raw fastboot, NEVER a flash.
set -euo pipefail
cd "$(dirname "$0")/.."

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the pressed
# 996/995/994 and the confirmed 993 and the regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 997 (scripts/build_997.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 997 (scripts/build_997.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 997 (scripts/build_997.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 997 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 997 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the USB ladder, both probes AND the open wire (the readers only help if it does) =="
grep -q '^STAGE90_XNU_USB_STREAM=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: this entry record has no STAGE90_XNU_USB_STREAM=1 - the host reader below would"
  echo "capture an empty file. Rebuild the arm with the ladder on."; exit 1; }
grep -q '^STAGE90_XNU_DYLD_WITNESS=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: record has no STAGE90_XNU_DYLD_WITNESS=1 - the message/cacheBase readings would be"
  echo "absent."; exit 1; }
grep -q '^STAGE90_XNU_SHREG_PROBE=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: record has no STAGE90_XNU_SHREG_PROBE=1 - the IOS7SHREG lines would be absent."
  exit 1; }
grep -q '^STAGE90_XNU_SHREG_WIRE=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: record has no STAGE90_XNU_SHREG_WIRE=1 - the IOS7LAB cache mapping lines (THE"
  echo "POINT of this rung) would be absent."; exit 1; }
echo "   (record carries USB_PROBE/DEV/ENUM/STREAM=1 + DYLD_WITNESS=1 + SHREG_PROBE=1 + SHREG_WIRE=1)"

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
# 997 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-997-$(date -u +%Y%m%d-%H%M%S).txt"
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