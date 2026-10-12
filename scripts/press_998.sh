#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 998 = 997's switch set VERBATIM (DYLD_WITNESS,
# SHREG_PROBE and SHREG_WIRE stay 1) + STAGE90_XNU_SHREG_GEOM=1: G1, the tree's OWN iOS 7 shared-region
# GEOMETRY (`osfmk/vm/vm_shared_region.c`: the `#if IOS7_COMPAT_PLATFORM` block at :626-635 gains the
# 994 gate spelling and `ios7_shreg_geom_enabled()`, so base_address 0x2C000000U / size 0x14000000U and
# the `IOS7LAB shared region ...` printf compile in) + the STACK HALF under the SAME switch
# (`osfmk/arm/status.c` :62/:66 -> LAB_USER_STACK = ios7lab_user_stack() -> 0x2BE00000 instead of
# USRSTACK 0x2FE00000; without it create_unix_stack's FIXED allocation at trunc(0x2FE00000 - MAXSSIZ)
# would land INSIDE the new window for every process - audit docs/reference/d13-shreg-g1-audit.md
# section 2). The MACRO itself is NOT touched (the 994 "do not widen the macro" lesson).
#
# WHY. 997's press (arm armed-d13-518b6a94, commit 9e6307a, log
# out/stage90/captures/cancro-last_kmsg-997-armed-d13-518b6a94.txt:4096-4110) MEASURED the geometry
# wall by value: the four `IOS7LAB cache mapping` lines print (the 32 B wire is open) with ALREADY SLID
# addresses - m0 va=0x2cd9c000 = 0x2C000000 + slide 0xd9c000; m1 carries VM_PROT_SLIDE (init=0x23) -
# then `IOS7SHREG map_file kr=1` (KERN_INVALID_ADDRESS) + `map_slide ret=14`: the tree's default
# 0x30000000/0x10000000 window cannot hold dyld's slid shared cache. 998 opens the geometry.
#
# WHAT THIS PROVES / HOW TO READ IT - THE PRE-REGISTRATION BLOCK BELOW IS THE TEAM-LEAD'S (embedded
# 2026-10-12; FROZEN at the press). This script carries a GUARD (the grep check after
# `set -euo pipefail`): it REFUSES to run unless the block whose first line is
# `# 998 PRESS PRE-REGISTRATION (frozen; ...)` is present - the reading is not pressable before it is
# pre-registered (the 997 pattern: switch set / THE POINT / failure signature(s) / falsification /
# regression guards), and once embedded the block is FROZEN (deviations are reported, not
# reinterpreted - never edit it after the press).
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
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# ====================================================================================================
# 998 PRESS PRE-REGISTRATION (frozen; deviations reported, not reinterpreted)
# ====================================================================================================
# SWITCH SET. 997's verbatim (STAGE90_XNU_DYLD_WITNESS=1, STAGE90_XNU_SHREG_PROBE=1,
# STAGE90_XNU_SHREG_WIRE=1) + STAGE90_XNU_SHREG_GEOM=1; CARD_COW=1, HDD_WRITE=0; RESIDENT rung
# (POST_END_TICKS=0).
#
# THE POINTS. (1) `IOS7LAB shared region 0x2c000000..0x40000000; stack below 0x2be00000` must appear
# ONCE (the create-path printf, vm_shared_region.c:635, first reached at launchd's exec). Its absence
# with the switch=1 in the record = the define did not reach vm_shared_region.o - FALSIFICATION, do
# not judge, rebuild. (2) The four `IOS7LAB cache mapping` lines stay (wire stays on) and
# `IOS7SHREG map_file kr` must be 0 for the first time in this series; `map_slide ret` then becomes
# the slide stage's own outcome (watch for `slide_info initialization failed with kr=` - the section 8
# item 4 hook; also the slide stage's own ret if it fails).
#
# ARITHMETIC THIS FREEZES AGAINST (measured 997 + G1): targets = va - 0x2C000000 =
# 0xd9c000/0xd004000/0xece4000/0x10fd1000, ends 0xd002000/0xece4000/0x10fd1000/0x11148000 - ALL
# < 0x14000000; leaf named-entry size 0x14000000 >= 0x12d51000 - so kr=0 is the ONLY outcome the
# model allows.
#
# FAILURE SIGNATURES, distinguishable: (a) enter-path panic in vm_shared_region_enter/pmap
# (pmap_nest/pmap_expand assertions or `assert(sr_size == 0)`) - audit section 2 says the
# preconditions cannot be met at this call site; a panic there = stop, re-model. (b) map_file kr=3
# (KERN_NO_SPACE -> ENOMEM 12) or kr=1 (-> EFAULT 14) - falsifies the window/offset model despite the
# arithmetic above. (c) kr=2 (KERN_PROTECTION_FAILURE -> EPERM 1) - the pmap nesting refused a chunk.
# (d) full 438 success: dyld's halt outcome changes - either past libbsm or a NEW failure name in the
# witness message; report the new message verbatim if it changes.
#
# CONSTANTS/GUARDS. `check_np ret=12` still; `IOS7LAB vm_allocate64 flags=1 result=0` still; VMHINT991
# exactly 2x at exec. The stack half must hold: the falsifier for it is the `Process 1 exec failed`
# panic or a spawn/exec failure line - any such = the status.c half did not engage.
#
# REGRESSION. Zero `IOS7LAB cache mapping` lines = wire regression, do not judge; zero geom lines =
# the define did not reach the record, do not judge.
if ! grep -q '^# 998 PRESS PRE-REGISTRATION' "$SELF"; then
  echo "REFUSING: the 998 pre-registration block is NOT embedded in this script yet."
  echo "          The team-lead writes it (the 997 pattern: switch set / THE POINT / failure"
  echo "          signature(s) / falsification / regression guards). The build side is ready:"
  echo "          scripts/build_998.sh + scripts/park_998.sh + the 998 clause in build_entry.sh."
  exit 1
