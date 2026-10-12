#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 992 = 991's switch set + STAGE90_XNU_COMMPAGE_ALIAS=1: the
# six dormant IOS7LAB board gates open, so `pmap_create_sharedpage` maps the commpage page a second time
# at 0xFFFF4000 in kernel_pmap (user read-only via L2_ACCESS_USER|L2_ACCESS_APX, NX) and
# `commpage_populate` fills the iOS 7 count bytes at `_COMM_PAGE_BASE_ADDRESS+0x34..0x36`.
#
# WHY 991 IS NOT THE ONE TO PRESS. 991's press (2026-10-12, arm armed-d13-13355bf0, commit af099f4,
# evidence /tmp/cancro-last_kmsg-991.txt) is a READ-CONFIRMED SUCCESS in the pre-registered sense: the
# VMHINT991 guard fired exactly TWICE (`VMHINT991: obj=0xc0005b48 mem=0x1 lr=0x8003d8ec w00=8000
# w04=baddecaf w18=1000 w1c=0 w24=0`, and obj=0xc0005ab0, same shape) and the boot moved PAST the
# kernel-mode exec wall into USERSPACE: /sbin/launchd EXECUTED (dyld ran from the mapped shared cache:
# dyld_all_image_info set, pc/lr in the 0x2be5-0x2be7 region) and then aborted 231x:
#   launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0xffff4090 in map 0xc00057b8 (pmap 0xc0ba3cc0)
#   (Translation fault on page)
# Zero kernel fatal exceptions, zero panics in the whole 22,707-line log.
#
# THE w04 DECODE (991's pre-registered branch, ANSWERED): w04=0xbaddecaf is ZP_NOT_POISONED
# (osfmk/kern/zalloc.c:135-137), left in elem[1] by free_to_zone's default else-branch when
# check-sampling is off; a real _vm_object_allocate would have erased it. The two reported "objects" are
# FREED zone-element memory - a later rung's question. The first abort MOVED to userspace, where the
# commpage wall stopped launchd.
#
# ROOT CAUSE (992), read BY VALUE. 0xffff4090 = the iOS 7 commpage + 0x90. This tree's control ABI is
# `_COMM_PAGE32_BASE_ADDRESS` = 0x40000000 (cpu_capabilities.h:13) and pmap.c's leo_pte_build grants
# exactly THAT va L2_ACCESS_USER; userspace reads the commpage at 0xffff4000, which is unmapped. The fix
# for exactly this symptom already exists in the tree - the 4570/HD2 line's IOS7LAB alias
# (pmap.c:205,2848,3242,3434 + commpage.c:57,99) - compiled out on D13 behind six
# `BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` gates. The alias nests cleanly: arm_vm_init.c:413-423
# maps the high vectors at 0xffff0000 as an L2_SMALL_PAGE, so L2 slot 0xf4 (0xffff4000) is free.
#
# THE FIX (tools/stage_d13_commpage_alias.sh + the switch): appends
# `|| (defined(STAGE90_XNU_COMMPAGE_ALIAS) && STAGE90_XNU_COMMPAGE_ALIAS)` to the six byte-identical gate
# lines (switch OFF compiles the tree's original text byte for byte). The alias block's boot marker is
# staged as a LIVE printf (pmap.c:409's `#define kprintf(args...)` is a no-op) and its read-back asserts
# compile OUT under MACH_ASSERT=0 - so the static read-back is the checker's register binding.
# VERIFIED BY VALUE: tools/check_commpage_alias.py asserts the linked image carries the alias (both
# IOS7LAB anchors, the alias va AND the 0x40000000 control-ABI va inside pmap_create_sharedpage, >=2
# pmap_enter calls with the alias va live in r1 at the second) and REFUSES 991's image.
#
# THE ARM is `armed-d13-<sha8 of out/stage90/xnu_arm_entry.bin>`.  Every switch is 991's (window
# 0x1e400000 = 484 MiB, RESIDENT=1, USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1,
# HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/CARD_COW=1, HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0,
# SP_ALIGN=1, VMHINT_GUARD=1) plus STAGE90_XNU_COMMPAGE_ALIAS=1.  The ENTRY BIN MOVED
# (13355bf0 -> the new sha8).
#
# WHAT THIS PROVES / HOW TO READ IT (the discriminating experiment):
#   The log carries BOTH markers - `IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN`
#   (staged as printf because kprintf is a no-op at pmap.c:409; the three asserts compile out under
#   MACH_ASSERT=0, so there is no read-back left to fire) and `IOS7LAB commpage cpus: configured=...
#   active=... physical=... logical=...` - and launchd's `usermode data abort ...
#   0xffff4090` does NOT recur; the boot moves further into userspace. Candidates for the NEXT wall,
#   named in advance: (a) libdispatch heap from +0x36 (now 1); (b) the commpage's VERSION/timebase
#   fields still ZERO (mach_absolute_time takes the syscall fallback); (c) a new dyld read elsewhere.
#   FALSIFICATION: the SAME `0xffff4090 ... Translation fault` abort recurs => the alias did not reach
#   the pool (the 992 clause should have refused such an image), the perms are wrong (then the SAME page
#   faults with a PERMISSION fault instead - no compiled-out assert can name it), or the alias landed in
#   a pmap the user context does not consult.
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

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the stale 991 arm.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-13355bf0 ]] && {
  echo "REFUSING: out/stage90 holds 991's arm (13355bf0). Build 992 (scripts/build_992.sh) first."; exit 1; }

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
# 992 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-992-$(date -u +%Y%m%d-%H%M%S).txt"
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