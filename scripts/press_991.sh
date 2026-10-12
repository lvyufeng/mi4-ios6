#!/usr/bin/env bash
# Run this WHEN YOU ARE BACK AT THE DEVICE. 991 = 990's switch set + STAGE90_XNU_VMHINT_GUARD=1:
# `vm_page_lookup` treats an implausible `object->memq_hint` as absent FOR THAT CALL ONLY (a local
# shadow; the field is never written).
#
# WHY 990 IS NOT THE ONE TO PRESS. 990's press (2026-10-12, arm armed-d13-0d531453, commit ce0195e,
# evidence /tmp/cancro-last_kmsg.txt) is a READ-CONFIRMED SUCCESS in the pre-registered sense: the
# SP-alignment fix HELD (sp = 0xcfdbba88, 8-aligned; 989's `vld1.32 {d18}` alignment fault did NOT
# recur), the boot ran the WHOLE mount + bsd_init and reached `attempting to start init of /sbin/launchd`,
# then took a NEW first abort INSIDE vm_page_lookup:
#   pc=0x80020838, instruction `ldrd r0,[r5,#24]`
#   r5 = object->memq_hint = 0x1, object = r0 = r4 = 0xc0005b48
#   lr = 0x8003d8ec = vm_map_pmap_enter's vm_page_lookup call (offset 0)
#   far = 0x19 (= 1 + 24), fsr = 1 (alignment fault)
# Then 214x `recursive abort` at pc=0x8046a464 (Debugger trap) and a watchdog reset ~46 s later. The USB
# console capture was EMPTY; the log came from /proc/last_kmsg.
#
# ROOT CAUSE (991), read BY VALUE. `memq_hint` sits at vm_object+0x20 (`ldr r5,[r0,#32]`); NO legitimate
# writer of memq_hint can produce 0x1 - vm_object_template copies in NULL via `_vm_object_allocate`,
# VM_PAGE_INSERT/VM_PAGE_REMOVE write a valid page or NULL, and vm_page_lookup's own hint paths write a
# real page. So either the field was clobbered after allocation, or `object` is not a vm_object: it sits
# ~11 MB below every other heap pointer in the log, consistent with an early `zalloc` allocation.
#
# THE FIX (tools/stage_d13_vmhint_guard.sh + the switch): vm_page_lookup treats `(vm_offset_t)mem <
# 0x80000000u || (mem & 3u)` as absent FOR THAT CALL ONLY (a local shadow; the field is never written),
# after a bounded (max 8) kprintf beginning `VMHINT991: ` that records obj/mem/lr and the object's words
# w00/w04/w18/w1c/w24. VERIFIED BY VALUE: tools/check_vmhint_guard.py asserts the linked image carries
# the guard and REFUSES 990's image. Switch OFF compiles the tree's original text byte for byte (#else
# carries it).
#
# THE ARM is `armed-d13-<sha8 of out/stage90/xnu_arm_entry.bin>`.  Every switch is 990's (window
# 0x1e400000 = 484 MiB, RESIDENT=1, USB_PROBE/DEV/ENUM/STREAM=1, MEM_TOTAL=1, CARD_TOTAL=1,
# HFS_ROOT_MEDIA/EMMC_STRATEGY/ROOT_FROM_CARD/CARD_COW=1, HDD_WRITE=0, SMEM_PROBE=1, IDLE_NO_SLEEP=0,
# SP_ALIGN=1) plus STAGE90_XNU_VMHINT_GUARD=1.  The ENTRY BIN MOVED (0d531453 -> the new sha8).
#
# WHAT THIS PROVES / HOW TO READ IT (the discriminating experiment):
#   The log carries `VMHINT991:` lines (at most 8) naming the object's words and lr, and the boot moves
#   PAST the exec wall => the guard modelled the right thing. If w00 (memq.next) equals obj and the other
#   words look like a live vm_object, the next rung hunts the clobberer; if the words are nonsense, the
#   object identity chain (ubc_getobject -> moc_object) is the bug and lr names the path.
#   FALSIFICATION: the SAME fault recurs (`ldrd r0,[r5,#24]` at pc=0x80020838 with r5=1) => the guard
#   did not reach the pool (the 991 clause should have refused such an image) or an object's hint was
#   plausible-range-but-bad (the guard models only <0x80000000 | &3).
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

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse the stale 990 arm.
EXPECT_ARM=armed-d13-$(sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8)
[[ $EXPECT_ARM == armed-d13-0d531453 ]] && {
  echo "REFUSING: out/stage90 holds 990's arm (0d531453). Build 991 (scripts/build_991.sh) first."; exit 1; }

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
# 991 is a RESIDENT rung: if it does not return, it keeps NO /proc/last_kmsg, so start the HOST reader
# FIRST with --wait, and it attaches the moment the payload enumerates and captures the console LIVE.
USB_OUT="out/stage90/captures/usb-console-991-$(date -u +%Y%m%d-%H%M%S).txt"
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