fi

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the pressed
# 997/996/995/994 and the confirmed 993 and the regressed 992 arms.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-518b6a94 ]] && {
  echo "REFUSING: out/stage90 holds 997's arm (518b6a94) - already pressed 2026-10-12."
  echo "Build 998 (scripts/build_998.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-cea0c1c2 ]] && {
  echo "REFUSING: out/stage90 holds 996's arm (cea0c1c2) - already pressed 2026-10-12."
  echo "Build 998 (scripts/build_998.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-4efcabae ]] && {
  echo "REFUSING: out/stage90 holds 995's arm (4efcabae) - already pressed 2026-10-12."
  echo "Build 998 (scripts/build_998.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-8b4c9a3e ]] && {
  echo "REFUSING: out/stage90 holds 994's arm (8b4c9a3e) - already pressed 2026-10-12."
  echo "Build 998 (scripts/build_998.sh) first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-1d5cf20e ]] && {
  echo "REFUSING: out/stage90 holds 993's arm (1d5cf20e). Build 998 first."; exit 1; }
[[ $EXPECT_ARM == armed-d13-fce6e8dd ]] && {
  echo "REFUSING: out/stage90 holds 992's arm (fce6e8dd) - the one that panicked at 0xc1233b14."
  echo "Build 998 first."; exit 1; }

echo "== 1. device present? =="
adb devices | grep -q 4a2fe00b || { echo "NO DEVICE - hold Power ~10-15s to boot it back, then re-run"; exit 1; }
fastboot devices | grep -q 33e80afe && { echo "33e80afe (the OTHER phone) is present - unplug it first (hardware gate)"; exit 1; }

echo "== 2. the arm carries the USB ladder, all three SHREG switches AND the open geometry (the readers only help if it does) =="
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
  echo "REFUSING: record has no STAGE90_XNU_SHREG_WIRE=1 - the IOS7LAB cache mapping lines (997's"
  echo "reading) would be absent."; exit 1; }
grep -q '^STAGE90_XNU_SHREG_GEOM=1' out/stage90/xnu_arm_entry-config.txt || {
  echo "REFUSING: record has no STAGE90_XNU_SHREG_GEOM=1 - the geometry/stack half (THE POINT of this"
  echo "rung) would be absent."; exit 1; }
echo "   (record carries USB_PROBE/DEV/ENUM/STREAM=1 + DYLD_WITNESS=1 + SHREG_PROBE=1 + SHREG_WIRE=1 + SHREG_GEOM=1)"

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
# 998 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-998-$(date -u +%Y%m%d-%H%M%S).txt"
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